// Gate: the daemon is idle at ~0% CPU. With one finished job and one idle-but-running job known to it, over a quiet window the
// daemon's CPU time and (where the system counts them) context switches must not move (it blocks in its Waiter; no timers, no polling).
#include "e2e.hpp"

using namespace e2e;

int main(int, char** argv)
{
    Squad sq(argv[1], "idle");
    sq.startDaemon();
    const std::string done = sq.submitWorker("i1", {"--steps", "2", "--step-ms", "10"});
    const std::string live = sq.submitWorker("i2", {"--steps", "100000", "--step-ms", "1000"});  // running but silent for a second at a time
    CHECK(sq.waitState(done, "succeeded"));
    CHECK(sq.waitState(live, "running"));
    sleepMs(1500);  // settle
    const auto before = sub0squad::platform::processUsage(sq.daemonPid());
    const int windowMs = 12000;
    sleepMs(windowMs);
    const auto after = sub0squad::platform::processUsage(sq.daemonPid());
    CHECK(before && after);
    if (before && after) {
        const double cpu = after->cpuSeconds - before->cpuSeconds;
        std::printf("idle window %d ms: %.3f s of CPU (%.3f %% of one core)\n", windowMs, cpu, 100.0 * cpu / (windowMs / 1000.0));
        CHECK(cpu <= 0.016);   // one clock tick at most (10 ms on Linux; Windows counts in 15.6 ms quanta)
        if (before->contextSwitches && after->contextSwitches) {
            const auto switches = *after->contextSwitches - *before->contextSwitches;
            std::printf("  %llu context switches\n", static_cast<unsigned long long>(switches));
            CHECK(switches <= 4);   // no polling loop: essentially no wake-ups
        }
    }
    CHECK(sq.cli({"cancel", live}).exitCode == 0);
    CHECK(sq.waitState(live, "cancelled"));
    return finish();
}
