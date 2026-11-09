#pragma once
/// @file triage.hpp
/// Triage is the first call (docs/SQUAD_UX_AND_HARNESS.md section 1): a cheap, rule-based decision (a small local model later) about
/// what kind of project this is, whether a known blueprint covers it, how it should be split and whether an architect is needed.
/// Architect and project manager roles exist only when triage says so; an unknown task is DEFERRED as a file handoff, never sent anywhere.

#include <string>
#include <vector>

namespace sub0squad {

struct TriageResult
{
    std::string projectType;             ///< "web-static", "cpp-header" or "unknown"
    std::string blueprint;               ///< known task name, or empty
    int complexity = 1;                  ///< 1..5
    bool split = false;
    std::vector<std::string> personas;   ///< personas the plan will use
    std::string architect;               ///< "not needed" or "deferred"
    int cardBudgetTokens = 1500;
    std::vector<std::string> reasons;

    [[nodiscard]] std::string toJson() const;
};

[[nodiscard]] TriageResult triage(const std::string& taskText);
/// True for task names the mock planner can plan itself (the blueprints).
[[nodiscard]] bool isKnownTask(const std::string& task);
/// The `needs-plan.md` handoff: everything an architect (a frontier model or a person) needs to answer with one plan-2 file.
[[nodiscard]] std::string needsPlanPacket(const std::string& taskText, const TriageResult& t, const std::string& workflowId);

}  // namespace sub0squad
