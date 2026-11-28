// Process-tree gates at the shim level: normal exit, failure, crash, cancel with a stubborn server and a setsid/double-fork
// escapee, leftovers after the main command exits, and wall timeout.
#include "check.hpp"
#include "sub0squad/job_spec.hpp"
#include "sub0squad/supervisor.hpp"

#include "tagged.hpp"
#include "sub0platform/lease.hpp"
#include "sub0platform/proc.hpp"
#include "sub0platform/wait.hpp"

#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string gExe;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

static int countByTag(const std::string& tag) { return tagged::count(tag); }

struct Run { fs::path dir; sub0platform::ProcessId shim = 0; };

static Run startRun(const char* tag, std::vector<std::string> workerArgs, std::int64_t wall = 0, std::int64_t grace = 1,
                    std::optional<sub0platform::LeaseMode> machineLease = std::nullopt)
{
    Run r;
    r.dir = makeTempDir(tag);
    JobSpec spec;
    spec.argv = {gExe, "mock-worker", "--tag", tag};
    for (auto& a : workerArgs) spec.argv.push_back(a);
    spec.wallSeconds = wall;
    spec.graceSeconds = grace;
    spec.machineLease = machineLease;
    { std::ofstream o(r.dir / "spec.json"); o << spec.toJson(); }
    auto pid = spawnShim(gExe, r.dir);
    CHECK(pid.has_value());
    r.shim = pid.value_or(0);
    return r;
}

/// Wait (up to two minutes) for the shim to end; it is this process's child, so the wait also reaps it.
static void waitShim(const Run& r)
{
    auto w = sub0platform::Waiter::create();
    CHECK(w.has_value());
    if (!w || !w->watchExit(r.shim, 1)) return;   // already gone
    for (int i = 0; i < 240; ++i) {
        auto events = w->wait(std::chrono::milliseconds(500));
        if (events && !events->empty()) return;
    }
    CHECK(false && "the shim did not end");
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
        CHECK(ex && ex->signal == 11);   // SIGSEGV, or the access violation on Windows
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
        CHECK(sub0platform::terminateProcess(r.shim));
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        CHECK(ex && ex->outcome == "cancelled" && ex->teardown == "verified");
        CHECK_EQ(countByTag(tag), 0);
    }
    { // wall timeout
        auto r = startRun("t1", {"--steps", "5", "--stall-at", "1"}, /*wall*/ 1, /*grace*/ 1);
        waitShim(r);
        auto ex = readExitRecord(r.dir);
        const bool timedOut = ex && ex->outcome == "timeout" && ex->teardown == "verified";
        CHECK(timedOut);
        const int left = countByTag("t1");
        CHECK_EQ(left, 0);
        // This check failed once and could not be reproduced (docs/INTERVENTIONS.md row 58): if it fails again, the evidence is printed rather than lost.
        if (!timedOut || left != 0)
            std::fprintf(stderr, "wall timeout: exit.json=[%s] shim.json=[%s] processes left=%d events=[%s] stderr=[%s]\n", slurp(r.dir / "exit.json").c_str(), slurp(r.dir / "shim.json").c_str(), left,
                         slurp(r.dir / "events.ndjson").c_str(), slurp(r.dir / "stderr.log").c_str());
    }
    {
        using sub0platform::LeaseMode;
        using sub0platform::MachineLease;
        const auto state = makeTempDir("lease-state");
#ifdef _WIN32
        sub0platform::setEnvironment("LOCALAPPDATA", state.string());
#else
        sub0platform::setEnvironment("XDG_STATE_HOME", state.string());
#endif
        auto holder = MachineLease::acquire(LeaseMode::exclusive, "timing fixture", "calibration");
        CHECK(holder && holder->has_value());
        auto blocked = startRun("lease-blocked", {"--steps", "1"}, 10, 1, LeaseMode::shared);
        waitShim(blocked);
        const auto exit = readExitRecord(blocked.dir);
        CHECK(exit && exit->outcome == "startFailed");
        CHECK(!fs::exists(blocked.dir / "shim.json"));
        const auto diagnostic = slurp(blocked.dir / "stderr.log");
        CHECK(diagnostic.find("timing fixture") != std::string::npos);
        CHECK(diagnostic.find("calibration") != std::string::npos);
        if (holder) holder->reset();

        auto live = startRun("lease-live", {"--steps", "1000", "--step-ms", "20"}, 15, 1, LeaseMode::shared);
        CHECK(waitForFile(live.dir / "shim.json"));
        {
            auto reader = MachineLease::acquire(LeaseMode::shared, "fixture", "another reader");
            CHECK(reader && reader->has_value());
            auto writer = MachineLease::acquire(LeaseMode::exclusive, "fixture", "exclusive timing");
            CHECK(writer && !writer->has_value());
        }
        CHECK(sub0platform::terminateProcess(live.shim));
        waitShim(live);
        const auto cancelled = readExitRecord(live.dir);
        CHECK(cancelled && cancelled->outcome == "cancelled" && cancelled->teardown == "verified");
        {
            auto writer = MachineLease::acquire(LeaseMode::exclusive, "fixture", "after teardown");
            CHECK(writer && writer->has_value());
        }
        removeTree(blocked.dir);
        removeTree(live.dir);
        removeTree(state);
    }
    return finish();
}
