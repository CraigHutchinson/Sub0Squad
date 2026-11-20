#include "first_use.hpp"

#include "sub0squad/build_id.hpp"

#include "sub0squad/client.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/host_guard.hpp"
#include "sub0squad/paths.hpp"
#include "sub0platform/proc.hpp"
#include "sub0platform/process.hpp"
#include "sub0platform/file.hpp"
#include "sub0platform/net.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::string option(const std::vector<std::string>& a, const std::string& key, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}
bool flag(const std::vector<std::string>& a, const std::string& key) { for (const auto& x : a) if (x == "--" + key) return true; return false; }

/// The directory of a program on PATH ("" when it is not there).
std::string onPath(const std::string& program) { return sub0platform::findOnPath(program).string(); }

bool daemonAnswers(const RuntimePaths& paths)
{
    auto client = ControlClient::connect(paths);
    if (!client) return false;
    client->setReceiveTimeout(3);
    return client->request("ping").has_value();
}

/// The build id the running daemon reports ("" when none answers or it is too old to say).
std::string daemonBuild(const RuntimePaths& paths)
{
    auto client = ControlClient::connect(paths);
    if (!client) return {};
    client->setReceiveTimeout(3);
    const auto reply = client->request("ping");
    if (!reply) return {};
    const std::string key = "\"build\":\"";
    const auto at = reply->find(key);
    return at == std::string::npos ? std::string{} : reply->substr(at + key.size(), reply->find('"', at + key.size()) - at - key.size());
}

/// Whether something accepts connections on 127.0.0.1:port.
bool portInUse(int port) { return sub0platform::loopbackPortInUse(static_cast<std::uint16_t>(port)); }

/// Start `argv` detached (its own session, output appended to `log`); returns its process id, or the reason it could not start.
std::expected<sub0platform::ProcessId, std::string> startDetached(std::vector<std::string> argv, const fs::path& log)
{
    auto child = sub0platform::spawn({.argv = std::move(argv), .outputFile = log, .appendOutput = true, .newSession = true});
    if (!child) return std::unexpected(child.error().message);
    return child->id();   // not waited for: it outlives this command
}

/// How long `up` waits for the daemon to answer, and for the portal to report its port, in tenths of a second.
constexpr int kStartTenths = 100;

int portalPort(const std::vector<std::string>& args)
{
    if (const std::string p = option(args, "portal-port"); !p.empty()) return std::atoi(p.c_str());
    if (const char* env = std::getenv("SUB0SQUAD_PORTAL_PORT"); env && *env) return std::atoi(env);
    return 8780;
}

}  // namespace

std::string resolveProgram(const char* argv0)
{
    const std::string name = argv0 ? argv0 : "";
    if (name.empty()) return {};
    std::error_code ec;
    if (name.find_first_of("/\\") == std::string::npos) return onPath(name);
    const fs::path found = fs::absolute(name, ec);
    return !ec && sub0platform::isExecutable(found) ? found.string() : std::string{};
}

int runUp(const std::vector<std::string>& args, const std::string& exe)
{
    if (std::getenv("SUB0SQUAD_UP_ACTIVE")) { std::fprintf(stderr, "up: refusing to run inside a process that `up` started\n"); return 64; }
    if (exe.empty()) { std::fprintf(stderr, "up: cannot find this program's own path; run it by its full path\n"); return 64; }
    sub0platform::setEnvironment("SUB0SQUAD_UP_ACTIVE", "1");   // inherited by what is started below, so nothing started by `up` can run `up` again
    const RuntimePaths paths = RuntimePaths::fromEnvironment();
    if (const std::string err = paths.ensureDirectories(); !err.empty()) { std::fprintf(stderr, "up: %s\n", err.c_str()); return 1; }
    const fs::path upDir = paths.stateDir / "up";
    std::error_code ec;
    fs::create_directories(upDir, ec);

    if (daemonAnswers(paths)) {
        // A daemon keeps running the program it was started from (catalogue row 20): after a rebuild it is an OLDER build than this command, and every
        // submit would be refused. `up` says so here, where the fix is one command away, instead of at the first submit.
        const std::string theirs = daemonBuild(paths);
        if (theirs != buildId()) std::printf("daemon: already running, but it is ANOTHER BUILD of this program (%s; this command is %s). Restart it: `sub0squad down`, then `sub0squad up`. Until then submits are refused.\n", theirs.empty() ? "it does not say which" : theirs.c_str(), buildId().c_str());
        else std::printf("daemon: already running\n");
    }
    else {
        std::vector<std::string> argv = {exe, "serve", "--max-jobs", option(args, "max-jobs", "4"), "--frontier", option(args, "frontier", "stub")};
        if (const auto started = startDetached(argv, upDir / "daemon.log"); !started) { std::fprintf(stderr, "up: %s\n", started.error().c_str()); return 1; }
        bool up = false;
        for (int i = 0; i < kStartTenths && !up; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); up = daemonAnswers(paths); }
        if (!up) { std::fprintf(stderr, "up: the daemon did not come up; see %s\n", (upDir / "daemon.log").string().c_str()); return 1; }
        std::printf("daemon: started (log: %s)\n", (upDir / "daemon.log").string().c_str());
    }
    if (flag(args, "no-portal")) std::printf("portal: not started (--no-portal)\n");
    else {
        const int port = portalPort(args);   // 0: the system picks a free pair, so parallel instances never collide
        if (port != 0 && portInUse(port)) std::printf("portal: port %d is already in use; open http://127.0.0.1:%d/ (or pick another with --portal-port N)\n", port, port);
        else {
            // The portal writes its port once it holds it: the URL printed is the one it serves, whether asked for or picked by the system.
            const fs::path portFile = upDir / "portal.port";
            fs::remove(portFile, ec);
            const auto pid = startDetached({exe, "portal", "--port", std::to_string(port), "--port-file", portFile.string()}, upDir / "portal.log");
            if (!pid) { std::fprintf(stderr, "up: %s\n", pid.error().c_str()); return 1; }
            (void)sub0platform::writeFile(upDir / "portal.pid", std::to_string(*pid) + "\n");
            int serving = 0;
            for (int i = 0; i < kStartTenths && serving == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::ifstream in(portFile);
                if (!(in >> serving)) serving = 0;
            }
            if (serving == 0) { std::fprintf(stderr, "up: the portal did not start; see %s\n", (upDir / "portal.log").string().c_str()); return 1; }
            std::printf("portal: http://127.0.0.1:%d/  (the work graph; click a node to watch its transcript live)\n", serving);
        }
    }
    std::printf("\nnext:\n"
                "  sub0squad do \"write a calculator in C++\"      start a project (the mock agent: no model needed)\n"
                "  sub0squad project status                      what is running; `project wait w-000001` blocks until it ends\n"
                "  results: %s/<workflow>/project and report.md\n"
                "  sub0squad down                                stop the daemon and the portal\n", paths.workflowsDir().string().c_str());
    return 0;
}

int runDown(const std::vector<std::string>&)
{
    const RuntimePaths paths = RuntimePaths::fromEnvironment();
    int rc = 0;
    if (auto client = ControlClient::connect(paths)) {
        client->setReceiveTimeout(10);
        if (client->request("shutdown")) std::printf("daemon: stopped\n"); else { std::printf("daemon: did not answer the stop request\n"); rc = 1; }
    } else std::printf("daemon: not running\n");
    const fs::path pidFile = paths.stateDir / "up" / "portal.pid";
    std::ifstream in(pidFile);
    long pid = 0;
    if (in >> pid && pid > 1) {
        // Only a process that is still this program's portal is signalled: a pid can be reused after a reboot.
        const auto args = sub0platform::commandLine(pid).value_or(std::vector<std::string>{});
        const bool isPortal = std::ranges::any_of(args, [](const std::string& a) { return a.find("sub0squad") != std::string::npos; }) &&
                              std::ranges::find(args, std::string{"portal"}) != args.end();
        if (isPortal && sub0platform::terminateProcess(pid)) std::printf("portal: stopped\n");
        else std::printf("portal: not running\n");
    } else std::printf("portal: not started by `up`\n");
    std::error_code ec;
    fs::remove(pidFile, ec);
    return rc;
}

std::vector<DoctorCheck> doctorChecks(const std::vector<std::string>& args, const std::string& exe, const std::string& version)
{
    std::vector<DoctorCheck> checks;
    const auto ok = [&](const char* id, const std::string& what) { checks.push_back({id, "ok", what}); };
    const auto note = [&](const char* id, const std::string& what) { checks.push_back({id, "note", what}); };
    const auto fix = [&](const char* id, const std::string& what) { checks.push_back({id, "fix", what}); };

    ok("binary", "sub0squad " + version + (exe.empty() ? std::string{} : " at " + exe));
    const RuntimePaths paths = RuntimePaths::fromEnvironment();
    if (const std::string err = paths.ensureDirectories(); err.empty()) ok("state", "state in " + paths.stateDir.string()); else fix("state", "the state directory cannot be created: " + err + " (set SUB0SQUAD_HOME to a writable directory)");
    if (daemonAnswers(paths)) ok("daemon", "the daemon is running"); else note("daemon", "the daemon is not running: start it with `sub0squad up`");

    const HostMemory mem = readHostMemory();
    if (mem.availableMiB >= 2048) ok("memory", std::to_string(mem.availableMiB) + " MiB of memory available (of " + std::to_string(mem.totalMiB) + ")");
    else fix("memory", "only " + std::to_string(mem.availableMiB) + " MiB of memory available: jobs are shed below 1536 MiB; close something before starting a model");
    if (const auto disk = freeDiskMiB(paths.stateDir); disk >= 1024 || disk < 0) ok("disk", "disk space for the state directory"); else fix("disk", "less than 1 GiB free where the state lives (" + paths.stateDir.string() + ")");

    // Each project harness needs its toolchain; at least one must be usable for any project to run.
    struct Tools { const char* harness; std::vector<const char*> programs; };
    const Tools tools[] = {{"cpp-cmake", {"g++", "cmake", "ninja"}}, {"py-unittest", {"python3"}}, {"web-static", {"node"}}};
    int usable = 0;
    for (const auto& t : tools) {
        if (!findHarness(t.harness)) continue;
        std::string missing;
        for (const char* program : t.programs) if (onPath(program).empty()) missing += (missing.empty() ? "" : ", ") + std::string{program};
        if (missing.empty()) { ok("harness", std::string{"harness "} + t.harness + ": its tools are on PATH"); ++usable; }
        else note("harness", std::string{"harness "} + t.harness + " cannot run: not on PATH: " + missing);
    }
    if (usable == 0) fix("harness", "no project harness has its tools: install a C++ compiler with cmake and ninja, or python3, or node");

    const int port = portalPort(args);
    if (port == 0) ok("ports", "portal port 0: the system picks a free one when the portal starts");
    else if (portInUse(port)) note("ports", "port " + std::to_string(port) + " is in use (a portal is probably running: http://127.0.0.1:" + std::to_string(port) + "/)");
    else ok("ports", "portal port " + std::to_string(port) + " is free");

    if (const std::string engine = option(args, "engine"); !engine.empty()) {
        auto profile = EngineProfile::parse(engine.find('=') == std::string::npos ? "engine=" + engine : engine);
        if (!profile) fix("engine", "--engine: " + profile.error());
        else if (const auto r = httpRequest("GET", profile->endpoint + "/models", "", 5); r && r->status == 200) {
            const auto slots = engineSlots(*profile);
            ok("engine", "the engine at " + profile->endpoint + " answers" + (slots ? " (" + std::to_string(slots->idle) + " of " + std::to_string(slots->total) + " slots idle)" : std::string{}));
        } else fix("engine", "the engine at " + profile->endpoint + " does not answer (" + (r ? "HTTP " + std::to_string(r->status) : r.error()) + "): start the model server, or use the mock agent (the default)");
    } else note("engine", "no --engine given: projects run with the mock agent (reference deliverables, no model); pass --engine NAME=http://127.0.0.1:PORT/v1 to check a model server");
    return checks;
}

int runDoctor(const std::vector<std::string>& args, const std::string& exe, const std::string& version)
{
    int fixes = 0;
    for (const DoctorCheck& c : doctorChecks(args, exe, version)) {
        std::printf("%-5s %s\n", c.level == "fix" ? "FIX" : c.level.c_str(), c.text.c_str());
        if (c.level == "fix") ++fixes;
    }
    if (fixes) std::printf("\n%d thing(s) to fix before a first run\n", fixes);
    else std::printf("\nready: `sub0squad up`, then `sub0squad do \"write a calculator in C++\"`\n");
    return fixes ? 1 : 0;
}

}  // namespace sub0squad
