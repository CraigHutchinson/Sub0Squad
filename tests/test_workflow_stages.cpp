// The workflow engine on processes other than the standard one: other parameters, other stages, the same engine (docs/PROCESS_DESIGN.md section 13).
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
    {   // Processes other than the standard one: the SAME engine, different parameters, different behaviour (docs/PROCESS_DESIGN.md). The goldens above hold the
        // standard process to what the engine did before processes were data; these drive the others.
        using Outcome = std::pair<JobState, std::string>;
        const auto drive = [](Sim& s, const std::function<Outcome(const JobRecord&)>& outcome) {
            for (int guard = 0; guard < 120 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) { const Outcome o = outcome(*s.job(id)); s.finishJob(id, o.first, o.second); }
            }
        };
        const auto count = [](Sim& s, const char* action) { int n = 0; for (const auto& d : s.wfs.find("w-000001")->decisions) if (d.action == action) ++n; return n; };
        const auto gate = [](const char* tests, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {tests}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        // A tester's item and a wide developer's item that depends on it: the plan on which the processes differ.
        const auto widePlan = [&] {
            Plan p;
            p.task = "t"; p.harness = "web-static";
            WorkItem t; t.id = "t"; t.title = "Tests"; t.goal = "g"; t.persona = "tester"; t.files = {"tests/e.test.mjs"}; t.gate = gate("tests/e.test.mjs", true);
            WorkItem a; a.id = "a"; a.title = "Work"; a.goal = "g"; a.files = {"src/e.js", "src/f.js"}; a.deps = {"t"}; a.complexity = 3; a.gate = gate("tests/e.test.mjs", false);
            p.items = {t, a};
            return p;
        };
        // everything succeeds except the developer's gate and the decompose job: the ladder climbs until it is spent
        const auto stuck = [&](const JobRecord& j) { return (j.spec.role == "gate" && j.spec.item == "a") || j.spec.role == "decompose" ? Outcome{JobState::failed, ""} : Outcome{JobState::succeeded, ""}; };
        {   // the standard process divides a stuck wide item into modules; the sections process never does, and starts it as forked sections
            SCENARIO("process-standard-divides");
            Sim s = startSim(2, FrontierMode::off, false, standardProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", widePlan()));
            drive(s, stuck);
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);
            CHECK_GE(count(s, "decompose"), 1);
        }
        {
            SCENARIO("process-sections-never-divides");
            Sim s = startSim(2, FrontierMode::off, false, sectionsProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", widePlan()));
            // t first (a canvas session), then a's FIRST attempt is already the fork executor
            auto acts = s.step();
            CHECK(acts.size() == 1 && acts[0].job.item == "t" && acts[0].job.argv[1] == "llm-agent");
            CHECK(std::find(acts[0].job.argv.begin(), acts[0].job.argv.end(), "canvas") != acts[0].job.argv.end());      // a tester's item runs on the canvas
            s = startSim(2, FrontierMode::off, false, sectionsProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", widePlan()));
            bool devStartedAsFork = false;
            for (int guard = 0; guard < 120 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto step = s.step();
                for (const auto& a : step) if (a.kind == WorkflowAction::Kind::startJob && a.job.item == "a" && a.job.role == "agent" && a.job.attempt == 1) devStartedAsFork = a.job.argv[1] == "llm-fork";
                for (const auto& id : s.run(step)) { const Outcome o = stuck(*s.job(id)); s.finishJob(id, o.first, o.second); }
            }
            CHECK(devStartedAsFork);
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);
            CHECK_EQ(count(s, "decompose"), 0);                                                                         // the rung does not exist in this process
        }
        {   // a process of one's own, by parameters alone: no escalation (the stub tier would otherwise be asked), no card rewrite, and its own wall times
            SCENARIO("process-custom-parameters");
            Process mine = standardProcess();
            mine.name = "mine";
            CHECK(mine.set("build.leaf.allow.escalate", "false").has_value() && mine.set("build.leaf.allow.rearchitect", "false").has_value());
            CHECK(mine.set("build.leaf.wallSeconds.gate", "45").has_value() && mine.set("integrate.job.wallSeconds", "77").has_value() && mine.set("plan.job.wallSeconds", "33").has_value());
            Sim s = startSim(2, FrontierMode::stub, false, mine);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            int gateWall = 0;
            bool sawFrontier = false;
            for (int guard = 0; guard < 40 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto step = s.step();
                for (const auto& a : step) if (a.kind == WorkflowAction::Kind::startJob) { if (a.job.role == "gate") gateWall = static_cast<int>(a.job.wallSeconds); if (a.job.tier == "frontier-stub") sawFrontier = true; }
                for (const auto& id : s.run(step)) s.finishJob(id, s.job(id)->spec.role == "gate" ? JobState::failed : JobState::succeeded);
            }
            CHECK_EQ(gateWall, 45);
            CHECK(!sawFrontier);
            CHECK_EQ(count(s, "escalate"), 0);
            CHECK_EQ(count(s, "rearchitect"), 0);
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::failed);
            // the standard process, same plan, same outcomes: it does climb (rearchitect, then the frontier tier)
            SCENARIO("process-custom-parameters-standard-contrast");
            Sim c = startSim(2, FrontierMode::stub);
            c.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            drive(c, [](const JobRecord& j) { return j.spec.role == "gate" && j.spec.tier != "frontier-stub" ? Outcome{JobState::failed, ""} : Outcome{JobState::succeeded, ""}; });
            CHECK_EQ(count(c, "rearchitect"), 1);
            CHECK_EQ(count(c, "escalate"), 1);
        }
        {   // the planner's wall time and retries, and the integration's wall time, come from the process
            SCENARIO("process-planner-and-integration");
            Process fast = standardProcess();
            fast.name = "fast";
            CHECK(fast.set("plan.job.wallSeconds", "33").has_value() && fast.set("plan.job.retries", "0").has_value() && fast.set("integrate.job.wallSeconds", "77").has_value());
            Sim s = startSim(2, FrontierMode::off, false, fast);
            auto acts = s.step();
            CHECK(acts.size() == 1 && acts[0].job.role == "planner" && acts[0].job.wallSeconds == 33);
            const auto ids = s.run(acts);
            s.finishJob(ids[0], JobState::failed);
            acts = s.step();
            CHECK(acts.size() == 1 && acts[0].kind == WorkflowAction::Kind::finish && acts[0].finalState == WorkflowState::failed);   // no retry
            Sim d = startSim(2, FrontierMode::off);
            acts = d.step();
            const auto dids = d.run(acts);
            d.finishJob(dids[0], JobState::failed);
            acts = d.step();
            CHECK(acts.size() == 1 && acts[0].kind == WorkflowAction::Kind::startJob && acts[0].job.role == "planner" && acts[0].job.wallSeconds == 60);   // the standard process retries once
            Sim e = startSim(2, FrontierMode::off, false, fast);
            e.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            drive(e, [](const JobRecord&) { return Outcome{JobState::succeeded, ""}; });
            CHECK(e.wfs.find("w-000001")->state == WorkflowState::succeeded);
            bool integrator = false;
            for (const auto& j : e.jobs.jobs()) if (j.spec.role == "integrator") { integrator = true; CHECK_EQ(j.spec.wallSeconds, static_cast<std::int64_t>(77)); }
            CHECK(integrator);
        }
        {   // a spec's executor overrides the process for every item; a tester is never forked
            SCENARIO("process-executor-override");
            Sim s;
            s.settings.frontier = FrontierMode::off;
            WorkflowSpec spec;
            spec.task = "t"; spec.sandboxDir = "/sb"; spec.agent = "llm"; spec.engines = {"e=http://127.0.0.1:9/v1"}; spec.executor = "fork";
            s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
            Plan p;
            p.task = "t"; p.harness = "web-static";
            WorkItem t; t.id = "t"; t.title = "Tests"; t.goal = "g"; t.persona = "tester"; t.files = {"tests/e.test.mjs"}; t.gate = gate("tests/e.test.mjs", true);
            WorkItem d; d.id = "d"; d.title = "Work"; d.goal = "g"; d.files = {"src/e.js"}; d.gate = gate("tests/e.test.mjs", false);
            p.items = {t, d};
            s.feed("workflowPlanned", wfbody::planned("w-000001", p));
            const auto acts = s.step();
            CHECK_EQ(acts.size(), static_cast<std::size_t>(2));
            for (const auto& a : acts) {
                if (a.job.item == "t") CHECK(a.job.argv[1] == "llm-agent" && std::find(a.job.argv.begin(), a.job.argv.end(), "canvas") != a.job.argv.end());
                if (a.job.item == "d") CHECK_EQ(a.job.argv[1], std::string("llm-fork"));
            }
        }
    }
    {   // Stages as data (docs/PROCESS_DESIGN.md section 13): processes whose STAGES differ from the standard one run on the same engine, which names no stage.
        using Outcome = std::pair<JobState, std::string>;
        const auto drive = [](Sim& s, const std::function<Outcome(const JobRecord&)>& outcome) {
            for (int guard = 0; guard < 60 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) { const Outcome o = outcome(*s.job(id)); s.finishJob(id, o.first, o.second); }
            }
        };
        const auto allPass = [](const JobRecord&) { return Outcome{JobState::succeeded, ""}; };
        const auto roles = [](Sim& s) { std::string r; for (const auto& j : s.jobs.jobs()) r += (r.empty() ? "" : " ") + j.spec.role; return r; };
        const auto stages = [](Sim& s) { std::string r; for (const auto& st : stageStatuses(*s.wfs.find("w-000001"), s.jobs)) r += (r.empty() ? "" : " ") + st.name + "=" + st.state; return r; };
        const auto waitingOn = [](Sim& s, const char* name) { for (const auto& st : stageStatuses(*s.wfs.find("w-000001"), s.jobs)) if (st.name == name) return st.waitingOn; return std::string{"?"}; };
        {   // one stage MORE than the standard process: after integration the review runs a registered tool, and only then is the workflow done
            SCENARIO("stages-reviewed");
            Sim s = startSim(2, FrontierMode::off, false, reviewedProcess());
            CHECK_EQ(stages(s), std::string("plan=active build=waiting integrate=waiting review=waiting"));
            CHECK_EQ(waitingOn(s, "plan"), std::string("plan.present"));
            CHECK_EQ(waitingOn(s, "build"), std::string("stage.plan.done"));
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            CHECK_EQ(stages(s), std::string("plan=done build=active integrate=waiting review=waiting"));
            CHECK_EQ(waitingOn(s, "build"), std::string("items.all(accepted)"));
            drive(s, allPass);
            const WorkflowRecord& wf = *s.wfs.find("w-000001");
            CHECK(wf.state == WorkflowState::succeeded);
            CHECK_EQ(wf.note, std::string("all items accepted; integration passed; review passed"));
            CHECK_EQ(roles(s), std::string("agent gate integrator qa"));
            const JobRecord& qa = s.jobs.jobs().back();
            CHECK(qa.spec.argv == (std::vector<std::string>{"/bin/sub0squad", "qa", "/sb/project"}) && qa.spec.tool == "qa" && qa.spec.wallSeconds == 120 && qa.spec.item.empty());   // the placeholder is the workflow's sandbox
            CHECK_EQ(stages(s), std::string("plan=done build=done integrate=done review=done"));
        }
        {   // the same process when its extra stage fails: the workflow fails and says which step, with the job's note
            SCENARIO("stages-reviewed-review-fails");
            Sim s = startSim(2, FrontierMode::off, false, reviewedProcess());
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            for (int guard = 0; guard < 60 && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) {
                    if (s.job(id)->spec.role == "qa") s.feed("jobFinished", journalbody::finished(id, s.job(id)->run, JobState::failed, "", "exit 2"));
                    else s.finishJob(id, JobState::succeeded);
                }
            }
            const WorkflowRecord& wf = *s.wfs.find("w-000001");
            CHECK(wf.state == WorkflowState::failed);
            CHECK_EQ(wf.note, std::string("review failed: exit 2"));
            CHECK_EQ(stages(s), std::string("plan=done build=done integrate=done review=failed"));
        }
        {   // one stage FEWER: without the integration stage the accepted items are the result
            SCENARIO("stages-no-integration");
            Process p = standardProcess();
            p.name = "items-only";
            CHECK(p.removeStage("integrate").has_value());
            Sim s = startSim(2, FrontierMode::off, false, p);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            drive(s, allPass);
            const WorkflowRecord& wf = *s.wfs.find("w-000001");
            CHECK(wf.state == WorkflowState::succeeded);
            CHECK_EQ(wf.note, std::string("all items accepted"));
            CHECK_EQ(roles(s), std::string("agent gate"));
        }
        {   // no plan and no items at all: a process of job stages, the second waiting for the first; retries are the stage's
            SCENARIO("stages-jobs-only");
            auto p = Process::fromJson("{\"schema\":\"process-1\",\"name\":\"two-jobs\",\"stages\":["
                                       "{\"name\":\"first\",\"kind\":\"job\",\"job\":{\"step\":\"lint\",\"label\":\"the lint\",\"tool\":\"plan-lint\",\"args\":[\"/sb/a.json\"],\"retries\":2,\"wallSeconds\":30}},"
                                       "{\"name\":\"second\",\"kind\":\"job\",\"entry\":\"stage.first.done && job(lint).tries >= 2\",\"job\":{\"step\":\"scan\",\"tool\":\"qa\",\"args\":[\"/sb\"]}}]}");
            CHECK(p.has_value());
            if (p) {
                Sim s = startSim(2, FrontierMode::off, false, *p);
                int lints = 0;
                drive(s, [&](const JobRecord& j) { return j.spec.role == "lint" && ++lints < 2 ? Outcome{JobState::failed, ""} : Outcome{JobState::succeeded, ""}; });   // the first try fails, the second passes
                const WorkflowRecord& wf = *s.wfs.find("w-000001");
                CHECK(wf.state == WorkflowState::succeeded);
                CHECK_EQ(wf.note, std::string("the lint passed; scan passed"));
                CHECK_EQ(roles(s), std::string("lint lint scan"));
                CHECK(s.jobs.jobs().front().spec.wallSeconds == 30 && s.jobs.jobs().front().spec.argv == (std::vector<std::string>{"/bin/sub0squad", "plan-lint", "/sb/a.json"}));
                // every try fails: three tries (two retries), then the workflow fails and counts them
                Sim f = startSim(2, FrontierMode::off, false, *p);
                drive(f, [](const JobRecord&) { return Outcome{JobState::failed, ""}; });
                CHECK(f.wfs.find("w-000001")->state == WorkflowState::failed);
                CHECK_EQ(f.wfs.find("w-000001")->note, std::string("the lint failed 3 times"));
                CHECK_EQ(roles(f), std::string("lint lint lint"));
                // the first try passes: the second stage's entry also asks for two tries, so it WAITS, and the status says on what (a stall is visible, not silent)
                Sim w = startSim(2, FrontierMode::off, false, *p);
                for (int k = 0; k < 3; ++k) { const auto acts = w.step(); for (const auto& id : w.run(acts)) w.finishJob(id, JobState::succeeded); }
                CHECK(w.wfs.find("w-000001")->state == WorkflowState::running);
                CHECK_EQ(stages(w), std::string("first=done second=waiting"));
                CHECK_EQ(waitingOn(w, "second"), std::string("job(lint).tries >= 2"));
                CHECK(w.step().empty());
            }
        }
        {   // Stage state is DERIVED, never stored: a dispute that voids an accepted item re-opens `build` and closes the stage that waited for it.
            SCENARIO("stages-derived-state");
            Sim s = startSim(2, FrontierMode::off, false, reviewedProcess());
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            std::string integrator;
            for (int guard = 0; guard < 20 && integrator.empty(); ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) { if (s.job(id)->spec.role == "integrator") integrator = id; else s.finishJob(id, JobState::succeeded); }
            }
            CHECK(!integrator.empty());
            CHECK_EQ(stages(s), std::string("plan=done build=done integrate=active review=waiting"));
            s.feed("workflowDecision", wfbody::decision("w-000001", "a", 1, "disputed", "local", "{}"));
            CHECK_EQ(stages(s), std::string("plan=done build=active integrate=waiting review=waiting"));
            CHECK_EQ(waitingOn(s, "integrate"), std::string("stage.build.done"));
            s.finishJob(integrator, JobState::succeeded);
            auto acts = s.step();   // the integration that ran on the voided item does not finish the workflow: the item is redone first
            CHECK(acts.size() == 1 && acts[0].kind == WorkflowAction::Kind::startJob && acts[0].job.role == "agent" && acts[0].job.item == "a" && acts[0].job.attempt == 2);
        }
        {   // a stage whose tool is the plan's harness cannot run before there is a plan: the workflow says so instead of starting a job with no command
            SCENARIO("stages-harness-tool-without-plan");
            auto p = Process::fromJson("{\"schema\":\"process-1\",\"name\":\"early\",\"stages\":[{\"name\":\"integrate\",\"kind\":\"job\",\"job\":{\"step\":\"integrator\",\"tool\":\"harness:integrate\"}}]}");
            CHECK(p.has_value());
            if (p) {
                Sim s = startSim(2, FrontierMode::off, false, *p);
                const auto acts = s.step();
                CHECK(acts.size() == 1 && acts[0].kind == WorkflowAction::Kind::finish && acts[0].finalState == WorkflowState::failed);
                if (acts.size() == 1) CHECK_EQ(acts[0].note, std::string("stage 'integrate' cannot run: its tool is the plan's harness, and there is no plan"));
            }
        }
        {   // the decision of a process that lacks rungs says which, so it can be explained from its own record
            SCENARIO("stages-process-lacks");
            Sim s = startSim(1, FrontierMode::off, false, sectionsProcess());
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            for (int guard = 0; guard < 6 && s.wfs.find("w-000001")->decisions.empty(); ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "gate" ? JobState::failed : JobState::succeeded);
            }
            const auto& decisions = s.wfs.find("w-000001")->decisions;
            CHECK(!decisions.empty());
            if (!decisions.empty()) CHECK(decisions[0].json.find("\"processLacks\":[\"decompose\"]") != std::string::npos);
        }
    }
    checkGoldens(std::filesystem::path{SUB0SQUAD_TEST_DATA} / "actions", "stages");
    return finish();
}
