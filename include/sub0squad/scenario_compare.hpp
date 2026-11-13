#pragma once
/// @file scenario_compare.hpp
/// `sub0squad scenario compare A.json B.json` (docs/SCENARIOS.md): two `scenario-results-1` files side by side, scenario by scenario, with a verdict that
/// does not mistake noise for change. With a handful of trials two pass counts differ by chance all the time, so a difference is only called a REGRESSION or
/// an IMPROVEMENT when the two 95 % Wilson intervals do not overlap; anything else is "no evidence of change". Pure: text in, report out, no model, no I/O.

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// One scenario as both files have it. A side that lacks the scenario has `trials == 0`.
struct ScenarioDelta
{
    std::string platform, element;
    int passedA = 0, trialsA = 0, passedB = 0, trialsB = 0;
    double lowerA = 0, upperA = 0, lowerB = 0, upperB = 0;   ///< 95 % Wilson interval of each pass rate
    double secondsA = 0, secondsB = 0, tokensA = 0, tokensB = 0;
    std::string verdict;   ///< regression | improvement | no evidence of change | only in A | only in B
};

struct ScenarioComparison
{
    std::string buildA, buildB, engineA, engineB, executorA, executorB, treatmentA, treatmentB;
    std::vector<ScenarioDelta> rows;   ///< A's order, then the scenarios only B has
    int regressions = 0, improvements = 0;
    /// The table and the summary line, as `scenario compare` prints them.
    [[nodiscard]] std::string text() const;
};

/// Upper bound of the 95 % Wilson score interval for `passes` of `trials` (1 when there are no trials).
[[nodiscard]] double wilsonUpperBound(int passes, int trials);

/// Compare two `scenario-results-1` documents. The error says which side is wrong and why.
[[nodiscard]] std::expected<ScenarioComparison, std::string> compareScenarioResults(std::string_view a, std::string_view b);

}  // namespace sub0squad
