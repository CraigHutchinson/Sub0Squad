// The workflow engine as a pure function: planning, dependency order, gates, retry, escalation, failure, cancel, idempotence.
#include "check.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/briefing.hpp"
#include "sub0squad/overseer.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/workflow.hpp"

using namespace sub0squad;

struct Sim
{
    JobStore jobs;
    WorkflowStore wfs;
    std::uint64_t seq = 0;
    WorkflowSettings settings{"/bin/sub0squad", FrontierMode::stub};
    RulePolicy policy;

    void feed(const char* type, const std::string& body)
    {
        JournalRecord r{++seq, 1000.0 + static_cast<double>(seq), type, body};
        jobs.apply(r);
        wfs.apply(r);
    }
    Plan fragment;   // what the next decompose job "wrote"
    std::vector<WorkflowAction> step() { return advance(*wfs.find("w-000001"), jobs, settings, policy); }
    // Apply actions the way the daemon does; returns ids of jobs started.
    std::vector<std::string> run(const std::vector<WorkflowAction>& acts)
    {
        std::vector<std::string> started;
        for (const auto& a : acts) {
            switch (a.kind) {
                case WorkflowAction::Kind::startJob: { const std::string id = jobs.nextId(); feed("jobSubmitted", journalbody::submitted(id, a.job)); started.push_back(id); break; }
                case WorkflowAction::Kind::recordDecision: feed("workflowDecision", wfbody::decision("w-000001", a.item, a.attempt, a.decisionAction, a.decisionTier, a.decisionJson)); break;
                case WorkflowAction::Kind::finish: feed("workflowFinished", wfbody::finished("w-000001", a.finalState, a.note)); break;
                case WorkflowAction::Kind::cancelJob: feed("jobCancelRequested", journalbody::cancelRequested(a.jobId, 1)); feed("jobFinished", journalbody::finished(a.jobId, 1, JobState::cancelled, "", "cancelled")); break;
                case WorkflowAction::Kind::loadPlan: break;
                case WorkflowAction::Kind::writeDispute: break;
                case WorkflowAction::Kind::raiseRequest: { RequestRecord r = a.request; r.id = "r-" + std::to_string(wfs.find("w-000001")->requests.size() + 1); feed("workflowRequestRaised", wfbody::requestRaised("w-000001", r)); break; }
                case WorkflowAction::Kind::sendDown: feed("workflowDecision", wfbody::decision("w-000001", "d2", 1, "disputed", "local", "{}")); break;  // the daemon picks the part; here always d2
                case WorkflowAction::Kind::amendPlan: {
                    auto merged = Plan::amended(*wfs.find("w-000001")->plan, a.item, fragment);
                    if (merged) feed("workflowPlanAmended", wfbody::planAmended("w-000001", a.item, *merged));
                    else feed("workflowAmendRejected", wfbody::amendRejected("w-000001", a.item, a.attempt, merged.error()));
                    break;
                }
            }
        }
        return started;
    }
    void finishJob(const std::string& id, JobState s) { feed("jobFinished", journalbody::finished(id, jobs.find(id)->run, s, "", "")); }
    const JobRecord* job(const std::string& id) { return jobs.find(id); }
};

static Plan makePlan()
{
    Plan p;
    p.task = "t";
    p.items = {{"a", "A", "goal a", {"a.hpp"}, {}, 1}, {"b", "B", "goal b", {"b.hpp"}, {"a"}, 2}, {"c", "C", "goal c", {"c.hpp"}, {}, 1}};
    return p;
}

static Sim startSim(int maxLocal = 2, FrontierMode fm = FrontierMode::stub, bool ask = false)
{
    Sim s;
    s.settings.frontier = fm;
    WorkflowSpec spec;
    spec.task = "t";
    spec.sandboxDir = "/sb";
    spec.maxLocalAttempts = maxLocal;
    spec.ask = ask;
    s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
    return s;
}

int main()
{
    { // plan validation
        CHECK(Plan::fromJson(makePlan().toJson()).has_value());
        Plan dup = makePlan(); dup.items[2].files = {"a.hpp"};
        CHECK(!Plan::fromJson(dup.toJson()).has_value());                       // overlapping editable files
        Plan cyc = makePlan(); cyc.items[0].deps = {"b"};
        CHECK(!Plan::fromJson(cyc.toJson()).has_value());                       // cycle
        Plan unk = makePlan(); unk.items[1].deps = {"zzz"};
        CHECK(!Plan::fromJson(unk.toJson()).has_value());                       // unknown dependency
        Plan esc = makePlan(); esc.items[0].files = {"../evil.hpp"};
        CHECK(!Plan::fromJson(esc.toJson()).has_value());                       // path escape
        Plan abs = makePlan(); abs.items[0].files = {"/etc/passwd"};
        CHECK(!Plan::fromJson(abs.toJson()).has_value());
        CHECK(!Plan::fromJson("{\"schema\":\"plan-1\",\"items\":[]}").has_value());
    }
    { // plan-2: the blueprint is valid; personas, cards and gates are enforced
        const std::string bp = blueprintJson("calculator-web");
        CHECK(Plan::fromJson(bp).has_value());
        CHECK(!Plan::fromJson(bp, 50).has_value());                                  // cards over the token budget are rejected
        auto swap = [&](const std::string& from, const std::string& to) { std::string t = bp; auto p = t.find(from); if (p != std::string::npos) t.replace(p, from.size(), to); return t; };
        CHECK(!Plan::fromJson(swap("\"files\": [\"tests/eval.test.mjs\"]", "\"files\": [\"src/x.js\"]")).has_value());          // a tester writes only tests/
        CHECK(!Plan::fromJson(swap("\"files\": [\"src/calc.js\"]", "\"files\": [\"tests/y.test.mjs\"]")).has_value());          // a developer never writes tests
        CHECK(!Plan::fromJson(swap("\"persona\": \"developer\"", "\"persona\": \"wizard\"")).has_value());
        CHECK(!Plan::fromJson(swap("\"kind\": \"node-test\"", "\"kind\": \"shell\"")).has_value());                           // a plan cannot carry commands
        CHECK(!Plan::fromJson(swap("\"harness\": \"web-static\"", "\"harness\": \"mystery\"")).has_value());
        auto plan = Plan::fromJson(bp);
        CHECK(plan && plan->items.size() == 4 && plan->find("engine")->card.include.size() == 1);
        // Triage: the first call.
        const TriageResult web = triage("write a scientific calculator as a web page");
        CHECK(web.blueprint == "calculator-web" && web.architect == "not needed" && web.split && web.projectType == "web-static");
        const TriageResult basic = triage("make a calculator");
        CHECK(basic.blueprint == "calculator-basic" && !basic.split);
        const TriageResult unknown = triage("build a unit converter page with length and mass");
        CHECK(unknown.blueprint.empty() && unknown.architect == "deferred" && unknown.projectType == "web-static");
        CHECK(needsPlanPacket("x", unknown, "w-1").find("plan-2 syntax") != std::string::npos);
        // Engine profiles: parse, round trip, loopback only.
        auto e = EngineProfile::parse("q=http://127.0.0.1:8342/v1,model=m,ctx=8192,slots=2");
        CHECK(e && e->model == "m" && e->contextTokens == 8192 && e->maxConcurrent == 2);
        CHECK(e && EngineProfile::parse(e->toArg()).has_value());
        auto seeded = EngineProfile::parse("q=http://127.0.0.1:8342/v1,model=m,slots=1,seed=42,cache=0");  // a deterministic profile
        CHECK(seeded && seeded->seed == 42 && !seeded->cachePrompt && e->seed == -1 && e->cachePrompt);
        auto again = seeded ? EngineProfile::parse(seeded->toArg()) : std::unexpected(std::string{"x"});
        CHECK(again && again->seed == 42 && !again->cachePrompt);
    }
    { // happy path: planner, dependency order, gates, integrator
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
        Sim s = startSim(2, FrontierMode::stub);
        s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 1}}; return p; }()));
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
        Sim s = startSim(2, FrontierMode::off);
        s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 1}}; return p; }()));
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
    { // divide and conquer: after the local attempts the model decides the division; the item is replaced by smaller items
        Sim s = startSim(2, FrontierMode::stub);
        Plan base;
        base.task = "t";
        base.items = {{"t", "Tests", "g", {"tests/t.test.mjs"}, {}, 1, "tester"}, {"a", "Work", "g", {"a.js", "b.js"}, {"t"}, 3, "developer"}, {"z", "After", "g", {"z.js"}, {"a"}, 1, "developer"}};
        s.feed("workflowPlanned", wfbody::planned("w-000001", base));
        s.fragment.task = "frag";
        s.fragment.items = {{"x", "Part x", "g", {"a.js"}, {}, 1, "developer"}, {"y", "Part y", "g", {"b.js"}, {"x"}, 1, "developer"}};
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
        CHECK(merged.find("a") == nullptr && merged.find("a.x") && merged.find("a.y"));
        CHECK(merged.find("a.x")->deps == std::vector<std::string>{"t"});        // roots inherit the parent's dependencies
        CHECK(merged.find("a.y")->deps == std::vector<std::string>{"a.x"});
        CHECK(merged.find("z")->deps == std::vector<std::string>{"a.y"});        // dependants now wait for the leaves
        acts = s.step();                                                         // the first child starts at once
        CHECK(acts.size() == 1 && acts[0].job.item == "a.x" && acts[0].job.role == "agent" && acts[0].job.attempt == 1);
        // an invalid division is rejected, counts as a failed attempt, and the ladder moves on: send back to the producer
        Sim r = startSim(2, FrontierMode::stub);
        r.feed("workflowPlanned", wfbody::planned("w-000001", base));
        r.fragment.items = {{"x", "Part x", "g", {"a.js"}, {}, 1, "developer"}, {"y", "Part y", "g", {"c.js"}, {"x"}, 1, "developer"}};  // does not cover b.js
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
        for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::writeDispute && a.item == "t" && a.fromItem == "a") dispute = true;
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
    { RulePolicy policy; // Near pass: a gate failure with few failing tests is repaired (extra local attempts), not divided; a bad failure still climbs the ladder.
        DecisionContext c; c.workflow = "w"; c.item = "engine"; c.attempt = 2; c.localAttempts = 2; c.failedRole = "gate"; c.splittable = true; c.nearPass = true;
        c.totalAttempts = 2;
        CHECK(policy.decide(c).action == Action::retry);
        c.localAttempts = 4;                       // the repair allowance is used up
        CHECK(policy.decide(c).action == Action::decompose);
        c.localAttempts = 2; c.nearPass = false;   // not a near pass: the ladder starts at decompose
        CHECK(policy.decide(c).action == Action::decompose);
    }
    { RulePolicy policy; // Plan phase: an oversize verdict divides at once, before any local retry; at the depth limit it does not.
        DecisionContext c; c.workflow = "w"; c.item = "engine"; c.attempt = 1; c.localAttempts = 1; c.failedRole = "agent"; c.splittable = true; c.oversize = true; c.totalAttempts = 1;
        CHECK(policy.decide(c).action == Action::decompose);
        c.depth = c.maxDepth;
        CHECK(policy.decide(c).action == Action::retry);
        c.depth = 0; c.decomposeTried = true;
        CHECK(policy.decide(c).action == Action::retry);
    }
    { RulePolicy policy; // Repeated identical failure: the tests are sent back at once, once; afterwards the ordinary ladder applies.
        DecisionContext c; c.workflow = "w"; c.item = "lexer"; c.attempt = 2; c.localAttempts = 2; c.failedRole = "gate"; c.hasTesterDep = true; c.repeatedFailure = true; c.nearPass = true; c.totalAttempts = 2;
        CHECK(policy.decide(c).action == Action::reviseTests);
        c.reviseTried = true;
        CHECK(policy.decide(c).action != Action::reviseTests);
    }
    { RulePolicy policy; // The frontier stub is only used when it has a reference; a repeated failure disputes the tests only when it is a near pass.
        DecisionContext c; c.workflow = "w"; c.item = "lexer"; c.attempt = 6; c.localAttempts = 2; c.failedRole = "gate"; c.totalAttempts = 6;
        c.decomposeTried = c.reviseTried = c.rearchitectTried = true; c.splittable = true;
        CHECK(policy.decide(c).action == Action::escalate);
        c.frontierHasReference = false;
        CHECK(policy.decide(c).action == Action::fail);
        DecisionContext r; r.workflow = "w"; r.item = "ui-js"; r.attempt = 2; r.localAttempts = 2; r.failedRole = "gate"; r.hasTesterDep = true; r.splittable = true; r.totalAttempts = 2;
        r.repeatedFailure = false;  // 3 of 7 failing twice is not a near pass, so the gate does not flag it: the ordinary ladder applies
        CHECK(policy.decide(r).action == Action::decompose);
    }
    { // The V's ascent: a coordinator integrates its parts, a failed round is sent down to a part, the part is redone, the coordinator integrates again.
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
    { // A division on a web project keeps the parent as a coordinator, and refuses parts graded by the parent's own tests.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        WorkItem tt; tt.id = "tt"; tt.persona = "tester"; tt.files = {"tests/e.test.mjs"}; tt.gate = gate("tests/e.test.mjs", true);
        WorkItem eng; eng.id = "eng"; eng.persona = "developer"; eng.files = {"src/e.js", "src/f.js"}; eng.deps = {"tt"}; eng.complexity = 3; eng.gate = gate("tests/e.test.mjs", false);
        WorkItem after; after.id = "after"; after.persona = "developer"; after.files = {"src/g.js"}; after.deps = {"eng"}; after.gate = gate("tests/g.test.mjs", false);
        WorkItem gt; gt.id = "gt"; gt.persona = "tester"; gt.files = {"tests/g.test.mjs"}; gt.gate = gate("tests/g.test.mjs", true);
        after.deps.push_back("gt");
        base.items = {tt, eng, gt, after};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem ft; ft.id = "t-f"; ft.persona = "tester"; ft.files = {"tests/f.test.mjs"}; ft.gate = gate("tests/f.test.mjs", true);
        WorkItem fe; fe.id = "d-e"; fe.persona = "developer"; fe.files = {"src/e.js"}; fe.gate = gate("tests/e.test.mjs", false);   // graded by the parent's tests
        WorkItem ff; ff.id = "d-f"; ff.persona = "developer"; ff.files = {"src/f.js"}; ff.deps = {"t-f"}; ff.gate = gate("tests/f.test.mjs", false);
        frag.items = {ft, fe, ff};
        CHECK(!Plan::amended(base, "eng", frag).has_value());                                              // d-e is graded by tests/e.test.mjs, the parent's
        frag.items[1].deps = {"t-f"}; frag.items[1].gate = gate("tests/f.test.mjs", false);
        auto merged = Plan::amended(base, "eng", frag);
        CHECK(merged.has_value());
        const WorkItem* coord = merged ? merged->find("eng") : nullptr;
        CHECK(coord && coord->persona == "integrator" && coord->files.empty() && coord->deps.size() == 2);   // the parent stays, waiting for both parts
        const WorkItem* dependent = merged ? merged->find("after") : nullptr;
        CHECK(dependent && std::find(dependent->deps.begin(), dependent->deps.end(), std::string{"eng"}) != dependent->deps.end());   // dependents still wait for it
    }
    { // Requests: a blocking request holds only the nodes in its scope; answering releases them.
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
        for (const char* resolution : {"retry", "stop"}) {
            Sim s = startSim(2, FrontierMode::off, true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 1}}; return p; }()));
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
    { // Split for parallelism: the size budget follows the engine's idle capacity.
        CHECK_EQ(effectiveSizeBudget(1800, 7, 8), 900);   // plenty idle: smaller parts that can run side by side
        CHECK_EQ(effectiveSizeBudget(1800, 3, 6), 900);
        CHECK_EQ(effectiveSizeBudget(1800, 4, 8), 900);
        CHECK_EQ(effectiveSizeBudget(1800, 2, 8), 1800);  // some spare capacity: as configured
        CHECK_EQ(effectiveSizeBudget(1800, 1, 8), 2700);  // nearly busy: a split would only queue
        CHECK_EQ(effectiveSizeBudget(1800, 0, 8), 2700);
        CHECK_EQ(effectiveSizeBudget(600, 8, 8), 400);    // never below 400
        CHECK_EQ(effectiveSizeBudget(1800, 7, 1), 1800);  // one slot: nothing to parallelise
        CHECK_EQ(effectiveSizeBudget(1800, -1, 8), 1800); // unknown state
        CHECK_EQ(effectiveSizeBudget(0, 8, 8), 0);        // size splitting off stays off
    }
    { // The overseer, first responder to an escalation: guidance for mechanical causes, fatal for the environment, the owner otherwise.
        OverseerInput load; load.persona = "tester";
        load.gates = {"GATE FAIL: only 1 test(s) ran, at least 6 are required\nFAIL 1 - tests/page.test.mjs: 'test failed'\nThe runner reported (a test file that fails while loading counts as one failed test):\n  Error: ENOENT: no such file or directory, open '/ws/style.css'\n"};
        const auto v1 = analyseEscalation(load);
        CHECK(v1.triage == "guidance" && v1.guidance.find("top level") != std::string::npos && v1.analysis.find("ENOENT") != std::string::npos);
        OverseerInput stub; stub.persona = "tester"; stub.gates = {"GATE FAIL: only 18 of 27 tests fail against the stub: they do not exercise the contract"};
        CHECK(analyseEscalation(stub).triage == "guidance");
        OverseerInput env; env.persona = "developer"; env.agentNote = "llm-agent: engine call failed: connect to 127.0.0.1:8092: Connection refused";
        const auto v3 = analyseEscalation(env);
        CHECK(v3.triage == "fatal" && v3.guidance.empty());
        OverseerInput same; same.persona = "developer";
        same.gates = {"GATE FAIL: 2 of 20 tests failed\nFAIL 3 - rounds: 'expected 1, got 2'\n", "GATE FAIL: 2 of 20 tests failed\nFAIL 3 - rounds: 'expected 1, got 2'\n"};
        const auto v4 = analyseEscalation(same);
        CHECK(v4.triage == "owner" && v4.analysis.find("same tests failed") != std::string::npos);
        OverseerInput none; none.persona = "developer"; none.gates = {"GATE FAIL: 1 of 9 tests failed\nFAIL 2 - x: 'y'\n"};
        CHECK(analyseEscalation(none).triage == "owner");
    }
    { // The harness-expert's briefing carries the situation, the harness facts, the evidence, the history and the overseer's verdict.
        BriefingInput b; b.workflow = "w-1"; b.request = "r-3"; b.kind = "escalation"; b.title = "'lexer' could not be completed"; b.task = "build a calculator";
        b.item = "lexer"; b.persona = "developer"; b.itemTitle = "Lexer"; b.goal = "tokenise"; b.files = {"src/lexer.js"}; b.contract = "tokenize(text) returns tokens";
        b.harnessFacts = webHarnessFacts(); b.gates = {"GATE FAIL: 1 of 10 tests failed"}; b.history = {"attempt 1: retry (local)", "attempt 2: reviseTests (local)"};
        b.triage = "owner"; b.analysis = "needs a decision"; b.options = {"retry with my guidance", "stop the workflow"};
        const std::string md = buildBriefing(b);
        for (const char* part : {"# Briefing for r-3", "build a calculator", "src/lexer.js", "never throw", "70 %", "GATE FAIL: 1 of 10", "attempt 2: reviseTests", "Class: **owner**", "retry with my guidance"})
            CHECK(md.find(part) != std::string::npos);
    }
    return finish();
}
