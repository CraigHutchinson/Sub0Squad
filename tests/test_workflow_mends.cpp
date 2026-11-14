// The workflow engine's mend decisions (docs/PROCESS_DESIGN.md section 14): one mend and no rearchitect; twice, then the ladder; a mend with no model; disabled; ignored.
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
    {   // Self-mending (mends.hpp, catalogue rows 59 to 63): a failed gate that reports a known mechanical error gets the decision `mend` and the mend's action
        // as its next attempt. It is not a rung of the ladder; the same signature is mended at most twice per item; then the ladder goes on.
        const auto count = [](Sim& s, const char* action) { int n = 0; for (const auto& d : s.wfs.find("w-000001")->decisions) if (d.action == action) ++n; return n; };
        const auto gateResult = [](int code, const char* name, const char* action, const char* symbol, int line = 0, int column = 0) {
            MendMatch m;
            m.name = name; m.action = action; m.requires_ = "contract"; m.symbol = symbol; m.file = "src/calc.js"; m.line = line; m.column = column; m.error = "the first error line";
            return "{\"schema\":\"exit-1\",\"outcome\":\"exited\",\"code\":" + std::to_string(code) + ",\"result\":{\"mend\":" + m.toJson() + "}}";
        };
        const auto has = [](const JobSpec& j, const char* word) { return std::find(j.argv.begin(), j.argv.end(), word) != j.argv.end(); };
        // One pass, then finish every job it started: agents succeed, a gate ends as `gateOutcome(attempt)` says ("" = it passes).
        const auto drive = [](Sim& s, const std::function<std::string(const JobRecord&)>& gateOutcome, int passes = 40) {
            for (int guard = 0; guard < passes && s.wfs.find("w-000001")->state == WorkflowState::running; ++guard) {
                const auto acts = s.step();
                for (const auto& id : s.run(acts)) {
                    const JobRecord& j = *s.job(id);
                    const std::string exit = j.spec.role == "gate" ? gateOutcome(j) : std::string{};
                    s.finishJob(id, exit.empty() ? JobState::succeeded : JobState::failed, exit);
                }
            }
        };
        {   // run-v10's defect: the entry point is undefined. ONE mend decision, the next attempt writes only that section, the gate passes: no rearchitect.
            SCENARIO("mend-add-section");
            Sim s = startSim(2, FrontierMode::off, false, standardProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            auto acts = s.step();
            auto ids = s.run(acts);
            s.finishJob(ids[0], JobState::succeeded);                      // the agent's attempt 1
            ids = s.run(s.step());
            s.finishJob(ids[0], JobState::failed, gateResult(6, "undefined-contract-symbol", "add-section", "calc::evaluate"));   // its gate: INTERFACE MISMATCH, with the mend
            acts = s.step();
            CHECK_EQ(acts.size(), static_cast<std::size_t>(2));
            if (acts.size() == 2) {
                CHECK(acts[0].kind == WorkflowAction::Kind::recordDecision && acts[0].decisionAction == "mend" && acts[0].decisionTier == "undefined-contract-symbol" && acts[0].attempt == 1);
                CHECK(acts[0].decisionJson.find("\"signature\":\"undefined-contract-symbol\"") != std::string::npos && acts[0].decisionJson.find("\"symbol\":\"calc::evaluate\"") != std::string::npos && acts[0].decisionJson.find("\"applied\":1") != std::string::npos);
                const JobSpec& j = acts[1].job;
                CHECK(acts[1].kind == WorkflowAction::Kind::startJob && j.role == "agent" && j.attempt == 2 && j.argv[1] == "llm-fork" && !j.leaseResource.empty());
                CHECK(has(j, "--mend-section") && has(j, "calc::evaluate") && has(j, "--mend-file") && has(j, "src/calc.js"));
            }
            const auto again = s.step();                                    // idempotent: asked again before anything is applied, the same two actions
            CHECK(again.size() == 2 && again[0].toJson() == acts[0].toJson() && again[1].toJson() == acts[1].toJson());
            ids = s.run(acts);
            s.finishJob(ids[0], JobState::succeeded);
            drive(s, [](const JobRecord&) { return std::string{}; });
            const WorkflowRecord& wf = *s.wfs.find("w-000001");
            CHECK(wf.state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "mend"), 1);
            CHECK_EQ(count(s, "rearchitect"), 0);
            CHECK_EQ(count(s, "retry"), 0);                                 // and no rung of the ladder was spent
            CHECK_EQ(static_cast<int>(wf.decisions.size()), 1);
        }
        {   // The same signature again and again: mended twice, and the THIRD time the ladder decides (the interface-gap rule: rearchitect), with the two
            // mended attempts not counted against it.
            SCENARIO("mend-recurs-then-ladder");
            Sim s = startSim(2, FrontierMode::off, false, standardProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            const std::string same = gateResult(6, "undefined-contract-symbol", "add-section", "calc::evaluate");
            std::vector<std::string> order;
            for (int guard = 0; guard < 12 && order.size() < 3; ++guard) {
                const auto acts = s.step();
                for (const auto& a : acts) if (a.kind == WorkflowAction::Kind::recordDecision) order.push_back(a.decisionAction + "@" + std::to_string(a.attempt));
                for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "gate" ? JobState::failed : JobState::succeeded, s.job(id)->spec.role == "gate" ? same : std::string{});
            }
            CHECK(order == (std::vector<std::string>{"mend@1", "mend@2", "rearchitect@3"}));
            const auto& decisions = s.wfs.find("w-000001")->decisions;
            CHECK_EQ(decisions.size(), static_cast<std::size_t>(3));
            if (decisions.size() == 3) {
                CHECK(decisions[1].json.find("\"applied\":2") != std::string::npos);
                CHECK(decisions[2].json.find("\"localAttempts\":1,") != std::string::npos && decisions[2].json.find("\"totalAttempts\":1,") != std::string::npos);   // three attempts ran; two were the harness's
            }
        }
        {   // A different signature has its own two: run-v10 attempts 3 to 5 (an unused variable) after an undefined entry point. The unused-name mend needs no
            // model: its job is `mend-apply`, with no engine lease, even for the mock agent.
            SCENARIO("mend-silence-unused");
            Sim s = startSim(2, FrontierMode::off);                          // the mock agent: no engine at all
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            auto ids = s.run(s.step());
            s.finishJob(ids[0], JobState::succeeded);
            ids = s.run(s.step());
            s.finishJob(ids[0], JobState::failed, gateResult(1, "unused-entity", "silence-unused", "hasExp", 321, 10));
            const auto acts = s.step();
            CHECK_EQ(acts.size(), static_cast<std::size_t>(2));
            if (acts.size() == 2) {
                const JobSpec& j = acts[1].job;
                CHECK(acts[0].decisionAction == "mend" && acts[0].decisionTier == "unused-entity");
                CHECK(j.tool == "mend-apply" && j.role == "agent" && j.attempt == 2 && j.leaseResource.empty() && j.wallSeconds == 120);
                CHECK(j.argv == (std::vector<std::string>{"/bin/sub0squad", "mend-apply", "--workflow-dir", "/sb", "--item", "a", "--attempt", "2", "--action", "silence-unused", "--file", "src/calc.js", "--line", "321", "--column", "10", "--symbol", "hasExp"}));
            }
            ids = s.run(acts);
            s.finishJob(ids[0], JobState::succeeded);
            drive(s, [](const JobRecord&) { return std::string{}; });
            CHECK(s.wfs.find("w-000001")->state == WorkflowState::succeeded);
            CHECK_EQ(count(s, "mend"), 1);
        }
        {   // What does NOT mend: a process that disables the mend, or all mends; a mend that needs a model when there is none. The ladder decides as before.
            SCENARIO("mend-not-applied");
            const std::string gap = gateResult(6, "undefined-contract-symbol", "add-section", "calc::evaluate");
            const auto firstDecision = [&](const Process& p, bool llm) {
                Sim s = startSim(2, FrontierMode::off, false, p, llm);
                s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
                for (int guard = 0; guard < 6 && s.wfs.find("w-000001")->decisions.empty(); ++guard) {
                    const auto acts = s.step();
                    for (const auto& id : s.run(acts)) s.finishJob(id, s.job(id)->spec.role == "gate" ? JobState::failed : JobState::succeeded, s.job(id)->spec.role == "gate" ? gap : std::string{});
                }
                const auto& d = s.wfs.find("w-000001")->decisions;
                return d.empty() ? std::string{"none"} : d[0].action;
            };
            CHECK_EQ(firstDecision(standardProcess(), true), std::string("mend"));
            auto without = Process::fromJson(standardProcess().toJson());
            CHECK(without.has_value());
            if (without) {
                without->name = "no-contract-mend";
                without->stages[1].leaf.mendsDisabled = {"undefined-contract-symbol"};
                CHECK(without->validate().empty());
                auto back = Process::fromJson(without->toJson());       // the list is part of the file
                CHECK(back.has_value() && back->stages[1].leaf.mendsDisabled == std::vector<std::string>{"undefined-contract-symbol"});
                CHECK_EQ(firstDecision(*without, true), std::string("rearchitect"));
                Process off = standardProcess();
                off.name = "no-mends";
                CHECK(off.set("build.leaf.mends.enabled", "false").has_value());
                CHECK_EQ(firstDecision(off, true), std::string("rearchitect"));
            }
            CHECK_EQ(firstDecision(standardProcess(), false), std::string("rearchitect"));   // add-section writes a section: with the mock agent there is no model to write it
        }
        {   // A gate result that is not a mend, or is malformed, changes nothing: the exit code alone is read, as before.
            SCENARIO("mend-result-ignored");
            Sim s = startSim(2, FrontierMode::off, false, standardProcess(), true);
            s.feed("workflowPlanned", wfbody::planned("w-000001", onePlan()));
            auto ids = s.run(s.step());
            s.finishJob(ids[0], JobState::succeeded);
            ids = s.run(s.step());
            s.finishJob(ids[0], JobState::failed, "{\"code\":1,\"result\":{\"mend\":{\"name\":\"\"},\"other\":[1,2]}}");
            const auto acts = s.step();
            CHECK(!acts.empty() && acts[0].kind == WorkflowAction::Kind::recordDecision && acts[0].decisionAction == "retry");
        }
    }
    checkGoldens(std::filesystem::path{SUB0SQUAD_TEST_DATA} / "actions", "mends");
    return finish();
}
