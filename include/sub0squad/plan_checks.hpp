#pragma once
/// @file plan_checks.hpp
/// The mechanical checks of a plan a MODEL wrote (docs/CLEAN_SLATE.md section 4). `Plan::validate` says whether a plan is well formed; these say whether it is
/// one the squad can succeed with, from the failures the first campaign found: tests nobody checked, a contract with no examples, an item with no
/// acceptance, a developer who is not held to any tests, a scaffold that does not carry the project's own stubs. Each problem is one plain sentence that the
/// planner is shown on its repair turn, so it says what to change.

#include "sub0squad/plan.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sub0squad {

/// The fewest worked examples a contract must carry for an item that has tests (the tester's literals and the developer's code are both checked against them).
inline constexpr int kMinWorkedExamples = 3;

/// The most items a model-made plan may have, and its largest size in bytes (a small engine's reply must fit its token limit and its slot).
inline constexpr std::size_t kMaxPlanItems = 8;
inline constexpr std::size_t kMaxPlanBytes = 16000;

/// The slips a plan can be mended from without asking the model again (each costs a repair turn, and on a slow engine a turn is half an hour): a gate that
/// is not strict enough or asks for more tests than the examples can give, a developer not tied to the tester whose tests it is graded by (when exactly one
/// tester item owns those tests), a command-line program's `src/main.cpp` with no stub. Returns one sentence per mend made.
[[nodiscard]] std::vector<std::string> mendPlan(Plan& plan);

/// Problems with `plan`, empty when it passes. Pure: no files, no engine.
[[nodiscard]] std::vector<std::string> checkPlan(const Plan& plan);

}  // namespace sub0squad
