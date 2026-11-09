// End-to-end gates (docs/M1_PLAN.md): cancel leaves no child; a dead supervisor is `lost`, never `running`; a daemon crash leaves
// jobs running and the next daemon adopts them; status reports progress without parsing logs.
#include "e2e.hpp"

using namespace e2e;

int main(int, char** argv)
{
    const std::string exe = argv[1];

    { // Gate: cancel leaves no child (worker + stubborn server + setsid escapee), released, cancelled not failed. Repeated.
        Squad sq(exe, "cancel");
        sq.startDaemon();
        for (int rep = 0; rep < 10; ++rep) {
            const std::string tag = "c" + std::to_string(rep);
            const std::string id = sq.submitWorker(tag, {"--steps", "10000", "--step-ms", "20", "--server", "--escape", "--ignore-term"}, {"--grace", "1"});
            for (int i = 0; i < 500 && pidsByTag(tag).size() < 3; ++i) sleepMs(10);
            CHECK(pidsByTag(tag).size() >= 3);
            const Result c = sq.cli({"cancel", id});
            CHECK(c.exitCode == 0);
            CHECK(sq.waitState(id, "cancelled"));
            CHECK(pidsByTag(tag).empty());
            CHECK(sq.cli({"status", id, "--json"}).out.find("\"teardown\":\"verified\"") != std::string::npos);
        }
    }

    { // Gate: a dead supervisor is `lost`, never `running` - with the daemon up and with it down.
        Squad sq(exe, "lost");
        sq.startDaemon();
        const std::string id = sq.submitWorker("l1", {"--steps", "10000", "--step-ms", "20"});
        CHECK(sq.waitState(id, "running"));
        const pid_t shim = static_cast<pid_t>(std::atoi(sq.statusField(id, "shimPid").c_str()));
        CHECK(shim > 0);
        kill(shim, SIGKILL);  // the supervisor dies without writing an exit record
        CHECK(sq.waitState(id, "lost"));
        CHECK(sq.statusField(id, "note").find("vanished") != std::string::npos);
        killByTag("l1");
        sq.stopDaemon();
        const Result offline = sq.cli({"status", id, "--json"});  // no daemon: derived from journal + disk
        CHECK(offline.out.find("\"state\":\"lost\"") != std::string::npos);
        CHECK(offline.out.find("\"source\":\"journal\"") != std::string::npos);
    }

    { // A daemon crash leaves the job running; offline status says running; the next daemon adopts it and records the outcome.
        Squad sq(exe, "adopt");
        sq.startDaemon();
        const std::string id = sq.submitWorker("a1", {"--steps", "30", "--step-ms", "100"});
        CHECK(sq.waitState(id, "running"));
        sq.killDaemon();
        CHECK(sq.cli({"status", id, "--json"}).out.find("\"state\":\"running\"") != std::string::npos);
        sq.startDaemon();
        CHECK(sq.waitState(id, "succeeded"));
        sq.stopDaemon();
    }

    { // A crash while no daemon runs: outcome comes from exit.json, and the next daemon journals it.
        Squad sq(exe, "offline-exit");
        sq.startDaemon();
        const std::string id = sq.submitWorker("o1", {"--steps", "3", "--step-ms", "50"});
        CHECK(sq.waitState(id, "running"));
        sq.killDaemon();
        sleepMs(600);  // the shim finishes alone
        CHECK(sq.cli({"status", id, "--json"}).out.find("\"state\":\"succeeded\"") != std::string::npos);
        sq.startDaemon();
        CHECK(sq.waitState(id, "succeeded"));
    }

    { // Gate: status reports progress without parsing logs (logs deleted, garbage in them, progress still correct).
        Squad sq(exe, "progress");
        sq.startDaemon();
        const std::string id = sq.submitWorker("p1", {"--steps", "40", "--step-ms", "50", "--metric", "tokens=10"});
        CHECK(sq.waitState(id, "running"));
        sleepMs(400);
        const auto runDir = sq.home() / "state" / "jobs" / id / "run-1";
        { std::ofstream o(runDir / "stdout.log", std::ios::trunc); o << "done 99/99 garbage that must not be parsed\n"; }
        std::filesystem::remove(runDir / "stderr.log");
        const std::string s = sq.cli({"status", id, "--json"}).out;
        const auto p = s.find("\"progress\":{\"done\":");
        CHECK(p != std::string::npos);
        const int done = p == std::string::npos ? -1 : std::atoi(s.c_str() + p + 19);
        CHECK(done > 0 && done < 40);
        CHECK(s.find("\"metrics\":{\"tokens\":") != std::string::npos);
        CHECK(sq.cli({"cancel", id}).exitCode == 0);
        CHECK(sq.waitState(id, "cancelled"));
    }

    { // Concurrency limit: with maxJobs 1 the second job waits queued.
        Squad sq(exe, "limit");
        sq.startDaemon(1);
        const std::string a = sq.submitWorker("q1", {"--steps", "10000", "--step-ms", "20"});
        const std::string b = sq.submitWorker("q2", {"--steps", "2", "--step-ms", "10"});
        CHECK(sq.waitState(a, "running"));
        sleepMs(200);
        CHECK(sq.statusField(b, "state") == "queued");
        CHECK(sq.cli({"cancel", a}).exitCode == 0);
        CHECK(sq.waitState(b, "succeeded"));
    }
    return finish();
}
