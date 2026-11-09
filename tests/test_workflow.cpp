// The workflow engine as a pure function: planning, dependency order, gates, retry, escalation, failure, cancel, idempotence.
#include "check.hpp"
#include "sub0squad/harness_web.hpp"
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

static Sim startSim(int maxLocal = 2, FrontierMode fm = FrontierMode::stub)
{
    Sim s;
    s.settings.frontier = fm;
    WorkflowSpec spec;
    spec.task = "t";
    spec.sandboxDir = "/sb";
    spec.maxLocalAttempts = maxLocal;
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
    { // retry then escalate then success; decisions are recorded with inputs
        Sim s = startSim(2, FrontierMode::stub);
        s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 3}}; return p; }()));
        auto ids = s.run(s.step());                                              // agent a attempt 1 local
        s.finishJob(ids[0], JobState::succeeded);
        ids = s.run(s.step());                                                   // gate 1
        s.finishJob(ids[0], JobState::failed);
        auto acts = s.step();                                                    // decision retry + agent attempt 2 local
        CHECK(acts.size() == 2 && acts[0].kind == WorkflowAction::Kind::recordDecision && acts[0].decisionAction == "retry");
        CHECK(acts[1].job.attempt == 2 && acts[1].job.tier == "local");
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::failed);                                   // agent 2 fails outright
        acts = s.step();                                                         // local attempts exhausted -> escalate
        CHECK(acts.size() == 2 && acts[0].decisionAction == "escalate" && acts[1].job.tier == "frontier-stub" && acts[1].job.attempt == 3);
        CHECK(acts[0].decisionJson.find("local attempts exhausted") != std::string::npos);
        ids = s.run(acts);
        s.finishJob(ids[0], JobState::succeeded);
        ids = s.run(s.step());
        CHECK(s.job(ids[0])->spec.role == "gate" && s.job(ids[0])->spec.tier == "frontier-stub");
        s.finishJob(ids[0], JobState::succeeded);
        const auto st = itemStatuses(*s.wfs.find("w-000001"), s.jobs);
        CHECK(st.size() == 1 && st[0].status == "accepted" && st[0].attempts == 3 && st[0].tier == "frontier-stub");
    }
    { // frontier off: fail after the local attempts, no frontier job is ever created
        Sim s = startSim(2, FrontierMode::off);
        s.feed("workflowPlanned", wfbody::planned("w-000001", [] { Plan p; p.task = "t"; p.items = {{"a", "A", "g", {"a.hpp"}, {}, 1}}; return p; }()));
        for (int attempt = 1; attempt <= 2; ++attempt) {
            auto ids = s.run(s.step());
            s.finishJob(ids[0], JobState::failed);
        }
        auto acts = s.step();
        bool sawFail = false, sawStart = false;
        for (const auto& a : acts) { if (a.kind == WorkflowAction::Kind::recordDecision && a.decisionAction == "fail") sawFail = true; if (a.kind == WorkflowAction::Kind::startJob) sawStart = true; }
        CHECK(sawFail && !sawStart);
        CHECK(acts.back().kind == WorkflowAction::Kind::finish && acts.back().finalState == WorkflowState::failed);  // nothing active: finishes in the same pass
        s.run(acts);
        CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);
        CHECK(s.step().empty());
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
    return finish();
}
