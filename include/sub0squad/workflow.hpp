#pragma once
/// @file workflow.hpp
/// Workflows: a project-scope task delegated into work items (docs/SANDBOX.md). `WorkflowStore` is rebuilt from the journal;
/// `advance()` is the level-triggered engine, a pure function from (workflow, its labelled jobs, settings, policy) to the next
/// actions, so a restarted daemon derives exactly the same next steps and needs no special recovery. No I/O happens here.

#include "sub0squad/job_store.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/policy.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sub0squad {

struct WorkflowSpec
{
    std::string task;                  ///< e.g. "calculator-scientific"
    std::string sandboxDir;            ///< absolute directory holding plan.json, items/ and project/
    std::vector<std::string> inject;   ///< failure injection for the mock agent: "item" or "item:N" (fail the first N local attempts)
    int maxLocalAttempts = 2;

    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static std::expected<WorkflowSpec, std::string> fromJson(std::string_view text);
};

enum class WorkflowState { running, succeeded, failed, cancelled };
[[nodiscard]] const char* toString(WorkflowState s);

struct DecisionRecord { std::string item; int attempt = 0; std::string action; std::string tier; std::string json; };

struct WorkflowRecord
{
    std::string id;
    WorkflowSpec spec;
    std::optional<Plan> plan;
    std::vector<DecisionRecord> decisions;
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
[[nodiscard]] std::string cancelRequested(const std::string& id);
[[nodiscard]] std::string finished(const std::string& id, WorkflowState state, const std::string& note);
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
    enum class Kind { startJob, loadPlan, recordDecision, cancelJob, finish } kind = Kind::finish;
    JobSpec job;                                       ///< startJob
    std::string jobId;                                 ///< cancelJob
    std::string item;                                  ///< recordDecision
    int attempt = 0;                                   ///< recordDecision
    std::string decisionAction, decisionTier, decisionJson;  ///< recordDecision
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
