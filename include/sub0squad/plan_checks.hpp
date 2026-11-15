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

/// Problems with `plan`, empty when it passes. Pure: no files, no engine.
[[nodiscard]] std::vector<std::string> checkPlan(const Plan& plan);

}  // namespace sub0squad
