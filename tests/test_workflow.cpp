// The workflow engine as a pure function on the standard process: planning, dependency order, gates, retry, escalation, failure, cancel, the V's ascent,
// requests, governance, attribution, the manager's strategies.
#include "workflow_sim.hpp"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/briefing.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/division.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/overseer.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/workflow.hpp"

int main()
{
    { // happy path: planner, dependency order, gates, integrator
        SCENARIO("happy-path");
        Sim s = startSim();
        auto a1 = s.step();
        CHECK(a1.size() == 1 && a1[0].kind == WorkflowAction::Kind::startJob && a1[0].job.role == "planner");
        CHECK(s.step().size() == a1.size());                                    // idempotent before the action is applied
        const auto planner = s.run(a1);
        CHECK(s.step().empty());                                                // waits while the planner is active
        s.finishJob(planner[0], JobState::succeeded);
        auto a2 = s.step();
        CHECK(a2.size() == 1 && a2[0].kind == WorkflowAction::Kind::loadPlan);
        s.feed("workflowPlanned", wfbody::planned("w-000001", makePlan()));
        auto a3 = s.step();                                                     // a and c start; b waits for a
        CHECK_EQ(a3.size(), 2u);
        const auto started = s.run(a3);
        CHECK(s.job(started[0])->spec.item == "a" && s.job(started[1])->spec.item == "c");
        s.finishJob(started[0], JobState::succeeded);                           // agent a done -> its gate starts
        auto a4 = s.step();
        CHECK(a4.size() == 1 && a4[0].job.role == "gate" && a4[0].job.item == "a");
        const auto gateA = s.run(a4);
        s.finishJob(gateA[0], JobState::succeeded);                             // a accepted -> b may start
        auto a5 = s.step();
        CHECK(a5.size() == 1 && a5[0].job.item == "b" && a5[0].job.role == "agent");
        const auto agentB = s.run(a5);
        s.finishJob(started[1], JobState::succeeded);
        s.finishJob(agentB[0], JobState::succeeded);
        for (const auto& id : s.run(s.step())) s.finishJob(id, JobState::succeeded);   // gates for b and c
        auto a6 = s.step();
        CHECK(a6.size() == 1 && a6[0].job.role == "integrator");
        const auto integ = s.run(a6);
        s.finishJob(integ[0], JobState::succeeded);
        auto a7 = s.step();
        CHECK(a7.size() == 1 && a7[0].kind == WorkflowAction::Kind::finish && a7[0].finalState == WorkflowState::succeeded);
    }
    { // the ladder for a small item (one file, complexity 1, no tester): retry, rearchitect (a dedicated job), then escalate once
        SCENARIO("ladder-small-item");
        Sim s = startSim(2, FrontierMode::stub);
        s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
        auto ids = s.run(s.step());                                              // agent a attempt 1 local
        s.finishJob(ids[0], JobState::succeeded);
        ids = s.run(s.step());                                                   // gate 1
        s.finishJob(ids[0], JobState::failed);
        auto acts = s.step();                                                    // decision retry + agent attempt 2 local
        CHECK(acts.size() == 2 && acts[0].decisionAction == "retry" && acts[1].job.attempt == 2 && acts[1].job.tier == "local");
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::failed);                                   // agent 2 fails outright
        acts = s.step();                                                         // not splittable, no tester dependency: rearchitect
        CHECK(acts.size() == 2 && acts[0].decisionAction == "rearchitect" && acts[1].job.role == "rearchitect" && acts[1].job.attempt == 3);
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::succeeded);                                // the rewritten card is ready: a new local attempt follows
        acts = s.step();
        CHECK(acts.size() == 1 && acts[0].job.role == "agent" && acts[0].job.attempt == 4 && acts[0].job.tier == "local");
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::failed);
        acts = s.step();                                                         // every local rung tried: escalate once
        CHECK(acts.size() == 2 && acts[0].decisionAction == "escalate" && acts[1].job.tier == "frontier-stub" && acts[1].job.attempt == 5);
        CHECK(acts[0].decisionJson.find("every local rung was tried") != std::string::npos);
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::succeeded);
        ids = s.run(s.step());
        CHECK(s.job(ids[0])->spec.role == "gate" && s.job(ids[0])->spec.tier == "frontier-stub");
        s.finishJob(ids[0], JobState::succeeded);
        const auto st = itemStatuses(*s.wfs.find("w-000001"), s.jobs);
        CHECK(st.size() == 1 && st[0].status == "accepted" && st[0].attempts == 5 && st[0].tier == "frontier-stub");
    }
    { // frontier off: the ladder still runs locally, then fails; no frontier job is ever created
        SCENARIO("frontier-off");
        Sim s = startSim(2, FrontierMode::off);
        s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
        bool sawFrontier = false, sawFail = false;
        for (int guard = 0; guard < 12 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
            const auto acts = s.step();
            for (const auto& a : acts) {
                if (a.kind == WorkflowAction::Kind::startJob && a.job.tier == "frontier-stub") sawFrontier = true;
                if (a.kind == WorkflowAction::Kind::recordDecision && a.decisionAction == "fail") sawFail = true;
            }
            for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "rearchitect" ? JobState::succeeded : JobState::failed);
        }
        CHECK(sawFail && !sawFrontier);
        CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);
    }
    { // divide and conquer: after the local attempts the model decides the division; the parts join the plan and the item stays as their coordinator
        SCENARIO("divide-and-conquer");
        const auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        Plan base;
        base.task = "t";
        base.harness = "web-static";
        WorkItem t; t.id = "t"; t.title = "Tests"; t.goal = "g"; t.persona = "tester"; t.files = {"tests/e.test.mjs"}; t.gate = gate("tests/e.test.mjs", true);
        WorkItem a; a.id = "a"; a.title = "Work"; a.goal = "g"; a.files = {"src/e.js", "src/f.js"}; a.deps = {"t"}; a.complexity = 3; a.gate = gate("tests/e.test.mjs", false);
        WorkItem z; z.id = "z"; z.title = "After"; z.goal = "g"; z.files = {"src/g.js"}; z.deps = {"a"}; z.gate = gate("tests/e.test.mjs", false);
        base.items = {t, a, z};
        const auto part = [&](const char* id, const char* persona, const char* file, std::vector<std::string> deps, bool stub) {
            WorkItem w; w.id = id; w.title = id; w.goal = "g"; w.persona = persona; w.files = {file}; w.deps = std::move(deps); w.gate = gate("tests/f.test.mjs", stub);
            return w;
        };
        Sim s = startSim(2, FrontierMode::stub);
        s.feed("workflowPlanned", wfbody::planned("w-000001", base));
        s.fragment.task = "frag";
        s.fragment.harness = "web-static";
        s.fragment.items = {part("tf", "tester", "tests/f.test.mjs", {}, true), part("x", "developer", "src/e.js", {"tf"}, false), part("y", "developer", "src/f.js", {"x"}, false)};
        // tester t accepted at once
        auto ids = s.run(s.step());
        s.finishJob(ids[0], JobState::succeeded);
        ids = s.run(s.step());
        s.finishJob(ids[0], JobState::succeeded);
        // a: two failed attempts
        ids = s.run(s.step());
        CHECK(s.job(ids[0])->spec.item == "a");
        s.finishJob(ids[0], JobState::failed);
        ids = s.run(s.step());                                                   // retry
        s.finishJob(ids[0], JobState::failed);
        auto acts = s.step();                                                    // the first rung: decompose
        CHECK(acts.size() == 2 && acts[0].decisionAction == "decompose" && acts[1].job.role == "decompose" && acts[1].job.attempt == 3);
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::succeeded);
        acts = s.step();
        CHECK(acts.size() == 1 && acts[0].kind == WorkflowAction::Kind::amendPlan);
        s.run(acts);                                                             // the daemon merges the fragment
        const Plan& merged = *s.wfs.find("w-000001")->plan;
        CHECK(merged.find("a") && merged.find("a")->persona == "integrator" && merged.find("a.tf") && merged.find("a.x") && merged.find("a.y"));
        CHECK(merged.find("a.tf")->deps == std::vector<std::string>{"t"});       // roots inherit the parent's dependencies
        CHECK(merged.find("a.y")->deps == std::vector<std::string>{"a.x"});
        CHECK(merged.find("a")->deps == std::vector<std::string>{"a.y"});        // the coordinator waits for the leaves
        CHECK(merged.find("z")->deps == std::vector<std::string>{"a"});          // dependants still wait for the item, now its integration
        acts = s.step();                                                         // the first part starts at once
        CHECK(acts.size() == 1 && acts[0].job.item == "a.tf" && acts[0].job.role == "agent" && acts[0].job.attempt == 1);
        // an invalid division is rejected, counts as a failed attempt, and the ladder moves on: send back to the producer
        Sim r = startSim(2, FrontierMode::stub);
        r.feed("workflowPlanned", wfbody::planned("w-000001", base));
        r.fragment.task = "frag";
        r.fragment.harness = "web-static";
        r.fragment.items = {part("tf", "tester", "tests/f.test.mjs", {}, true), part("x", "developer", "src/e.js", {"tf"}, false), part("y", "developer", "src/h.js", {"x"}, false)};  // does not cover src/f.js
        ids = r.run(r.step()); r.finishJob(ids[0], JobState::succeeded);
        ids = r.run(r.step()); r.finishJob(ids[0], JobState::succeeded);
        for (int i = 0; i < 2; ++i) { ids = r.run(r.step()); r.finishJob(ids[0], JobState::failed); }
        ids = r.run(r.step());                                                   // decompose job
        r.finishJob(ids[0], JobState::succeeded);
        r.run(r.step());                                                         // amendPlan -> rejected
        CHECK(r.wfs.find("w-000001")->rejectedAmends.count("a#3") == 1);
        acts = r.step();                                                         // rejected division = failed attempt 3: reviseTests
        CHECK(acts.size() >= 2 && acts[0].decisionAction == "reviseTests");
        bool dispute = false;
        for (const auto& act : acts) if (act.kind == WorkflowAction::Kind::writeDispute && act.item == "t" && act.fromItem == "a") dispute = true;
        CHECK(dispute);
        r.run(acts);
        // the tester item t is re-run (no longer accepted), then a continues with a fresh attempt
        acts = r.step();
        CHECK(acts.size() == 1 && acts[0].job.item == "t" && acts[0].job.role == "agent" && acts[0].job.attempt == 2);
        ids = r.run(acts); r.finishJob(ids[0], JobState::succeeded);
        ids = r.run(r.step()); CHECK(r.job(ids[0])->spec.role == "gate" && r.job(ids[0])->spec.item == "t"); r.finishJob(ids[0], JobState::succeeded);
        acts = r.step();
        CHECK(acts.size() == 1 && acts[0].job.item == "a" && acts[0].job.role == "agent" && acts[0].job.attempt == 4 && acts[0].job.tier == "local");
    }
    { // cancel: active jobs are cancelled, then the workflow finishes cancelled
        SCENARIO("cancel");
        Sim s = startSim();
        s.feed("workflowPlanned", wfbody::planned("w-000001", makePlan()));
        s.run(s.step());
        s.feed("workflowCancelRequested", wfbody::cancelRequested("w-000001"));
        auto acts = s.step();
        CHECK(!acts.empty() && acts[0].kind == WorkflowAction::Kind::cancelJob);
        s.run(acts);
        acts = s.step();
        CHECK(acts.size() == 1 && acts[0].finalState == WorkflowState::cancelled);
    }
    { // Row 45 through the engine: on the C++ platform a spent developer item gets a `sections` decision and its next attempt IS the fork executor.
        SCENARIO("sections-on-cpp");
        Sim s;
        s.settings.frontier = FrontierMode::off;
        WorkflowSpec spec;
        spec.task = "t"; spec.sandboxDir = "/sb"; spec.maxLocalAttempts = 1; spec.agent = "llm"; spec.engines = {"e=http://127.0.0.1:9/v1"};
        s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
        Plan p = Plan::fromJson(findHarness("cpp-cmake")->blueprint("calculator-cpp")).value();
        s.feed("workflowPlanned", wfbody::planned("w-000001", p));
        bool sawSections = false, sawDecompose = false, sawFork = false;
        int sectionsAttempt = 0;
        for (int guard = 0; guard < 40 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
            const auto acts = s.step();
            for (const auto& a : acts) {
                if (a.kind == WorkflowAction::Kind::recordDecision && a.decisionAction == "sections") { sawSections = true; sectionsAttempt = a.attempt + 1; CHECK_EQ(a.item, std::string("dev-engine")); }
                if (a.kind == WorkflowAction::Kind::startJob && a.job.role == "decompose") sawDecompose = true;
                if (a.kind == WorkflowAction::Kind::startJob && a.job.role == "agent" && a.job.item == "dev-engine" && a.job.argv.size() > 1) {
                    if (static_cast<int>(a.job.attempt) == sectionsAttempt) { sawFork = true; CHECK_EQ(a.job.argv[1], std::string("llm-fork")); }
                    else if (a.job.attempt == 1) CHECK_EQ(a.job.argv[1], std::string("llm-agent"));
                }
            }
            // testers pass; developers' gates fail, so dev-engine climbs its ladder
            for (const auto& id : s.run(acts)) { const auto* j = s.job(id); s.finishJob(id, j->spec.item.rfind("test-", 0) == 0 || j->spec.role == "agent" || j->spec.role == "rearchitect" ? JobState::succeeded : JobState::failed); }
        }
        CHECK(sawSections && sawFork && !sawDecompose && sectionsAttempt > 1);
    }
    { // The V's ascent: a coordinator integrates its parts, a failed round is sent down to a part, the part is redone, the coordinator integrates again.
        SCENARIO("v-ascent");
        Plan p; p.task = "v"; p.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        WorkItem t; t.id = "t"; t.persona = "tester"; t.files = {"tests/t.test.mjs"}; t.gate = gate("tests/t.test.mjs", true);
        WorkItem d1; d1.id = "d1"; d1.persona = "developer"; d1.files = {"src/a.js"}; d1.deps = {"t"}; d1.gate = gate("tests/t.test.mjs", false);
        WorkItem d2; d2.id = "d2"; d2.persona = "developer"; d2.files = {"src/b.js"}; d2.deps = {"t"}; d2.gate = gate("tests/t.test.mjs", false);
        WorkItem co; co.id = "co"; co.persona = "integrator"; co.deps = {"d1", "d2"}; co.gate = gate("tests/t.test.mjs", false);
        p.items = {t, d1, d2, co};
        CHECK(Plan::fromJson(p.toJson()).has_value());                                                   // an integrator with parts and tests is a valid item
        Plan bad = p; bad.items[3].deps.clear();
        CHECK(!Plan::fromJson(bad.toJson()).has_value());                                                // but not without parts
        Sim s = startSim();
        s.run(s.step());
        s.finishJob("j-000001", JobState::succeeded);
        s.feed("workflowPlanned", wfbody::planned("w-000001", p));
        // drive every started job to success except the coordinator's first gate
        int coGates = 0, sendDowns = 0;
        bool finished = false;
        for (int round = 0; round < 40 && !finished; ++round) {
            const auto acts = s.step();
            for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::sendDown) ++sendDowns;
            const auto started = s.run(acts);
            for (const auto& id : started) {
                const auto* j = s.job(id);
                const bool isCoGate = j->spec.role == "gate" && j->spec.item == "co";
                if (isCoGate) ++coGates;
                s.finishJob(id, isCoGate && coGates == 1 ? JobState::failed : JobState::succeeded);
            }
            for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::finish) finished = true;
        }
        CHECK(finished);
        CHECK_EQ(coGates, 2);       // round 1 failed, round 2 passed
        CHECK_EQ(sendDowns, 1);     // exactly one send-down in between
        int d2Agents = 0;
        for (const auto& id : {"j-000002","j-000003","j-000004","j-000005","j-000006","j-000007","j-000008","j-000009","j-000010","j-000011","j-000012","j-000013","j-000014","j-000015"})
            if (const auto* j = s.job(id); j && j->spec.role == "agent" && j->spec.item == "d2") ++d2Agents;
        CHECK_EQ(d2Agents, 2);      // the part pointed at was redone
    }
    { // Requests: a blocking request holds only the nodes in its scope; answering releases them.
        SCENARIO("requests");
        Sim s = startSim();
        s.feed("workflowPlanned", wfbody::planned("w-000001", makePlan()));
        RequestRecord r; r.id = "r-1"; r.kind = "question"; r.from = "a"; r.title = "which rounding?"; r.blocking = true; r.scope = {"a"};
        s.feed("workflowRequestRaised", wfbody::requestRaised("w-000001", r));
        auto first = s.run(s.step());                       // a is held; c has nothing to wait for
        CHECK_EQ(first.size(), 1u);
        CHECK(s.job(first[0])->spec.item == "c");
        s.feed("workflowRequestAnswered", wfbody::requestAnswered("w-000001", "r-1", "round half up", "", "user"));
        const auto released = s.run(s.step());
        bool aStarted = false;
        for (const auto& id : released) if (s.job(id)->spec.item == "a") aStarted = true;
        CHECK(aStarted);
        CHECK(s.wfs.find("w-000001")->requests.size() == 1 && s.wfs.find("w-000001")->requests[0].status == "answered");
    }
    { // Governance on: an item the ladder cannot finish is deferred to the owner (a blocking request), not failed; the owner decides.
        SCENARIO("governance-defer");
        for (const char* resolution : {"retry", "stop"}) {
            Sim s = startSim(2, FrontierMode::off, true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            bool deferred = false, raised = false;
            for (int guard = 0; guard < 14 && !raised; ++guard) {
                const auto acts = s.step();
                for (const auto& a : acts) {
                    if (a.kind == WorkflowAction::Kind::recordDecision && a.decisionAction == "deferred") deferred = true;
                    if (a.kind == WorkflowAction::Kind::raiseRequest) raised = true;
                }
                for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "rearchitect" ? JobState::succeeded : JobState::failed);
            }
            CHECK(deferred && raised);
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::running);       // not failed: waiting for the owner
            CHECK(s.step().empty() || s.step().front().kind != WorkflowAction::Kind::startJob);  // and nothing new starts meanwhile
            const auto& req = s.wfs.find("w-000001")->requests.at(0);
            CHECK(req.kind == "escalation" && req.blocking && req.scope.size() == 1 && req.scope[0] == "a");
            s.feed("workflowRequestAnswered", wfbody::requestAnswered("w-000001", req.id, "use a simpler approach", resolution, "user"));
            const auto acts = s.step();
            if (std::string_view{resolution} == "retry") { bool started = false; for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::startJob && a.job.role == "agent") started = true; CHECK(started); }
            else { bool failed = false; for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::finish && a.finalState == WorkflowState::failed) failed = true; CHECK(failed); }
        }
    }
    {   // Scenarios driven to their end by EXIT CODES, which the hand-driven ones above never produced (docs/PROCESS_DESIGN.md 1.5: a near pass, a repeated
        // failure, an oversize verdict, an interface gap): each ladder rung is reached the way the gate and the agent reach it, and every pass is traced.
        using Outcome = std::pair<JobState, std::string>;
        const auto drive = [](Sim& s, const std::function<Outcome(const JobRecord&)>& outcome) {
            for (int guard = 0; guard < 80 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) { const Outcome o = outcome(*s.job(id)); s.finishJob(id, o.first, o.second); }
            }
        };
        const auto count = [](Sim& s, const char* action) { int n = 0; for (const auto& d : s.wfs.find("w-000001")->decisions) if (d.action == action) ++n; return n; };
        const auto gate = [](const char* tests, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {tests}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        const auto ok = Outcome{JobState::succeeded, ""};
        {   // gate exit 3 (at least 10 tests, at most 15 % failing): repaired by extra local attempts, never divided
            SCENARIO("near-pass-repairs");
            Sim s = startSim(1, FrontierMode::off);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            drive(s, [&](const JobRecord& j) { return j.spec.role == "gate" && j.spec.attempt < 3 ? Outcome{JobState::failed, "{\"code\":3}"} : ok; });
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "retry"), 2);
            CHECK_EQ(count(s, "decompose") + count(s, "escalate") + count(s, "fail"), 0);
        }
        {   // gate exit 5 (the same tests failed the same way twice) on an item graded by a tester's tests: the tests are sent back, once
            SCENARIO("repeated-failure-sends-tests-back");
            Plan p;
            p.task = "t"; p.harness = "web-static";
            WorkItem t; t.id = "t"; t.title = "Tests"; t.goal = "g"; t.persona = "tester"; t.files = {"tests/e.test.mjs"}; t.gate = gate("tests/e.test.mjs", true);
            WorkItem a; a.id = "a"; a.title = "Work"; a.goal = "g"; a.files = {"src/e.js"}; a.deps = {"t"}; a.gate = gate("tests/e.test.mjs", false);
            p.items = {t, a};
            Sim s = startSim(2, FrontierMode::off);
            s.feed("workflowPlanned", wfbody::planned("w-000001", p));
            drive(s, [&](const JobRecord& j) { return j.spec.role == "gate" && j.spec.item == "a" && j.spec.attempt == 1 ? Outcome{JobState::failed, "{\"code\":5}"} : ok; });
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "reviseTests"), 1);
            CHECK_EQ(count(s, "disputed"), 1);
        }
        {   // agent exit 4 (the item is too big for one attempt): divided at once, and the whole division runs to the integration
            SCENARIO("oversize-divides");
            Plan base;
            base.task = "t"; base.harness = "web-static";
            WorkItem t; t.id = "t"; t.title = "Tests"; t.goal = "g"; t.persona = "tester"; t.files = {"tests/e.test.mjs"}; t.gate = gate("tests/e.test.mjs", true);
            WorkItem a; a.id = "a"; a.title = "Work"; a.goal = "g"; a.files = {"src/e.js", "src/f.js"}; a.deps = {"t"}; a.complexity = 3; a.gate = gate("tests/e.test.mjs", false);
            base.items = {t, a};
            const auto part = [&](const char* id, const char* persona, const char* file, std::vector<std::string> deps, bool stub) {
                WorkItem w; w.id = id; w.title = id; w.goal = "g"; w.persona = persona; w.files = {file}; w.deps = std::move(deps); w.gate = gate("tests/f.test.mjs", stub);
                return w;
            };
            Sim s = startSim(2, FrontierMode::off);
            s.feed("workflowPlanned", wfbody::planned("w-000001", base));
            s.fragment.task = "frag";
            s.fragment.harness = "web-static";
            s.fragment.items = {part("tf", "tester", "tests/f.test.mjs", {}, true), part("x", "developer", "src/e.js", {"tf"}, false), part("y", "developer", "src/f.js", {"x"}, false)};
            drive(s, [&](const JobRecord& j) { return j.spec.role == "agent" && j.spec.item == "a" && j.spec.attempt == 1 ? Outcome{JobState::failed, "{\"code\":4}"} : ok; });
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "decompose"), 1);
            CHECK(s.wfs.find("w-000001")->plan->find("a.x") != nullptr);
        }
        {   // gate exit 6 (an interface gap): the next scope up at once, the card is rewritten, then the attempt passes
            SCENARIO("interface-gap-rearchitects");
            Sim s = startSim(2, FrontierMode::off);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            drive(s, [&](const JobRecord& j) { return j.spec.role == "gate" && j.spec.attempt == 1 ? Outcome{JobState::failed, "{\"code\":6}"} : ok; });
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "rearchitect"), 1);
            CHECK_EQ(count(s, "retry"), 0);
        }
    }
    {   // Row 14: a workflow that fails names the item that failed FIRST, and lists what failed after it. (The loop used to keep whichever failing item it reached
        // LAST in plan order: with a and c both failing it named c, and once blamed a part that had never been attempted.)
        SCENARIO("first-cause-attribution");
        Plan p;
        p.task = "t"; p.harness = "web-static";
        WorkItem a; a.id = "a"; a.title = "A"; a.goal = "g"; a.files = {"src/calc.js"}; a.gate.kind = "node-test"; a.gate.tests = {"tests/eval.test.mjs"};
        WorkItem c; c.id = "c"; c.title = "C"; c.goal = "g"; c.files = {"style.css"}; c.gate = a.gate;
        p.items = {a, c};
        Sim s = startSim(1, FrontierMode::off);
        s.feed("workflowPlanned", wfbody::planned("w-000001", p));
        for (int guard = 0; guard < 80 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
            const auto acts = s.step();
            for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "gate" ? JobState::failed : JobState::succeeded);
        }
        const WorkflowRecord& wf = *s.wfs.find("w-000001");
        CHECK(wf.state == WorkflowState::failed);
        CHECK_EQ(firstCause(wf), std::string("a"));
        CHECK(laterFailures(wf) == std::vector<std::string>{"c"});
        CHECK(wf.note.find("item 'a' failed after the policy ran out of options (it failed first; later failures: 'c')") != std::string::npos);
        CHECK(firstCause(WorkflowRecord{}).empty());
    }
    { // The manager solves before anyone is asked: a spent ladder applies strategies in order (think, then a stronger engine when configured), each once;
        SCENARIO("manager-strategies");
      // only then does the item fail (or, with --ask, go to the owner). The stronger engine is what the next attempt's job is built with.
        for (const bool withFallback : {false, true}) {
            Sim s;
            s.settings.frontier = FrontierMode::off;
            WorkflowSpec spec;
            spec.task = "t"; spec.sandboxDir = "/sb"; spec.maxLocalAttempts = 1; spec.agent = "llm";
            spec.engines = {"small=http://127.0.0.1:8091/v1,model=m,slots=2", "big=http://127.0.0.1:8092/v1,model=m,slots=2"};
            if (withFallback) spec.fallbackEngine = "big";
            s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            std::vector<std::string> strategies, engineOfAgent;
            for (int guard = 0; guard < 40 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& a : acts) {
                    if (a.kind == WorkflowAction::Kind::setStrategy) strategies.push_back(a.note);
                    if (a.kind == WorkflowAction::Kind::startJob && a.job.role == "agent") {
                        std::string eng;
                        for (std::size_t k = 0; k + 1 < a.job.argv.size(); ++k) if (a.job.argv[k] == "--engine") eng = a.job.argv[k + 1].substr(0, a.job.argv[k + 1].find('='));
                        engineOfAgent.push_back(eng);
                    }
                }
                for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "rearchitect" || s.job(id)->spec.role == "decompose" ? JobState::succeeded : JobState::failed);
            }
            CHECK(strategies.size() == (withFallback ? 2u : 1u));
            CHECK(!strategies.empty() && strategies[0] == "think");
            if (withFallback) CHECK(strategies.size() == 2 && strategies[1] == "engine" && engineOfAgent.back() == "big" && engineOfAgent.front() == "small");
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);   // after the strategies, a failed item fails the workflow (no --ask)
            CHECK(s.wfs.find("w-000001")->requests.empty());                  // and nothing was ever put to the owner
        }
    }
    checkGoldens(std::filesystem::path{SUB0SQUAD_TEST_DATA} / "actions", "engine");
    return finish();
}
