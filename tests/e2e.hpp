#pragma once
// End-to-end harness: a real daemon process and the real CLI, in an isolated SUB0SQUAD_HOME under the build directory.
#include "check.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

extern char** environ;

namespace e2e {

inline std::string slurp(const std::filesystem::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Live (non-zombie) processes whose comm contains `tag` and starts with "s0q-".
inline std::vector<pid_t> pidsByTag(const std::string& tag)
{
    std::vector<pid_t> out;
    DIR* d = opendir("/proc");
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const std::string base = std::string{"/proc/"} + e->d_name;
        const std::string comm = slurp(base + "/comm");
        if (comm.find("s0q-") != 0 || comm.find(tag) == std::string::npos) continue;
        if (slurp(base + "/status").find("State:\tZ") != std::string::npos) continue;
        out.push_back(static_cast<pid_t>(std::atoi(e->d_name)));
    }
    closedir(d);
    return out;
}
inline void killByTag(const std::string& tag) { for (pid_t p : pidsByTag(tag)) kill(p, SIGKILL); }

struct Result { int exitCode = -1; std::string out; };

class Squad
{
public:
    Squad(std::string exe, const char* tag) : exe_(std::move(exe)), home_(makeTempDir(tag)) { setenv("SUB0SQUAD_HOME", home_.c_str(), 1); }
    ~Squad() { stopDaemon(); std::filesystem::remove_all(home_); }

    void startDaemon(int maxJobs = 2)
    {
        const std::string mj = std::to_string(maxJobs);
        const std::string log = (home_ / "daemon.log").string();
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        char* argv[] = {const_cast<char*>(exe_.c_str()), const_cast<char*>("serve"), const_cast<char*>("--max-jobs"), const_cast<char*>(mj.c_str()), nullptr};
        CHECK(posix_spawn(&daemon_, exe_.c_str(), &fa, nullptr, argv, environ) == 0);
        posix_spawn_file_actions_destroy(&fa);
        for (int i = 0; i < 500; ++i) { if (cli({"ping"}).exitCode == 0) return; sleepMs(10); }
        CHECK(false && "daemon did not come up");
    }
    void stopDaemon() { if (daemon_ > 0) { kill(daemon_, SIGTERM); int st; waitpid(daemon_, &st, 0); daemon_ = 0; } }
    void killDaemon() { if (daemon_ > 0) { kill(daemon_, SIGKILL); int st; waitpid(daemon_, &st, 0); daemon_ = 0; } }
    [[nodiscard]] pid_t daemonPid() const { return daemon_; }
    [[nodiscard]] const std::filesystem::path& home() const { return home_; }
    [[nodiscard]] const std::string& exe() const { return exe_; }

    Result cli(std::vector<std::string> args)
    {
        int pipefd[2];
        CHECK(pipe(pipefd) == 0);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addclose(&fa, pipefd[0]);
        std::vector<char*> argv{const_cast<char*>(exe_.c_str())};
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid;
        CHECK(posix_spawn(&pid, exe_.c_str(), &fa, nullptr, argv.data(), environ) == 0);
        posix_spawn_file_actions_destroy(&fa);
        close(pipefd[1]);
        Result r;
        char buf[4096];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof buf)) > 0) r.out.append(buf, static_cast<std::size_t>(n));
        close(pipefd[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        return r;
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
    pid_t daemon_ = 0;
};

}  // namespace e2e
