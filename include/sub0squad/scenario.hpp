#pragma once
/// @file scenario.hpp
/// Capability scenarios (docs/SCENARIOS.md): each workflow element is exercised IN ISOLATION from a fixed starting state and judged by the trusted
/// harness gate, never by the agent's own claim, over many trials with different seeds. The report is a pass rate with a confidence lower bound, so
/// "it passed" is not mistaken for "it works". The same scenarios run on every platform, which is what separates a capable agent from a lucky one.

#include <string>
#include <vector>

namespace sub0squad {

/// One scenario on one platform.
struct ScenarioInfo
{
    std::string id;          ///< e.g. "dev-repair-syntax"
    std::string element;     ///< the workflow element it exercises: tester, developer-first, developer-repair-syntax, developer-repair-semantic, app-first, app-repair
    std::string platform;    ///< registry harness name
};

/// Every scenario of every platform (stable order).
[[nodiscard]] std::vector<ScenarioInfo> scenarioCatalogue();

/// Lower bound of the 95 % Wilson score interval for `passes` of `trials` (0 when there are no trials).
[[nodiscard]] double wilsonLowerBound(int passes, int trials);

/// Fixture validation without a model: from each scenario's starting state the REFERENCE solution passes the judge and the starting state itself (the
/// draft, or the stub) does not. Returns one message per problem (empty = every fixture is valid). `only` limits it to one scenario id or platform.
/// `exe` is the path of the sub0squad binary (its gate commands are run as processes); it is checked, and a nested run is refused.
[[nodiscard]] std::vector<std::string> scenarioSelfTest(const std::string& only, const std::string& exe);

/// `sub0squad scenario list | selftest [--only X] | run --engine E [--trials N] [--parallel P] [--only X] [--treatment T] [--repair-turns N] [--out DIR] [--min-lower F]`
[[nodiscard]] int runScenarioCommand(const std::vector<std::string>& args);

}  // namespace sub0squad
