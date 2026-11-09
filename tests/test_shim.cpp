// Process-tree gates at the shim level: normal exit, failure, crash, cancel with a stubborn server and a setsid/double-fork
// escapee, leftovers after the main command exits, and wall timeout.
#include "check.hpp"
#include "sub0squad/job_spec.hpp"
#include "sub0squad/supervisor.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <thread>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string gExe;

static std::string slurp(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

// Live (non-zombie) processes whose name carries our tag.
static int countByTag(const std::string& tag)
{
    int n = 0;
    DIR* d = opendir("/proc");
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const std::string base = std::string{"/proc/"} + e->d_name;
        std::string comm = slurp(base + "/comm");
        if (comm.find("s0q-") != 0 || comm.find(tag) == std::string::npos) continue;
        if (slurp(base + "/status").find("State:\tZ") != std::string::npos) continue;
        ++n;
    }
    closedir(d);
    return n;
}

struct Run { fs::path dir; pid_t shim = 0; };

static Run startRun(const char* tag, std::vector<std::string> workerArgs, std::int64_t wall = 0, std::int64_t grace = 1)
{
    Run r;
    r.dir = makeTempDir(tag);
    JobSpec spec;
    spec.argv = {gExe, "mock-worker", "--tag", tag};
    for (auto& a : workerArgs) spec.argv.push_back(a);
    spec.wallSeconds = wall;
    spec.graceSeconds = grace;
    { std::ofstream o(r.dir / "spec.json"); o << spec.toJson(); }
    auto pid = spawnShim(gExe, r.dir);
    CHECK(pid.has_value());
    r.shim = pid.value_or(0);
    return r;
}

static int waitShim(const Run& r)
{
    int status = 0;
    waitpid(r.shim, &status, 0);
    return status;
}

static bool waitForFile(const fs::path& p, int ms = 5000)
{
    for (int i = 0; i < ms / 10; ++i) { if (fs::exists(p)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return false;
}

int main(int, char** argv)
{
    gExe = argv[1];

    { // normal exit
        auto r = startRun("n1", {"--steps", "3", "--step-ms", "5"});
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "exited" && ex->code == 0 && ex->teardown == "verified");
        const std::string ev = slurp(r.dir / "events.ndjson");
        CHECK(ev.find("\"done\":3") != std::string::npos);
        CHECK(slurp(r.dir / "stdout.log").find("mock-worker n1") != std::string::npos);
        CHECK(slurp(r.dir / "stderr.log").find("stderr line") != std::string::npos);
    }
    { // failure code
        auto r = startRun("f1", {"--steps", "4", "--step-ms", "5", "--fail-at", "2"});
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "exited" && ex->code == 3);
    }
    { // crash
        auto r = startRun("c1", {"--steps", "4", "--step-ms", "5", "--crash-at", "2"});
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->signal == SIGSEGV);
    }
    { // main exits but leaves a server and an escapee behind: both must be gone and the record says verified
        auto r = startRun("l1", {"--steps", "1", "--step-ms", "50", "--server", "--escape"});
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "exited" && ex->teardown == "verified");
        CHECK_EQ(countByTag("l1"), 0);
    }
    for (int rep = 0; rep < 20; ++rep) { // cancel: worker + stubborn server + escapee, all ignoring SIGTERM; repeated for leaks
        const std::string tag = "k" + std::to_string(rep);
        auto r = startRun(tag.c_str(), {"--steps", "1000", "--step-ms", "20", "--server", "--escape", "--ignore-term"});
        CHECK(waitForFile(r.dir / "shim.json"));
        for (int i = 0; i < 300 && countByTag(tag) < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(countByTag(tag) >= 3);  // worker, server and escapee are all alive before the cancel
        kill(r.shim, SIGTERM);
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "cancelled" && ex->teardown == "verified");
        CHECK_EQ(countByTag(tag), 0);
    }
    { // wall timeout
        auto r = startRun("t1", {"--steps", "5", "--stall-at", "1"}, /*wall*/ 1, /*grace*/ 1);
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "timeout" && ex->teardown == "verified");
        CHECK_EQ(countByTag("t1"), 0);
    }
    return finish();
}
