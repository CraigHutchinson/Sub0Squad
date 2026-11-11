// The workflow engine as a pure function: planning, dependency order, gates, retry, escalation, failure, cancel, idempotence.
#include "check.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
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
                case WorkflowAction::Kind::setStrategy: break;
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
    { // failure detail: expected and actual values reach the worker
        const std::string tap = "TAP version 13\nnot ok 1 - adds\n  ---\n  location: '/x/a.test.mjs:2:1'\n  error: |-\n    Expected values to be strictly equal:\n  code: 'ERR_ASSERTION'\n  expected: 3\n  actual: 2\n  ...\n# tests 1\n# pass 0\n# fail 1\n";
        const std::string ex = webFailureExcerpt(tap);
        CHECK(ex.find("FAIL 1 - adds") != std::string::npos || ex.find("FAIL adds") != std::string::npos);
        CHECK(ex.find("expected 3 but the code gave 2") != std::string::npos);
        CHECK(ex.find("a.test.mjs:2:1") != std::string::npos);
    }

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
        CHECK(plan && plan->items.size() == 4 && plan->find("dev-engine")->card.include.size() == 1);
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
    { RulePolicy policy; // Interface mismatch (gate exit 6): go up a scope at once (the card is rewritten to name every export), not another leaf attempt; once only.
        DecisionContext c; c.workflow = "w"; c.item = "functions"; c.attempt = 1; c.localAttempts = 1; c.failedRole = "gate"; c.hasTesterDep = true; c.interfaceGap = true; c.totalAttempts = 1;
        CHECK(policy.decide(c).action == Action::rearchitect);
        c.rearchitectTried = true;
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
    { // Divide only when it pays: over the ceiling always, under the soft budget never, in between only when the saving beats the division's cost.
        CHECK(shouldDivide(3000, 1800, 2800, 7, 8));                 // cannot fit one attempt: divide whatever it costs
        CHECK(!shouldDivide(1500, 1800, 2800, 7, 8));                // under the soft budget: never
        CHECK(!shouldDivide(2400, 1800, 2800, 7, 8));                // 2400 x (1 - 1/4) = 1800 saved, less than the 2700 the division costs
        CHECK(shouldDivide(2400, 1800, 2800, 7, 8, 1000));           // a fast engine (a cheap division): worth it
        CHECK(!shouldDivide(2400, 1800, 2800, 0, 8, 1000));          // no spare slots: nothing runs beside it, so nothing is saved
        CHECK(!shouldDivide(2400, 1800, 2800, -1, 0, 1000));         // unknown engine state counts as no spare capacity
        CHECK(!shouldDivide(9000, 0, 2800, 8, 8));                   // size splitting off (budget 0) stays off
    }
    { // Slot affinity: a node's retry returns to its earlier slot when it is free; otherwise the lowest free slot.
        CHECK_EQ(chooseLeaseSlot(8, {}, -1), 0);
        CHECK_EQ(chooseLeaseSlot(8, {0, 1}, -1), 2);
        CHECK_EQ(chooseLeaseSlot(8, {0, 1, 5}, 5), 2);   // preferred is taken: lowest free
        CHECK_EQ(chooseLeaseSlot(8, {0, 1}, 5), 5);      // preferred is free: kept, so the prompt cache hits
        CHECK_EQ(chooseLeaseSlot(4, {0}, 9), 1);         // preferred beyond the cap is ignored
    }
    { // The manager solves before anyone is asked: a spent ladder applies strategies in order (think, then a stronger engine when configured), each once;
      // only then does the item fail (or, with --ask, go to the owner). The stronger engine is what the next attempt's job is built with.
        for (const bool withFallback : {false, true}) {
            Sim s;
            s.settings.frontier = FrontierMode::off;
            WorkflowSpec spec;
            spec.task = "t"; spec.sandboxDir = "/sb"; spec.maxLocalAttempts = 1; spec.agent = "llm";
            spec.engines = {"small=http://127.0.0.1:8091/v1,model=m,slots=2", "big=http://127.0.0.1:8092/v1,model=m,slots=2"};
            if (withFallback) spec.fallbackEngine = "big";
            s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
            s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 1}}; return p; }()));
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
    { // Edit replies: applied to the earlier files, exactly once each; every failure names the edit; the other files are untouched.
        const std::map<std::string, std::string> files{{"src/a.js", "function add(a, b) {\n  return a - b;\n}\nexport { add };\n"}, {"src/b.js", "export const x = 1;\n"}};
        const std::set<std::string> allowed{"src/a.js", "src/b.js"};
        std::string err;
        auto ok = applyEdits("Here is the fix.\n=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  return a - b;\n=======\n  return a + b;\n>>>>>>> REPLACE\n=== END EDIT ===\n", files, allowed, err);
        CHECK(ok && ok->at("src/a.js") == "function add(a, b) {\n  return a + b;\n}\nexport { add };\n" && ok->at("src/b.js") == "export const x = 1;\n");
        auto two = applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\nfunction add(a, b)\n=======\nfunction add(a, b = 0)\n>>>>>>> REPLACE\n<<<<<<< SEARCH\n  return a - b;\n=======\n  return a + b;\n>>>>>>> REPLACE\n=== END EDIT ===\n=== EDIT: src/b.js ===\n<<<<<<< SEARCH\nconst x = 1;\n=======\nconst x = 2;\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err);
        CHECK(two && two->at("src/a.js").find("b = 0") != std::string::npos && two->at("src/a.js").find("a + b") != std::string::npos && two->at("src/b.js") == "export const x = 2;\n");
        {   // Folded views: structure stays, function bodies fold, focus regions open, READ requests are answered.
            std::string cpp = "#include <string>\nnamespace n {\nclass A {\npublic:\n  int get() const { return v; }\n  int v = 0;\n};\ndouble f(double x)\n{\n  double y = x;\n  if (y > 1) {\n    y = 2;\n  }\n  y += 1;\n  return y;\n}\nint g(int a) { return a; }\n}\n";
            const auto regions = foldRegions(cpp, false);
            CHECK(regions.size() == 1 && regions[0].name == "f" && regions[0].first == 8 && regions[0].bodyFrom == 10 && regions[0].bodyTo == 16);   // g and get are one line: nothing to fold; namespace and class stay open
            const std::string small = foldedView(cpp, false, {}, 100000);
            CHECK(small.find("folded") == std::string::npos);                                  // fits: shown whole
            std::string big = "namespace n {\nclass A {\n  int v = 0;\n};\n";   // two functions of 40 body lines each, so a budget can force folding
            for (const char* fn : {"first", "second"}) { big += std::string("double ") + fn + "(double x)\n{\n"; for (int k = 0; k < 40; ++k) big += "  x += " + std::to_string(k) + ";\n"; big += "  return x;\n}\n"; }
            big += "}\n";
            const std::string tight = foldedView(big, false, {}, 700);
            CHECK(tight.find("lines folded: 7-47") != std::string::npos && tight.find("double first(double x)") != std::string::npos && tight.find("class A {") != std::string::npos && tight.find("x += 20;") == std::string::npos);
            const std::string open = foldedView(big, false, {70}, 1300);   // a failure at line 70 (inside `second`) keeps that function open, `first` stays folded
            CHECK(open.find("lines folded: 7-47") != std::string::npos && open.find("lines folded: 51-") == std::string::npos && open.find("x += 20;") != std::string::npos);
            const auto reqs = parseReadRequests("Let me look.\n=== READ: src/a.cpp LINES 9-12 ===\n=== READ: src/a.cpp SYMBOL f ===\n");
            CHECK(reqs.size() == 2 && reqs[0].from == 9 && reqs[0].to == 12 && reqs[1].symbol == "f");
            const std::string ans = answerRead(reqs[0], cpp, false);
            CHECK(ans.find("   9| {") != std::string::npos && ans.find("  12| ") != std::string::npos && ans.find("  13| ") == std::string::npos);
            CHECK(answerRead(reqs[1], cpp, false).find("double f(double x)") != std::string::npos);
            CHECK(answerRead({"x.cpp", 99, 100, ""}, cpp, false).find("do not exist") != std::string::npos);
            const std::string py = "import math\n\ndef a(x):\n    y = x\n    return y\n\ndef b():\n    pass\n";
            const auto pr = foldRegions(py, true);
            CHECK(pr.size() == 1 && pr[0].name == "a" && pr[0].first == 3 && pr[0].bodyTo == 5);
        }
        {   // The canvas: transactions against a revision, all or nothing, rebased through earlier edits, conflicts named, any revision restorable.
            Canvas cv;
            cv.addFile("a.cpp", "l1\nl2\nl3\nl4\nl5\nl6\n");
            CHECK(cv.revision("a.cpp") == 1);
            auto ok = cv.apply({"a.cpp", 1, "w1", {{CanvasOp::Kind::replaceLines, 2, 3, "", "X\nY\nZ"}}});    // lines 2-3 become three lines: +1
            CHECK(ok.ok && ok.revision == 2 && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nl5\nl6\n");
            auto stale = cv.apply({"a.cpp", 1, "w2", {{CanvasOp::Kind::replaceLines, 5, 5, "", "FIVE"}}});    // made against revision 1: line 5 is now line 6
            CHECK(stale.ok && stale.revision == 3 && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nFIVE\nl6\n");
            auto clash = cv.apply({"a.cpp", 1, "w3", {{CanvasOp::Kind::replaceLines, 3, 3, "", "no"}}});       // line 3 was changed by w1 in revision 2
            CHECK(!clash.ok && clash.error.find("w1") != std::string::npos && clash.error.find("revision 2") != std::string::npos && clash.revision == 3);
            auto bad = cv.apply({"a.cpp", 3, "w3", {{CanvasOp::Kind::replaceLines, 1, 1, "", "ONE"}, {CanvasOp::Kind::replaceLines, 99, 99, "", "x"}}});   // the second op is invalid: nothing applies
            CHECK(!bad.ok && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nFIVE\nl6\n" && cv.revision("a.cpp") == 3);
            auto multi = cv.apply({"a.cpp", 3, "w3", {{CanvasOp::Kind::replaceLines, 1, 1, "", "ONE"}, {CanvasOp::Kind::insertAfter, 7, 0, "", "tail"}}});   // numbering of the base revision
            CHECK(multi.ok && cv.text("a.cpp") == "ONE\nX\nY\nZ\nl4\nFIVE\nl6\ntail\n");
            auto overlap = cv.apply({"a.cpp", 4, "w3", {{CanvasOp::Kind::replaceLines, 2, 4, "", "a"}, {CanvasOp::Kind::replaceLines, 4, 5, "", "b"}}});
            CHECK(!overlap.ok && overlap.error.find("overlap") != std::string::npos);
            CHECK(cv.textAt("a.cpp", 1) == "l1\nl2\nl3\nl4\nl5\nl6\n");
            auto rv = cv.revertTo("a.cpp", 1, "owner");
            CHECK(rv.ok && rv.revision == 5 && cv.text("a.cpp") == cv.textAt("a.cpp", 1));
            CHECK(!cv.apply({"a.cpp", 4, "w4", {{CanvasOp::Kind::replaceLines, 1, 1, "", "q"}}}).ok);   // a revert touched everything: older edits conflict
            // Replace a function by name: no line arithmetic.
            cv.addFile("f.cpp", "int a() { return 1; }\ndouble f(double x)\n{\n  x += 1;\n  x += 2;\n  return x;\n}\nint z;\n");
            auto sym = cv.apply({"f.cpp", 1, "w5", {{CanvasOp::Kind::replaceSymbol, 0, 0, "f", "double f(double x) { return x; }"}}});
            CHECK(sym.ok && cv.text("f.cpp") == "int a() { return 1; }\ndouble f(double x) { return x; }\nint z;\n");
            CHECK(!cv.apply({"f.cpp", 2, "w5", {{CanvasOp::Kind::replaceSymbol, 0, 0, "nope", "x"}}}).ok);
        }
        {   // Line edits: the worker names lines of the numbered draft it was shown; several edits apply together against that numbering.
            std::map<std::string, std::string> f2 = {{"src/a.js", "one\ntwo\nthree\nfour\nfive\n"}};
            std::set<std::string> al = {"src/a.js"};
            auto one = applyEdits("=== EDIT: src/a.js LINES 2 ===\nTWO\n=== END EDIT ===", f2, al, err);
            CHECK(one && one->at("src/a.js") == "one\nTWO\nthree\nfour\nfive\n");
            auto range = applyEdits("=== EDIT: src/a.js LINES 2-3 ===\nX\nY\nZ\n=== END EDIT ===", f2, al, err);
            CHECK(range && range->at("src/a.js") == "one\nX\nY\nZ\nfour\nfive\n");
            auto del = applyEdits("=== EDIT: src/a.js LINES 4-5 ===\n=== END EDIT ===", f2, al, err);
            CHECK(del && del->at("src/a.js") == "one\ntwo\nthree\n");
            auto ins = applyEdits("=== EDIT: src/a.js AFTER 1 ===\nnew\n=== END EDIT ===", f2, al, err);
            CHECK(ins && ins->at("src/a.js") == "one\nnew\ntwo\nthree\nfour\nfive\n");
            auto both = applyEdits("=== EDIT: src/a.js LINES 1 ===\nONE\n=== END EDIT ===\n=== EDIT: src/a.js LINES 5 ===\nFIVE\n=== END EDIT ===", f2, al, err);   // numbering of the draft, not of the result
            CHECK(both && both->at("src/a.js") == "ONE\ntwo\nthree\nfour\nFIVE\n");
            CHECK(!applyEdits("=== EDIT: src/a.js LINES 9 ===\nx\n=== END EDIT ===", f2, al, err) && err.find("5 lines") != std::string::npos);
            CHECK(!applyEdits("=== EDIT: src/a.js LINES 2 ===\nx\n=== END EDIT ===\n=== EDIT: src/a.js ===\n<<<<<<< SEARCH\none\n=======\n1\n>>>>>>> REPLACE\n=== END EDIT ===", f2, al, err) && err.find("one kind per file") != std::string::npos);
            // A SEARCH copied with the numbered view's "N| " column still matches.
            auto numbered = applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n   2| two\n=======\n2\n>>>>>>> REPLACE\n=== END EDIT ===", f2, al, err);
            CHECK(numbered && numbered->at("src/a.js") == "one\n2\nthree\nfour\nfive\n");
        }
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  return a * b;\n=======\n  return 0;\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err) && err.find("does not appear") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  \n=======\n  x\n>>>>>>> REPLACE\n=== END EDIT ===", {{"src/a.js", "  a\n  b\n"}}, allowed, err) && err.find("more than once") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: ../evil.js ===\n<<<<<<< SEARCH\nx\n=======\ny\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err) && err.find("not one of the files") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\nfoo\n=======\nbar\n=== END EDIT ===", files, allowed, err) && err.find("not closed") != std::string::npos);
        CHECK(!applyEdits("no edits here", files, allowed, err) && err.find("no edit blocks") != std::string::npos);
    }
    { // The division the 9B wrote three times: tests for the lexer, parts for the functions and the parser, no part for src/lexer.js. The harness completes it.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 3; g.mustFailOnStub = stub; return g; };
        WorkItem et; et.id = "eval-tests"; et.persona = "tester"; et.files = {"tests/eval.test.mjs"}; et.gate = gate("tests/eval.test.mjs", true);
        WorkItem eng; eng.id = "engine"; eng.persona = "developer"; eng.files = {"src/calc.js", "src/lexer.js", "src/functions.js"}; eng.deps = {"eval-tests"}; eng.complexity = 3; eng.gate = gate("tests/eval.test.mjs", false);
        base.items = {et, eng};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem lx; lx.id = "lexer"; lx.persona = "tester"; lx.files = {"tests/lexer.test.mjs"}; lx.gate = gate("tests/lexer.test.mjs", true);
        WorkItem fn; fn.id = "functions"; fn.persona = "developer"; fn.files = {"src/functions.js"}; fn.deps = {"lexer"}; fn.gate = gate("tests/lexer.test.mjs", false);
        WorkItem ps; ps.id = "parser"; ps.persona = "developer"; ps.files = {"src/calc.js"}; ps.deps = {"lexer", "functions"}; ps.gate = gate("tests/lexer.test.mjs", false);
        frag.items = {lx, fn, ps};
        CHECK(!Plan::amended(base, "engine", frag).has_value());                       // as written: src/lexer.js is delivered by no part
        const auto added = completeDivision(frag, *base.find("engine"));
        CHECK(added.size() == 1 && added[0].find("src/lexer.js") != std::string::npos);
        const WorkItem* impl = frag.find("dev-lexer");
        CHECK(impl && impl->persona == "developer" && impl->files == std::vector<std::string>{"src/lexer.js"} && impl->deps == std::vector<std::string>{"lexer"} && impl->gate.tests == std::vector<std::string>{"tests/lexer.test.mjs"});
        CHECK(std::find(frag.find("parser")->deps.begin(), frag.find("parser")->deps.end(), "dev-lexer") != frag.find("parser")->deps.end());   // the parser waits for the lexer's implementation
        CHECK(Plan::fromJson(frag.toJson()).has_value());
        CHECK(completeDivision(frag, *base.find("engine")).empty());                  // nothing left to add
    }
    { // The 'ui-js' division of the first real 9B run: a developer part graded by tests/ui-dom.test.mjs, which no item writes. It gets a tester.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 3; g.mustFailOnStub = stub; return g; };
        WorkItem ut; ut.id = "ui-tests"; ut.persona = "tester"; ut.files = {"tests/ui.test.mjs"}; ut.gate = gate("tests/ui.test.mjs", true);
        WorkItem js; js.id = "ui-js"; js.persona = "developer"; js.files = {"src/ui.js"}; js.deps = {"ui-tests"}; js.gate = gate("tests/ui.test.mjs", false);
        base.items = {ut, js};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem ev; ev.id = "ui-eval"; ev.persona = "tester"; ev.files = {"tests/ui-eval.test.mjs"}; ev.gate = gate("tests/ui-eval.test.mjs", true);
        WorkItem dom; dom.id = "ui-dom"; dom.persona = "developer"; dom.files = {"src/ui.js"}; dom.deps = {"ui-eval"}; dom.gate = gate("tests/ui-dom.test.mjs", false);
        frag.items = {ev, dom};
        const auto added = completeDivision(frag, *base.find("ui-js"));
        CHECK(added.size() == 1 && added[0].find("tests/ui-dom.test.mjs") != std::string::npos);
        const WorkItem* t = frag.find("test-ui-dom");
        CHECK(t && t->persona == "tester" && t->files == std::vector<std::string>{"tests/ui-dom.test.mjs"} && t->gate.mustFailOnStub);
        CHECK(std::find(frag.find("ui-dom")->deps.begin(), frag.find("ui-dom")->deps.end(), "test-ui-dom") != frag.find("ui-dom")->deps.end());
        CHECK(completeDivision(frag, *base.find("ui-js")).empty());
    }
    { // QA: two modules that each wrote the same helper and the same block; a long function; tests and node_modules are left alone.
        const auto root = std::filesystem::current_path() / "qa-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src");
        std::filesystem::create_directories(root / "tests");
        const std::string shared = "function isDigit(c) {\n  return c >= '0' && c <= '9';\n}\nfunction readNumber(text, start) {\n  let end = start;\n  while (end < text.length && isDigit(text[end])) end += 1;\n  const value = Number(text.slice(start, end));\n  return { value, end };\n}\n";
        { std::ofstream o(root / "src" / "lexer.js"); o << shared << "export function tokenize(text) {\n  return [];\n}\n"; }
        { std::ofstream o(root / "src" / "functions.js"); o << "export function parseArg(text) {\n  return null;\n}\n" << shared; }
        { std::ofstream o(root / "src" / "big.js"); o << "export function big() {\n"; for (int i = 0; i < 45; ++i) o << "  const v" << i << " = compute(" << i << ", 'distinct text number " << i << "');\n"; o << "  return 0;\n}\n"; }
        { std::ofstream o(root / "tests" / "dup.test.mjs"); o << shared; }
        const auto f = qaScan(root);
        bool dupHelper = false, dupBlock = false, longFn = false, testsFlagged = false;
        for (const auto& x : f) {
            if (x.axis == "duplication" && x.what.find("'isDigit' is declared in 2 files") != std::string::npos) dupHelper = true;
            if (x.axis == "duplication" && x.what.find("lines appear twice") != std::string::npos) dupBlock = true;
            if (x.axis == "simplicity" && x.what.find("'big' is 48 lines long") != std::string::npos) longFn = true;
            for (const auto& w : x.where) if (w.find("tests/") != std::string::npos) testsFlagged = true;
        }
        CHECK(dupHelper && dupBlock && longFn && !testsFlagged);
        std::filesystem::remove_all(root);
    }
    { // Tolerant plan reading (what the 9B wrote: notes as one string) and an error that says where a type is wrong.
        const std::string base = "{\"schema\":\"plan-2\",\"task\":\"x\",\"harness\":\"web-static\",\"items\":[{\"id\":\"a\",\"title\":\"A\",\"persona\":\"developer\",\"goal\":\"g\",\"files\":%FILES%,\"deps\":[],\"complexity\":%CX%,"
                                  "\"card\":{\"context\":\"c\",\"contract\":\"k\",\"acceptance\":[\"ok\"],\"notes\":%NOTES%},\"gate\":{\"kind\":\"node-test\",\"tests\":%TESTS%,\"minTests\":%MIN%,\"mustFailOnStub\":%STUB%}}]}";
        const auto make = [&](const char* files, const char* cx, const char* notes, const char* tests, const char* mn, const char* stub) {
            std::string t = base;
            for (const auto& [k2, v] : std::vector<std::pair<std::string, std::string>>{{"%FILES%", files}, {"%CX%", cx}, {"%NOTES%", notes}, {"%TESTS%", tests}, {"%MIN%", mn}, {"%STUB%", stub}})
                t.replace(t.find(k2), k2.size(), v);
            return t;
        };
        CHECK(Plan::fromJson(make("[\"src/a.js\"]", "2", "[\"n\"]", "[\"tests/a.test.mjs\"]", "3", "false")).has_value());                       // the proper form
        const auto loose = Plan::fromJson(make("\"src/a.js\"", "\"2\"", "\"Output only src/a.js\"", "\"tests/a.test.mjs\"", "\"3\"", "\"false\""));   // strings where lists, numbers and booleans are wanted
        CHECK(loose.has_value() && loose->items[0].card.notes.size() == 1 && loose->items[0].files.size() == 1 && loose->items[0].complexity == 2 && loose->items[0].gate.minTests == 3);
        const auto bad = Plan::fromJson(make("{\"x\":1}", "2", "[\"n\"]", "[\"t\"]", "3", "false"));                                      // an object where a list is wanted: not repairable, but it names the field
        CHECK(!bad.has_value() && bad.error().find("item #1") != std::string::npos && bad.error().find("files") != std::string::npos);
    }
    return finish();
}
