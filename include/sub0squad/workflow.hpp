#pragma once
/// @file workflow.hpp
/// Workflows: a project-scope task delegated into work items (docs/SANDBOX.md). `WorkflowStore` is rebuilt from the journal;
/// `advance()` is the level-triggered engine, a pure function from (workflow, its labelled jobs, settings, policy) to the next
/// actions, so a restarted daemon derives exactly the same next steps and needs no special recovery. No I/O happens here.

#include "sub0squad/engine.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/policy.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sub0squad {

struct WorkflowSpec
{
    std::string task;                  ///< a known task name (blueprint), or "custom"
    std::string taskText;              ///< the owner's words, as given to `do`
    std::string triage;                ///< the triage-1 record (JSON) kept with the workflow
    std::string sandboxDir;            ///< absolute directory holding plan.json, items/ and project/
    std::vector<std::string> inject;   ///< failure injection for the mock agent: "item" or "item:N" (fail the first N local attempts)
    int maxLocalAttempts = 2;
    std::string agent = "mock";                   ///< "mock" (templates) or "llm" (a real engine on the local tier)
    std::vector<std::string> engines;             ///< engine profiles in command-line form (EngineProfile::toArg); the first is the default
    std::map<std::string, std::string> route;     ///< persona -> engine name, e.g. tester=qwen3-6 (extensibility: engines are per persona)
    std::string treatment = "baseline";           ///< prompt treatment of llm agents: baseline | index | planfirst (see docs/OPTIMIZATION.md)
    int repairTurns = 2;                          ///< extra turns an llm agent takes to run the tests and fix failures before it says done
    int sizeBudget = 1800;                        ///< estimated completion tokens above which an item that has tests is divided BEFORE the first attempt (0 = never)
    int maxTokens = 3000;                         ///< completion budget of one llm attempt
    bool ask = false;                             ///< governance on: an item the ladder cannot finish is deferred to the owner (a blocking request) instead of failing the workflow

    /// The engine profile for a persona (route, else the first engine), or nullopt when none is configured.
    [[nodiscard]] std::optional<EngineProfile> engineFor(const std::string& persona) const;
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static std::expected<WorkflowSpec, std::string> fromJson(std::string_view text);
};

enum class WorkflowState { running, awaitingPlan, succeeded, failed, cancelled };
[[nodiscard]] const char* toString(WorkflowState s);

struct DecisionRecord { std::string item; int attempt = 0; std::string action; std::string tier; std::string json; };

/// A request: something a node, the engine or the owner raises that needs a decision or a clarification, and what became of it. Kinds:
/// question | clarification | requirement | blocker | proposal | escalation. A blocking request holds the nodes in its scope (all nodes when the scope is empty).
struct RequestRecord
{
    std::string id;                      ///< r-1, r-2, ... per workflow
    std::string kind, from, title, detail;
    int attempt = 0;                     ///< escalation: the attempt of `scope[0]` that ended the ladder
    std::vector<std::string> scope, options;
    bool blocking = false;
    std::string status = "open";         ///< open | answered
    std::string triage, analysis;        ///< the first responder's class (guidance | fatal | owner) and its diagnosis
    std::string answer, resolution, answeredBy;  ///< resolution: retry | stop | "" (escalations); answer is free text, handed to the scope's nodes as guidance
    double raisedAt = 0, answeredAt = 0;
};

struct WorkflowRecord
{
    std::string id;
    WorkflowSpec spec;
    std::optional<Plan> plan;
    std::vector<DecisionRecord> decisions;
    std::vector<RequestRecord> requests;
    std::set<std::string> rejectedAmends;   ///< "item#attempt": a decomposition the daemon refused (invalid plan), which counts as a failed attempt
    std::vector<std::string> amendedItems;  ///< items that were replaced by a division, in order (history)
    bool cancelRequested = false;
    WorkflowState state = WorkflowState::running;
    std::string note;
    double submittedAt = 0, finishedAt = 0;
};

/// Journal record bodies for workflows.
namespace wfbody {
[[nodiscard]] std::string submitted(const std::string& id, const WorkflowSpec& spec);
[[nodiscard]] std::string planned(const std::string& id, const Plan& plan);
[[nodiscard]] std::string decision(const std::string& id, const std::string& item, int attempt, const std::string& action, const std::string& tier, const std::string& decisionJson);
[[nodiscard]] std::string planAmended(const std::string& id, const std::string& replaced, const Plan& plan);
[[nodiscard]] std::string amendRejected(const std::string& id, const std::string& item, int attempt, const std::string& reason);
[[nodiscard]] std::string requestRaised(const std::string& id, const RequestRecord& r);
[[nodiscard]] std::string requestAnalysed(const std::string& id, const std::string& rid, const std::string& triage, const std::string& analysis);
[[nodiscard]] std::string requestAnswered(const std::string& id, const std::string& rid, const std::string& answer, const std::string& resolution, const std::string& by);
[[nodiscard]] std::string cancelRequested(const std::string& id);
[[nodiscard]] std::string finished(const std::string& id, WorkflowState state, const std::string& note);
[[nodiscard]] std::string awaitingPlan(const std::string& id, const std::string& packetPath);
}  // namespace wfbody

class WorkflowStore
{
public:
    void apply(const JournalRecord& rec);
    [[nodiscard]] const std::vector<WorkflowRecord>& workflows() const { return workflows_; }
    [[nodiscard]] const WorkflowRecord* find(std::string_view id) const;
    [[nodiscard]] std::string nextId() const;

private:
    std::vector<WorkflowRecord> workflows_;
};

struct WorkflowSettings
{
    std::string exe;                                   ///< the sub0squad binary the item jobs run
    FrontierMode frontier = FrontierMode::stub;
};

/// What the engine asks the daemon to do, in order.
struct WorkflowAction
{
    enum class Kind { startJob, loadPlan, recordDecision, cancelJob, finish, amendPlan, writeDispute, sendDown, raiseRequest } kind = Kind::finish;
    JobSpec job;                                       ///< startJob
    std::string jobId;                                 ///< cancelJob
    std::string item;                                  ///< recordDecision
    int attempt = 0;                                   ///< recordDecision
    std::string decisionAction, decisionTier, decisionJson;  ///< recordDecision
    RequestRecord request;                             ///< raiseRequest (the daemon assigns the id)
    std::string fromItem;                              ///< writeDispute: the item whose failure disputes `item`'s output (attempt is that item's attempt)
    WorkflowState finalState = WorkflowState::failed;  ///< finish
    std::string note;                                  ///< finish
};

/// Evaluate one running workflow. Idempotent: calling it again before its actions are applied returns the same actions.
[[nodiscard]] std::vector<WorkflowAction> advance(const WorkflowRecord& wf, const JobStore& jobs, const WorkflowSettings& settings,
                                                  const IDecisionPolicy& policy);

/// Per-item view for status output.
struct ItemStatus
{
    std::string id, title, status;  ///< pending | running | accepted | failed
    int attempts = 0;
    std::string tier;               ///< tier of the latest attempt
};
[[nodiscard]] std::vector<ItemStatus> itemStatuses(const WorkflowRecord& wf, const JobStore& jobs);

}  // namespace sub0squad
