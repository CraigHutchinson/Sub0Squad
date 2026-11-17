// End-to-end gates (M1_PLAN.md (archived: docs/ARCHIVE.md)): cancel leaves no child; a dead supervisor is `lost`, never `running`; a daemon crash leaves
// jobs running and the next daemon adopts them; status reports progress without parsing logs. Then the daemon's edges: a hung daemon is a clear
// error within the reply timeout, --exit-with ends a daemon with the process it serves, and a deep home's moved socket name is removed or cleared.
#include "e2e.hpp"
#include "sub0squad/platform_file.hpp"
#include "sub0squad/platform_ipc.hpp"

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
        const ProcessId shim = std::atoll(sq.statusField(id, "shimPid").c_str());
        CHECK(shim > 0);
        CHECK(sub0squad::platform::killProcess(shim));  // the supervisor dies without writing an exit record
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

    { // Shims the waiter cannot watch (its handle limit on Windows, no pidfd on an old kernel) are polled: a job still finishes, a cancel still lands.
        sub0squad::platform::setEnvironment("SUB0SQUAD_TEST_POLL_SHIMS", "1");
        Squad sq(exe, "polled");
        sq.startDaemon();
        sub0squad::platform::unsetEnvironment("SUB0SQUAD_TEST_POLL_SHIMS");   // the daemon has it; the CLI and the jobs need not
        const std::string done = sq.submitWorker("q1", {"--steps", "3", "--step-ms", "20"});
        CHECK(sq.waitState(done, "succeeded"));
        const std::string live = sq.submitWorker("q2", {"--steps", "10000", "--step-ms", "20"}, {"--grace", "1"});
        CHECK(sq.waitState(live, "running"));
        CHECK(sq.cli({"cancel", live}).exitCode == 0);
        CHECK(sq.waitState(live, "cancelled"));
        CHECK(slurp(sq.home() / "daemon.log").find("watched by polling") != std::string::npos);
    }

    { // Gate: status reports progress without parsing logs (logs deleted, garbage in them, progress still correct).
        Squad sq(exe, "progress");
        sq.startDaemon();
        const std::string id = sq.submitWorker("p1", {"--steps", "40", "--step-ms", "50", "--metric", "tokens=10"});
        CHECK(sq.waitState(id, "running"));
        sleepMs(400);
        const auto runDir = sq.home() / "state" / "jobs" / id / "run-1";
        { std::ofstream o(runDir / "stdout.log", std::ios::trunc); o << "done 99/99 garbage that must not be parsed\n"; }
        std::error_code ignored;
        std::filesystem::remove(runDir / "stderr.log", ignored);   // Windows refuses while the job holds it open; either way status must not read it
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
    { // Gate: resume continues a recorded optimization-run-1 style run: attempts preserved, new attempts appended, same job.
        Squad sq(exe, "resume");
        sq.startDaemon();
        const std::string state = (sq.home() / "state.json").string();
        const std::string id = sq.submitWorker("r1", {"--steps", "6", "--step-ms", "10", "--fail-at", "4", "--state-file", state},
            {"--state-file", state, "--state-schema", "optimization-run-1", "--resume-arg", exe, "--resume-arg", "mock-worker",
             "--resume-arg", "--tag", "--resume-arg", "r2", "--resume-arg", "--steps", "--resume-arg", "6", "--resume-arg", "--step-ms",
             "--resume-arg", "10", "--resume-arg", "--resume", "--resume-arg", "--state-file", "--resume-arg", state});
        CHECK(sq.waitState(id, "failed"));
        std::string s1 = sq.cli({"status", id, "--json"}).out;
        CHECK(s1.find("\"progress\":{\"done\":3,\"total\":6") != std::string::npos);  // from the tool's own record, adapter status running
        CHECK(sq.cli({"resume", id}).exitCode == 0);
        CHECK(sq.waitState(id, "succeeded"));
        s1 = sq.cli({"status", id, "--json"}).out;
        CHECK(s1.find("\"run\":2") != std::string::npos);
        CHECK(s1.find("\"progress\":{\"done\":6,\"total\":6") != std::string::npos);
        CHECK(std::filesystem::exists(sq.home() / "state" / "jobs" / id / "run-1" / "stdout.log"));  // the interrupted run's evidence is kept
        CHECK(sq.cli({"resume", id}).exitCode == 1);  // a succeeded job cannot be resumed
        CHECK(sq.cli({"resume", "j-999999"}).exitCode == 1);
    }
    { // `wait --timeout` returns 124 while the job keeps running.
        Squad sq(exe, "waitto");
        sq.startDaemon();
        const std::string id = sq.submitWorker("w1", {"--steps", "10000", "--step-ms", "20"});
        CHECK(sq.waitState(id, "running"));
        CHECK_EQ(sq.cli({"wait", id, "--timeout", "1"}).exitCode, 124);
        CHECK(sq.statusField(id, "state") == "running");
        CHECK(sq.cli({"cancel", id}).exitCode == 0);
        CHECK(sq.cli({"wait", id}).exitCode == 1);  // cancelled is not success
    }
    { // The memory guard at submit: a job that declares more memory than the host has is refused with the numbers, nothing is queued.
        Squad sq(exe, "memguard");
        sq.startDaemon();
        const Result r = sq.cli({"submit", "--memory-mib", "99999999", "--", "true"});
        CHECK(r.exitCode != 0);
        CHECK(sq.cli({"status", "--json"}).out.find("\"jobs\":[]") != std::string::npos);
        CHECK(sq.cli({"submit", "--memory-mib", "1", "--", "true"}).exitCode == 0);
    }
    { // Resource lease: four job slots but a lease of 2 on one resource. Never more than 2 holders run at once, the rest wait, and a
      // lease-free job is not blocked behind the full lease.
        Squad sq(exe, "lease");
        sq.startDaemon(4);
        std::vector<std::string> ids;
        for (int i = 0; i < 5; ++i)
            ids.push_back(sq.submitWorker("lz" + std::to_string(i), {"--steps", "8", "--step-ms", "100"}, {"--lease", "engine-a=2"}));
        const std::string free = sq.submitWorker("lzfree", {"--steps", "2", "--step-ms", "50"});
        CHECK(sq.waitState(free, "succeeded"));  // finished while the leased jobs still queue or run
        // Count from ONE snapshot of all jobs per tick. Asking for each job in turn is not a snapshot: a holder can finish and the next start between two
        // questions, so three looked "running" at once (or, on a loaded host, the two were never seen together): docs/INTERVENTIONS.md row 22.
        int peak = 0;
        for (int tick = 0; tick < 400; ++tick) {
            const std::string snap = sq.cli({"status", "--json"}).out;
            int running = 0, done = 0;
            for (const auto& id : ids) {
                const auto at = snap.find("\"id\":\"" + id + "\",\"state\":\"");
                if (at == std::string::npos) continue;
                const auto from = at + id.size() + 17;
                const std::string st = snap.substr(from, snap.find('"', from) - from);
                if (st == "running") ++running;
                if (st == "succeeded") ++done;
            }
            peak = std::max(peak, running);
            if (done == 5) break;
            sleepMs(50);
        }
        CHECK(peak == 2);
        for (const auto& id : ids) CHECK(sq.waitState(id, "succeeded"));
    }
    if (std::filesystem::exists("/proc/self/oom_score_adj")) { // OOM priority (Linux): jobs run with oom_score_adj 600 and servers 900, so the kernel's last resort takes them before the owner's session.
        Squad sq(exe, "oomadj");
        sq.startDaemon();
        const std::string job = sq.submitWorker("oj", {"--steps", "200", "--step-ms", "50"});
        const std::string srv = sq.submitWorker("os", {"--steps", "200", "--step-ms", "50"}, {"--service"});
        CHECK(sq.waitState(job, "running") && sq.waitState(srv, "running"));
        for (int i = 0; i < 200 && (pidsByTag("oj").empty() || pidsByTag("os").empty()); ++i) sleepMs(10);
        const auto adj = [](const std::string& tag) {
            const auto pids = pidsByTag(tag);
            if (pids.empty()) return -1;
            std::ifstream in("/proc/" + std::to_string(pids.front()) + "/oom_score_adj");
            int v = -1;
            in >> v;
            return v;
        };
        CHECK(adj("oj") >= 600);
        CHECK(adj("os") >= 900);
        (void)sq.cli({"cancel", job});
        (void)sq.cli({"cancel", srv});
    }
    {   // Row 19: with the only job slot held by a worker, a WORKFLOW's structural jobs (no engine lease) still run, from their reserve, and the workflow
        // finishes; a plain job waits for the slot. (A client cannot label a job as a workflow's: the daemon clears such labels, so a real workflow is used.)
        Squad sq(exe, "classes");
        sq.startDaemon(1);
        const std::string worker = sq.submitWorker("cw", {"--steps", "1200", "--step-ms", "50"});   // a minute: longer than everything below
        CHECK(sq.waitState(worker, "running"));
        const std::filesystem::path file = sq.home() / "two-jobs.json";
        { std::ofstream(file) << "{\"schema\":\"process-1\",\"name\":\"two-jobs\",\"stages\":["
                                   "{\"name\":\"scan\",\"kind\":\"job\",\"job\":{\"step\":\"scan\",\"tool\":\"qa\",\"args\":[\"${sandbox}\"]}},"
                                   "{\"name\":\"again\",\"kind\":\"job\",\"entry\":\"stage.scan.done\",\"job\":{\"step\":\"rescan\",\"tool\":\"qa\",\"args\":[\"${sandbox}\"]}}]}\n"; }
        CHECK_EQ(sq.cli({"process", "add", "two-jobs", "--file", file.string()}).exitCode, 0);
        CHECK_EQ(sq.cli({"project", "submit", "--task", "custom", "--process", "two-jobs"}).exitCode, 0);
        const std::string plain = sq.submitWorker("cp", {"--steps", "1", "--step-ms", "5"});
        CHECK_EQ(sq.cli({"project", "wait", "w-000001", "--timeout", "40"}).exitCode, 0);   // both structural jobs ran while the worker held the only slot
        CHECK(sq.cli({"project", "status", "w-000001", "--json"}).out.find("\"state\":\"succeeded\"") != std::string::npos);
        CHECK(sq.statusField(worker, "state") == "running");               // the worker was never disturbed
        CHECK(sq.statusField(plain, "state") == "queued");                 // a plain job has no reserve: it still waits
        (void)sq.cli({"cancel", worker});
        CHECK(sq.waitState(plain, "succeeded", 30000));
    }
    { // Memory pressure: below the reserve the daemon sheds the newest job and admits nothing; servers only when critically low; it recovers.
        const std::string fake = (std::filesystem::current_path() / "fake-memavail.txt").string();
        { std::ofstream o(fake); o << 100000; }
        sub0squad::platform::setEnvironment("SUB0SQUAD_FAKE_MEMAVAILABLE_FILE", fake);
        Squad sq(exe, "shed");
        sq.startDaemon(4, {"--mem-shed-mib", "2000"});
        const std::string older = sq.submitWorker("sa", {"--steps", "400", "--step-ms", "50"});
        CHECK(sq.waitState(older, "running"));
        sleepMs(300);
        const std::string newer = sq.submitWorker("sb", {"--steps", "400", "--step-ms", "50"});
        const std::string srv = sq.submitWorker("ss", {"--steps", "400", "--step-ms", "50"}, {"--service"});
        CHECK(sq.waitState(newer, "running") && sq.waitState(srv, "running"));
        { std::ofstream o(fake, std::ios::trunc); o << 1500; }  // under the 2000 reserve, above half of it
        CHECK_EQ(sq.cli({"ping"}).exitCode, 0);                  // a ping is pure: it must not make the watcher decide
        const std::string untouched = sq.statusField(newer, "state");
        CHECK(untouched == "running" || untouched == "cancelling"); // running unless the daemon's own timer happened to fire: the ping itself decided nothing
        CHECK_EQ(sq.cli({"check"}).exitCode, 0);                 // `check` runs the watchers before it answers: no timer to wait for (row 22)
        const std::string decided = sq.statusField(newer, "state");
        CHECK(decided == "cancelling" || decided == "cancelled"); // decided already, whatever the host's load: the newest plain job goes first
        CHECK(sq.waitState(newer, "cancelled", 60000));          // only its teardown takes time
        CHECK(sq.statusField(newer, "note").find("shed: memory pressure") != std::string::npos);
        CHECK(sq.statusField(srv, "state") == "running");        // the server survives while memory is only low
        { std::ofstream o(fake, std::ios::trunc); o << 100000; }
        const std::string after = sq.submitWorker("sc", {"--steps", "2", "--step-ms", "20"});
        CHECK_EQ(sq.cli({"check"}).exitCode, 0);
        CHECK(sq.waitState(after, "succeeded", 60000));          // admission resumes once memory recovers
        sub0squad::platform::unsetEnvironment("SUB0SQUAD_FAKE_MEMAVAILABLE_FILE");
        (void)sq.cli({"cancel", older});
        (void)sq.cli({"cancel", srv});
    }

    { // A hung daemon: something listens on the control socket and never answers. The CLI says so within its reply timeout instead of hanging.
        Squad sq(exe, "hung");
        const auto sock = sq.home() / "run" / "control.sock";
        std::filesystem::create_directories(sock.parent_path());
        auto hung = sub0squad::platform::listenLocal(sock);   // accepts in the system's backlog, reads nothing, answers nothing
        CHECK(hung.has_value());
        sub0squad::platform::setEnvironment("SUB0SQUAD_REPLY_TIMEOUT", "1");
        const auto began = std::chrono::steady_clock::now();
        const Result r = sq.cli({"ping"}, true);
        CHECK(r.exitCode != 0);
        CHECK(r.out.find("timed out: the daemon did not answer 'ping' within 1 s") != std::string::npos);
        CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(20));
        CHECK(sq.cli({"status"}, true).out.find("did not answer 'status'") != std::string::npos);
        sub0squad::platform::unsetEnvironment("SUB0SQUAD_REPLY_TIMEOUT");
    }

    { // --exit-with: the daemon stops once the process it serves ends (a test killed on its timeout leaves no daemon behind), and refuses a
      // process that does not exist.
        Squad sq(exe, "exitwith");
        auto served = sub0squad::platform::spawn({.argv = {exe, "mock-worker", "--tag", "xw", "--steps", "100000", "--step-ms", "20"}, .outputFile = sq.home() / "served.log"});
        CHECK(served.has_value());
        if (served) {
            auto daemon = sub0squad::platform::spawn({.argv = {exe, "serve", "--exit-with", std::to_string(served->id())}, .outputFile = sq.home() / "daemon.log"});
            CHECK(daemon.has_value());
            bool up = false;
            for (int i = 0; i < 500 && !up; ++i) { up = sq.cli({"ping"}).exitCode == 0; if (!up) sleepMs(10); }
            CHECK(up);
            served->kill();
            (void)served->wait();
            bool gone = false;
            const auto start = daemon ? sub0squad::platform::processStart(daemon->id()) : std::nullopt;
            for (int i = 0; i < 200 && start && !gone; ++i) { gone = !sub0squad::platform::isAlive(daemon->id(), *start); if (!gone) sleepMs(50); }
            CHECK(gone);   // within ten seconds: it checks once a second
            if (daemon) { if (!gone) daemon->kill(); (void)daemon->wait(); }
            CHECK(slurp(sq.home() / "daemon.log").find("has ended: stopping") != std::string::npos);
        }
        killByTag("xw");
        auto none = sub0squad::platform::runCaptured({.argv = {exe, "serve", "--exit-with", "999999999"}}, {.timeout = std::chrono::seconds(30), .includeStderr = true});
        CHECK(none.has_value() && none->status.code != 0 && none->output.find("no such process") != std::string::npos);
    }

    { // A home deeper than sockaddr_un allows (Windows moves the socket to a short name in the user's profile): the daemon serves it, removes the
      // moved name when it stops, and a name left by a killed daemon is cleared as stale.
        Squad sq(exe, "deep");
        const auto deep = sq.home() / std::string(110, 'h');
        std::filesystem::create_directories(deep);
        sub0squad::platform::setEnvironment("SUB0SQUAD_HOME", deep.string());
        const auto sock = deep / "run" / "control.sock";
        const auto file = sub0squad::platform::localSocketFile(sock);
        CHECK(file.has_value());
        const auto present = [&] { return file && sub0squad::platform::modifiedSeconds(*file).has_value(); };   // reads attributes, opens nothing
        sq.startDaemon();
        CHECK(sq.cli({"ping"}).exitCode == 0);
        CHECK(present());
        sq.stopDaemon();
        CHECK(!present());   // removed as the daemon stopped
        sq.startDaemon();
        CHECK(present());
        sq.killDaemon();     // no chance to clean up
        if (file && *file != sock) {
            CHECK(present());
            CHECK(sub0squad::platform::clearStaleLocalSockets(std::chrono::seconds(0)) >= 1);
            CHECK(!present());
        }
        sub0squad::platform::setEnvironment("SUB0SQUAD_HOME", sq.home().string());
    }
    return finish();
}
