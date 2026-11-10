#pragma once
/// @file plan_lint.hpp
/// A cheap audit of a plan's shape before any work runs: how wide it is, how long its critical path is, and where it is serial.
/// Rule based and advisory (the orchestration rule of docs/WORK_GRAPH.md section 11: front-load parallelism and small packages).

#include "sub0squad/plan.hpp"

#include <string>
#include <vector>

namespace sub0squad {

struct PlanLint
{
    std::vector<std::vector<std::string>> levels;  ///< item ids by dependency level (level 0 has no dependencies)
    std::vector<std::string> criticalPath;         ///< the dependency chain with the most estimated tokens
    double totalTokens = 0, criticalTokens = 0;    ///< rough completion-token estimates (complexity based; marked as estimates everywhere)
    int maxWidth = 0;                              ///< most items that can run at once
    double serialFraction = 0;                     ///< critical tokens over total tokens (1 = fully serial)
    double minutesSerial = 0, minutesCritical = 0; ///< at `tokensPerSecond` for one request: everything one after another, and the critical path alone
    std::vector<std::string> warnings;
    [[nodiscard]] std::string toText() const;
};

/// Rough completion tokens for one item: testers 1,000 + 250 per complexity point, developers 400 + 500 per point.
[[nodiscard]] double estimateItemTokens(const WorkItem& item);

[[nodiscard]] PlanLint lintPlan(const Plan& plan, double tokensPerSecond = 5.0);

}  // namespace sub0squad
