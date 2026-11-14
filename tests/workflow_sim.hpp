#pragma once
// Shared fixtures of the workflow-engine tests (test_workflow, test_workflow_stages, test_workflow_mends, test_plan, test_policy): the engine driven as a pure
// function over a journal held in memory, the small plans the scenarios use, and the golden action logs.
#include "check.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "sub0squad/harness.hpp"
#include "sub0squad/workflow.hpp"

using namespace sub0squad;

// Golden action logs (docs/PROCESS_DESIGN.md section 5): every pass of the engine in a scenario is traced as one JSON line (the actions it returned and the
// state of every item), and the trace of each scenario is compared with tests/data/actions/<scenario>.ndjson. The goldens were recorded from the engine as it
// was BEFORE the process-as-data refactor; the refactor must reproduce them byte for byte. SUB0SQUAD_WRITE_GOLDENS=1 rewrites them (a deliberate act: review the diff).
inline std::string gScenario = "unnamed";
inline std::map<std::string, std::vector<std::string>> gTrace;
#define SCENARIO(name) do { gScenario = (name); } while (0)

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
    std::vector<WorkflowAction> step()
    {
        const WorkflowRecord& wf = *wfs.find("w-000001");
        auto acts = advance(wf, jobs, settings, policy);
        std::string line = "{\"scenario\":\"" + gScenario + "\",\"pass\":" + std::to_string(gTrace[gScenario].size() + 1) + ",\"actions\":[";
        for (std::size_t i = 0; i < acts.size(); ++i) line += (i ? "," : "") + acts[i].toJson();
        line += "],\"items\":[";
        const auto items = itemStatuses(wf, jobs);
        for (std::size_t i = 0; i < items.size(); ++i) line += std::string{i ? "," : ""} + "{\"id\":\"" + items[i].id + "\",\"status\":\"" + items[i].status + "\",\"attempts\":" + std::to_string(items[i].attempts) + ",\"tier\":\"" + items[i].tier + "\"}";
        line += "],\"state\":\"" + std::string{toString(wf.state)} + "\"}";
        gTrace[gScenario].push_back(std::move(line));
        return acts;
    }
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
    void finishJob(const std::string& id, JobState s, const std::string& exitJson = "") { feed("jobFinished", journalbody::finished(id, jobs.find(id)->run, s, exitJson, "")); }
    const JobRecord* job(const std::string& id) { return jobs.find(id); }
};

/// A plan for the rule engine's scenarios on a registered harness (web-static): developers' items over files the harness has references for, each
/// judged by the engine tests. Which harness it is does not matter to the rules; that every plan names one does.
inline Plan simPlan(std::vector<WorkItem> items)
{
    Plan p;
    p.task = "t";
    p.harness = "web-static";
    p.items = std::move(items);
    for (auto& it : p.items) { it.gate.kind = "node-test"; it.gate.tests = {"tests/eval.test.mjs"}; }
    return p;
}
inline Plan makePlan() { return simPlan({{"a", "A", "goal a", {"src/calc.js"}, {}, 1}, {"b", "B", "goal b", {"src/ui.js"}, {"a"}, 2}, {"c", "C", "goal c", {"style.css"}, {}, 1}}); }
inline Plan onePlan() { return simPlan({{"a", "A", "g", {"src/calc.js"}, {}, 1}}); }

inline Sim startSim(int maxLocal = 2, FrontierMode fm = FrontierMode::stub, bool ask = false, const Process& process = standardProcess(), bool llm = false)
{
    Sim s;
    s.settings.frontier = fm;
    WorkflowSpec spec;
    spec.task = "t";
    spec.sandboxDir = "/sb";
    spec.maxLocalAttempts = maxLocal;
    spec.ask = ask;
    spec.process = process;
    if (llm) { spec.agent = "llm"; spec.engines = {"e=http://127.0.0.1:9/v1"}; }
    s.feed("workflowSubmitted", wfbody::submitted("w-000001", spec));
    return s;
}

/// Which test program owns a golden log, by the scenario's name: the stage scenarios (`process-*`, `stages-*`), the mend scenarios (`mend-*`), and the
/// engine's own (everything else). Every name has exactly one owner, so a stale log is always some program's failure.
inline std::string goldenOwner(const std::string& scenario)
{
    if (scenario.starts_with("mend-")) return "mends";
    if (scenario.starts_with("process-") || scenario.starts_with("stages-")) return "stages";
    return "engine";
}

/// The goldens: every scenario this program traced equals its recorded log line by line; no log owned by `owner` is stale; no scenario was traced under a
/// name another program owns.
inline void checkGoldens(const std::filesystem::path& dir, const std::string& owner)
{
    const bool write = std::getenv("SUB0SQUAD_WRITE_GOLDENS") != nullptr;
    if (write) std::filesystem::create_directories(dir);
    for (const auto& [name, lines] : gTrace) {
        if (goldenOwner(name) != owner) { std::fprintf(stderr, "scenario %s belongs to the '%s' program, not '%s'\n", name.c_str(), goldenOwner(name).c_str(), owner.c_str()); CHECK(false); continue; }
        const std::filesystem::path file = dir / (name + ".ndjson");
        if (write) { std::ofstream o(file, std::ios::trunc); for (const auto& l : lines) o << l << "\n"; continue; }
        std::ifstream in(file);
        CHECK(in.good());
        std::string line;
        std::size_t n = 0;
        for (; std::getline(in, line); ++n) {
            if (n >= lines.size() || line != lines[n]) { std::fprintf(stderr, "golden %s differs at pass %zu:\n  recorded: %s\n  now:      %s\n", name.c_str(), n + 1, line.c_str(), n < lines.size() ? lines[n].c_str() : "(no such pass)"); CHECK(false); break; }
        }
        if (in.eof() && n != lines.size()) { std::fprintf(stderr, "golden %s has %zu pass(es), the scenario now has %zu\n", name.c_str(), n, lines.size()); CHECK(false); }
    }
    if (!write && std::filesystem::exists(dir))
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            const std::string name = e.path().stem().string();
            if (goldenOwner(name) == owner && !gTrace.count(name)) { std::fprintf(stderr, "stale golden %s\n", e.path().filename().c_str()); CHECK(false); }
        }
}
