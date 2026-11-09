// Gate: the daemon is idle at ~0% CPU. With one finished job and one idle-but-running job known to it, over a quiet window the
// daemon's CPU ticks and context switches must not move (it blocks in epoll_wait; no timers, no polling).
#include "e2e.hpp"

using namespace e2e;

static void readCounters(pid_t pid, long& ticks, long& switches)
{
    const std::string stat = slurp("/proc/" + std::to_string(pid) + "/stat");
    const auto close = stat.rfind(')');
    std::istringstream rest(stat.substr(close + 2));
    std::string tok;
    for (int i = 0; i < 11; ++i) rest >> tok;  // state ... majflt cmajflt
    long utime = 0, stime = 0;
    rest >> utime >> stime;
    ticks = utime + stime;
    const std::string status = slurp("/proc/" + std::to_string(pid) + "/status");
    long vol = 0, invol = 0;
    auto grab = [&](const char* key, long& out) { auto p = status.find(key); if (p != std::string::npos) out = std::atol(status.c_str() + p + std::string{key}.size()); };
    grab("voluntary_ctxt_switches:", vol);
    grab("nonvoluntary_ctxt_switches:", invol);
    switches = vol + invol;
}

int main(int, char** argv)
{
    Squad sq(argv[1], "idle");
    sq.startDaemon();
    const std::string done = sq.submitWorker("i1", {"--steps", "2", "--step-ms", "10"});
    const std::string live = sq.submitWorker("i2", {"--steps", "100000", "--step-ms", "1000"});  // running but silent for a second at a time
    CHECK(sq.waitState(done, "succeeded"));
    CHECK(sq.waitState(live, "running"));
    sleepMs(1500);  // settle
    long t0, s0, t1, s1;
    readCounters(sq.daemonPid(), t0, s0);
    const int windowMs = 12000;
    sleepMs(windowMs);
    readCounters(sq.daemonPid(), t1, s1);
    const long hz = sysconf(_SC_CLK_TCK);
    const double cpuPct = 100.0 * static_cast<double>(t1 - t0) / static_cast<double>(hz) / (windowMs / 1000.0);
    std::printf("idle window %d ms: %ld CPU ticks (%.3f %% of one core), %ld context switches\n", windowMs, t1 - t0, cpuPct, s1 - s0);
    CHECK(t1 - t0 <= 1);   // <= 10 ms of CPU in 12 s
    CHECK(s1 - s0 <= 4);   // no polling loop: essentially no wake-ups
    CHECK(sq.cli({"cancel", live}).exitCode == 0);
    CHECK(sq.waitState(live, "cancelled"));
    return finish();
}
