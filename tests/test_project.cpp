// End-to-end: a project-scope task is delegated by an internal decision into work items, run in parallel through the squad, gated,
// retried, escalated to the stub frontier tier, integrated, and accounted in the ledger. Real compiler runs decide every outcome.
#include "e2e.hpp"

using namespace e2e;

static std::string field(const std::string& s, const std::string& key)
{
    const std::string pat = "\"" + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    if (s[p] == '"') return s.substr(p + 1, s.find('"', p + 1) - p - 1);
    return s.substr(p, s.find_first_of(",}", p) - p);
}

static std::string wfStatus(Squad& sq, const std::string& id) { return sq.cli({"project", "status", id, "--json"}).out; }

static bool waitWorkflow(Squad& sq, const std::string& id, const std::string& want, int ms = 90000, int* maxRunning = nullptr)
{
    for (int i = 0; i < ms / 100; ++i) {
        const std::string s = wfStatus(sq, id);
        if (maxRunning) {
            const std::string jobs = sq.cli({"status", "--json"}).out;
            int n = 0;
            for (auto p = jobs.find("\"state\":\"running\""); p != std::string::npos; p = jobs.find("\"state\":\"running\"", p + 1)) ++n;
            if (n > *maxRunning) *maxRunning = n;
        }
        if (field(s, "state") == want) return true;
        if (field(s, "state") != "running" && field(s, "state") != "") return false;
        sleepMs(100);
    }
    return false;
}

int main(int, char** argv)
{
    const std::string exe = argv[1];

    { // The headline scenario, started by TASK NAME with no plan supplied: a blueprint of a registered harness loads itself. Parallel fan-out, a retry
      // that succeeds, an escalation, integration, a working calculator.
        Squad sq(exe, "proj");
        sq.startDaemon(4);
        const std::string id = sq.cli({"project", "submit", "--task", "calculator-py", "--inject", "dev-engine", "--inject", "test-engine:1"}).out;
        const std::string wf = id.substr(0, id.find('\n'));
        CHECK(wf == "w-000001");
        int maxRunning = 0;
        CHECK(waitWorkflow(sq, wf, "succeeded", 120000, &maxRunning));
        CHECK(maxRunning >= 2);  // independent items really ran in parallel
        const std::string s = wfStatus(sq, wf);
        for (const char* item : {"test-engine", "dev-engine", "test-app", "dev-app"}) {
            const std::string pat = std::string{"\"id\":\""} + item + "\",\"title\"";
            CHECK(s.find(pat) != std::string::npos);
        }
        CHECK(s.find("\"status\":\"failed\"") == std::string::npos && s.find("\"status\":\"pending\"") == std::string::npos);
        CHECK(s.find("\"action\":\"retry\"") != std::string::npos);      // test-engine: failed once, retried locally, passed
        CHECK(s.find("\"action\":\"escalate\"") != std::string::npos);   // dev-engine: local never passes, escalated once
        CHECK(s.find("\"frontier-stub\":{\"attempts\":2") != std::string::npos);  // one agent and one gate on the stub tier
        const double share = std::atof(field(s, "frontierTokenShare").c_str());
        CHECK(share > 0.0 && share < 0.5);                                // escalation is the exception, not the rule
        CHECK(s.find("\"stub\":true") != std::string::npos);
        // The calculator the squad built works.
        const auto project = sq.home() / "state" / "workflows" / wf / "project";
        CHECK(std::filesystem::exists(project / "calc" / "engine.py") && std::filesystem::exists(sq.home() / "state" / "workflows" / wf / "report.md"));
        const Result r = sq.cli({"submit", "--cwd", project.string(), "--wall", "20", "--", "python3", "-m", "calc", "2^3^2"});
        CHECK(r.exitCode == 0);
        // Stopping the daemon leaves the workflow readable offline.
        sq.stopDaemon();
        const std::string off = sq.cli({"project", "status", wf, "--json"}).out;
        CHECK(field(off, "state") == "succeeded" && off.find("\"source\":\"journal\"") != std::string::npos);
    }

    { // Frontier off: the policy must fail the item after the ladder; no frontier job is ever created.
        Squad sq(exe, "projoff");
        sq.startDaemon(4, {"--frontier", "off"});
        const std::string wf = sq.cli({"project", "submit", "--task", "calculator-py", "--inject", "dev-engine"}).out.substr(0, 8);
        CHECK(waitWorkflow(sq, wf, "failed", 120000));
        const std::string s = wfStatus(sq, wf);
        CHECK(s.find("frontier mode is off") != std::string::npos);
        CHECK(s.find("frontier-stub") == std::string::npos);
        CHECK(s.find("\"id\":\"dev-engine\"") != std::string::npos && s.find("\"status\":\"failed\"") != std::string::npos);
    }

    { // An unknown task name: no blueprint, so the workflow awaits a plan (the deferred architect) and can be cancelled while waiting.
        Squad sq(exe, "projbad");
        sq.startDaemon(2);
        const std::string wf = sq.cli({"project", "submit", "--task", "world-domination"}).out.substr(0, 8);
        CHECK(field(wfStatus(sq, wf), "state") == "awaiting-plan");
        CHECK(sq.cli({"project", "cancel", wf}).exitCode == 0);
        CHECK(field(wfStatus(sq, wf), "state") == "cancelled");
    }

    { // Cancel mid-flight: every job is cancelled, the workflow is cancelled, nothing is left running.
        Squad sq(exe, "projcancel");
        sq.startDaemon(4);
        const std::string wf = sq.cli({"project", "submit", "--task", "calculator-py"}).out.substr(0, 8);
        for (int i = 0; i < 100 && wfStatus(sq, wf).find("\"jobsActive\":0") != std::string::npos; ++i) sleepMs(50);
        sleepMs(700);
        CHECK(sq.cli({"project", "cancel", wf}).exitCode == 0);
        CHECK(waitWorkflow(sq, wf, "cancelled"));
        CHECK(field(wfStatus(sq, wf), "jobsActive") == "0");
        CHECK(sq.cli({"project", "cancel", wf}).exitCode == 1);  // already finished
    }

    { // A daemon crash mid-workflow: jobs keep running, the next daemon adopts them and the level-triggered engine finishes the project.
        Squad sq(exe, "projcrash");
        sq.startDaemon(4);
        const std::string wf = sq.cli({"project", "submit", "--task", "calculator-py", "--inject", "test-engine:1"}).out.substr(0, 8);
        sleepMs(1500);
        sq.killDaemon();
        sleepMs(400);
        sq.startDaemon(4);
        CHECK(waitWorkflow(sq, wf, "succeeded"));
        const std::string s = wfStatus(sq, wf);
        CHECK(s.find("\"status\":\"accepted\"") != std::string::npos && s.find("\"status\":\"failed\"") == std::string::npos);
    }
    { // The web project with personas, test-first handoff, retry, escalation, and the work-versus-harness metrics.
        Squad sq(exe, "projweb");
        sq.startDaemon(4);
        const Result d = sq.cli({"do", "write a scientific calculator as a web page", "--inject", "test-engine:1", "--inject", "dev-engine"});
        CHECK(d.exitCode == 0 && d.out.find("architect: not needed") != std::string::npos);
        const std::string wf = "w-000001";
        CHECK(waitWorkflow(sq, wf, "succeeded", 90000));
        const std::string s = wfStatus(sq, wf);
        CHECK(s.find("\"action\":\"retry\"") != std::string::npos && s.find("\"action\":\"escalate\"") != std::string::npos);
        CHECK(s.find("\"workRatio\":") != std::string::npos && field(s, "workRatio") != "0");
        CHECK(s.find("\"harnessBytes\":3") != std::string::npos);  // the harness supplied its scaffold: work the agents did not do
        const auto project = sq.home() / "state" / "workflows" / wf / "project";
        CHECK(std::filesystem::exists(project / "index.html") && std::filesystem::exists(project / "src" / "calc.js") && std::filesystem::exists(project / "serve.mjs"));
        const Result t = sq.cli({"submit", "--cwd", project.string(), "--wall", "60", "--", "node", "--test", "tests/"});
        CHECK(t.exitCode == 0);
    }

    { // The whole escalation ladder through the daemon: local retries, a decompose job that declines, the tester revises the tests (send back to
      // the producer), a rearchitect job rewrites the card, then one escalation to the stub tier. Every rung counts as an attempt.
        Squad sq(exe, "projladder");
        sq.startDaemon(4);
        sq.cli({"do", "write a scientific calculator as a web page", "--inject", "dev-engine"});
        const std::string wf = "w-000001";
        CHECK(waitWorkflow(sq, wf, "succeeded", 120000));
        const std::string s = wfStatus(sq, wf);
        for (const char* action : {"retry", "decompose", "reviseTests", "rearchitect", "escalate"})
            CHECK(s.find(std::string{"\"action\":\""} + action + "\"") != std::string::npos);
        CHECK(s.find("\"action\":\"disputed\"") != std::string::npos);   // the producer's earlier acceptance was voided
        const auto engine = sq.home() / "state" / "workflows" / wf / "items" / "dev-engine";
        CHECK(std::filesystem::exists(engine / "card-override.json"));       // the dedicated job rewrote the card
        const auto tests = sq.home() / "state" / "workflows" / wf / "items" / "test-engine";
        CHECK(std::filesystem::exists(tests / "attempt-2"));                  // the tester item was re-run after the dispute
        CHECK(std::filesystem::exists(tests / "dispute.txt"));               // what the producer was told (the mock agent does not consume it)
        CHECK(field(wfStatus(sq, wf), "state") == "succeeded");
    }

    { // An unknown task: triage defers the architect as a file handoff; supplying the plan starts the workflow.
        Squad sq(exe, "projawait");
        sq.startDaemon(4);
        const Result d = sq.cli({"do", "build a unit converter page with length and mass"});
        CHECK(d.exitCode == 0 && d.out.find("architect: deferred") != std::string::npos && d.out.find("awaiting a plan") != std::string::npos);
        const std::string wf = "w-000001";
        CHECK(field(wfStatus(sq, wf), "state") == "awaiting-plan");
        const auto packet = sq.home() / "state" / "workflows" / wf / "needs-plan.md";
        CHECK(std::filesystem::exists(packet) && slurp(packet).find("plan-2 syntax") != std::string::npos);
        const auto planFile = sq.home() / "plan.json";
        { std::ofstream o(planFile); o << sq.cli({"blueprint", "calculator-web"}).out; }
        CHECK(sq.cli({"project", "plan", wf, "--file", (sq.home() / "missing.json").string()}).exitCode != 0);
        { std::ofstream o(sq.home() / "bad.json"); o << "{\"schema\":\"plan-2\",\"items\":[]}"; }
        CHECK(sq.cli({"project", "plan", wf, "--file", (sq.home() / "bad.json").string()}).exitCode != 0);  // an invalid plan is rejected with a reason
        CHECK(sq.cli({"project", "plan", wf, "--file", planFile.string()}).exitCode == 0);
        CHECK(waitWorkflow(sq, wf, "succeeded", 90000));
        CHECK(sq.cli({"project", "plan", wf, "--file", planFile.string()}).exitCode != 0);  // no longer awaiting
    }
    { // The request inbox from the CLI: raise a blocking question, see it open, answer it, see the answer.
        Squad sq(exe, "inbox");
        sq.startDaemon(2);
        const Result d = sq.cli({"do", "build a unit converter page with length and mass"});   // no blueprint: the workflow waits for a plan
        CHECK(d.exitCode == 0);
        const Result raised = sq.cli({"request", "raise", "w-000001", "--kind", "question", "--title", "metric only?", "--blocking", "--option", "metric", "--option", "metric and imperial"});
        CHECK(raised.exitCode == 0 && raised.out.find("\"rid\":\"r-1\"") != std::string::npos);
        const Result open = sq.cli({"request", "list", "w-000001"});
        CHECK(open.out.find("r-1") != std::string::npos && open.out.find("open") != std::string::npos && open.out.find("(blocking)") != std::string::npos);
        CHECK(sq.cli({"request", "raise", "w-000001", "--kind", "nonsense", "--title", "x"}).exitCode != 0);       // unknown kind refused
        CHECK(sq.cli({"request", "answer", "w-000001", "r-9", "--answer", "x"}).exitCode != 0);                    // unknown request refused
        CHECK(sq.cli({"request", "answer", "w-000001", "r-1", "--answer", "metric and imperial"}).exitCode == 0);
        const Result done = sq.cli({"request", "list", "w-000001"});
        CHECK(done.out.find("answered") != std::string::npos && done.out.find("=> metric and imperial") != std::string::npos);
        CHECK(sq.cli({"request", "answer", "w-000001", "r-1", "--answer", "again"}).exitCode != 0);                // already answered
    }
    return finish();
}
