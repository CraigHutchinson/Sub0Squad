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
    /// Slot fill (owner direction, docs/CLEAN_SLATE.md section 6): how well the plan keeps `slots` busy when the size of every item is UNKNOWN. A plan is
    /// scheduled many times over (a fixed-seed Monte Carlo: each item's estimate times a log-normal factor of spread `sigma`, ready items start in plan
    /// order on the first free slot). Over-allocating ready work beats under-using slots, because items of different sizes finish at different times and
    /// a freed slot needs something to take.
    int slots = 0;                     ///< the slot count the numbers are for (0: not computed)
    double fill = 0;                   ///< mean of total work / (slots x makespan): 1 means no slot ever idled
    double fillP10 = 0;                ///< the 10th percentile of the same (a bad draw of sizes)
    double expectedSpeedup = 0;        ///< mean of total work / makespan (at most `slots`)
    double boundSpeedup = 0;           ///< the best any schedule can do on the estimates: min(slots, total work / critical path)
    double backfillRate = 0;           ///< share of the moments a slot freed (while work remained) at which ready items were waiting for EVERY idle slot
    double widthRatio = 0;             ///< items that can be ready at once over slots: 1.5 or more is the target, below 1 leaves slots idle by construction
    std::vector<std::string> warnings;
    [[nodiscard]] std::string toText() const;
};

/// Rough completion tokens for one item: testers 1,000 + 250 per complexity point, developers 400 + 500 per point.
[[nodiscard]] double estimateItemTokens(const WorkItem& item);

/// `slots` > 0 also computes the slot-fill scalars; `sigma` is the spread (log-normal) of the unknown item sizes, 0.6 meaning a typical factor of about 1.8 either way.
[[nodiscard]] PlanLint lintPlan(const Plan& plan, double tokensPerSecond = 5.0, int slots = 0, double sigma = 0.6);

}  // namespace sub0squad
