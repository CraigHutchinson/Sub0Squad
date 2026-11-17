#pragma once
// End-to-end harness: a real daemon process and the real CLI, in an isolated SUB0SQUAD_HOME under the build directory.
#include "check.hpp"

#include "tagged.hpp"
#include "sub0squad/platform_proc.hpp"
#include "sub0squad/platform_process.hpp"

#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace e2e {

using sub0squad::platform::ProcessId;

inline std::string slurp(const std::filesystem::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Live processes of the mock worker run tagged `tag` (tagged.hpp).
inline std::vector<ProcessId> pidsByTag(const std::string& tag) { return tagged::processes(tag); }
inline void killByTag(const std::string& tag) { tagged::killAll(tag); }

struct Result { int exitCode = -1; std::string out; };

class Squad
{
public:
    Squad(std::string exe, const char* tag) : exe_(std::move(exe)), home_(makeTempDir(tag)) { sub0squad::platform::setEnvironment("SUB0SQUAD_HOME", home_.string()); }
    ~Squad() { stopDaemon(); removeTree(home_); }

    void startDaemon(int maxJobs = 2, std::vector<std::string> extra = {})
    {
        sub0squad::platform::ProcessSpec spec{.argv = {exe_, "serve", "--max-jobs", std::to_string(maxJobs)}, .outputFile = home_ / "daemon.log", .appendOutput = true};
        for (auto& e : extra) spec.argv.push_back(e);
        auto child = sub0squad::platform::spawn(spec);
        CHECK(child.has_value());
        if (!child) return;
        daemon_ = std::move(*child);
        for (int i = 0; i < 500; ++i) { if (cli({"ping"}).exitCode == 0) return; sleepMs(10); }
        CHECK(false && "daemon did not come up");
        // Say why: the last ping's own words and the daemon's log (the home is removed with the test, so this is the only record).
        std::fprintf(stderr, "ping: %s\ndaemon.log:\n%s\n", cli({"ping"}, true).out.c_str(), slurp(home_ / "daemon.log").c_str());
    }
    /// Ask the daemon to stop (SIGTERM; on Windows its stop event) and wait for it.
    void stopDaemon() { if (daemon_.id() > 0) { (void)sub0squad::platform::terminateProcess(daemon_.id()); (void)daemon_.wait(); daemon_ = {}; } }
    void killDaemon() { if (daemon_.id() > 0) { daemon_.kill(); (void)daemon_.wait(); daemon_ = {}; } }
    [[nodiscard]] ProcessId daemonPid() const { return daemon_.id(); }
    [[nodiscard]] const std::filesystem::path& home() const { return home_; }
    [[nodiscard]] const std::string& exe() const { return exe_; }

    /// Run the CLI to its end: its stdout, and its stderr too when `withStderr` (otherwise discarded).
    Result cli(std::vector<std::string> args, bool withStderr = false)
    {
        sub0squad::platform::ProcessSpec spec{.argv = {exe_}};
        for (auto& a : args) spec.argv.push_back(a);
        auto ran = sub0squad::platform::runCaptured(spec, {.timeout = std::chrono::minutes(10), .maxBytes = 64u << 20, .includeStderr = withStderr, .discardStderr = !withStderr});
        CHECK(ran.has_value());
        if (!ran) return {};
        return Result{ran->status.exited() ? ran->status.code : -1, ran->output};
    }

    /// Submit `exe mock-worker <workerArgs>` with extra submit options; returns the job id.
    std::string submitWorker(const std::string& tag, std::vector<std::string> workerArgs, std::vector<std::string> submitOpts = {})
    {
        std::vector<std::string> a{"submit"};
        for (auto& o : submitOpts) a.push_back(o);
        a.insert(a.end(), {"--", exe_, "mock-worker", "--tag", tag});
        for (auto& w : workerArgs) a.push_back(w);
        Result r = cli(a);
        CHECK(r.exitCode == 0);
        while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == ' ')) r.out.pop_back();
        return r.out;
    }

    /// Value of `"key":"..."` for the job in `status --json` (naive but enough for tests).
    std::string statusField(const std::string& id, const std::string& key)
    {
        const std::string s = cli({"status", id, "--json"}).out;
        const std::string pat = "\"" + key + "\":";
        auto p = s.find(pat);
        if (p == std::string::npos) return {};
        p += pat.size();
        if (s[p] == '"') { auto e = s.find('"', p + 1); return s.substr(p + 1, e - p - 1); }
        auto e = s.find_first_of(",}", p);
        return s.substr(p, e - p);
    }

    bool waitState(const std::string& id, const std::string& want, int ms = 15000)
    {
        for (int i = 0; i < ms / 20; ++i) { if (statusField(id, "state") == want) return true; sleepMs(20); }
        return false;
    }

private:
    std::string exe_;
    std::filesystem::path home_;
    sub0squad::platform::Child daemon_;
};

}  // namespace e2e
