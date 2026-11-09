#pragma once
/// @file sandbox.hpp
/// The sandbox harness's job commands (docs/SANDBOX.md): a mock planner, a mock agent that writes real C++ (correct or deliberately
/// broken per failure injection), a per-item gate that compiles and runs that item's own test, and an integrator that merges the
/// accepted items and runs the combined build. Each is a plain process job: `sub0squad <name> <options>`.

#include <string>
#include <vector>

namespace sub0squad {

/// `mock-planner --task NAME --out FILE`: write a plan-1 for "calculator-basic" (one item) or "calculator-scientific" (six items).
[[nodiscard]] int runMockPlanner(const std::vector<std::string>& args);
/// `mock-agent --workflow-dir D --item I --tier T --attempt N [--inject ITEM[:N]]... [--delay-ms MS]`
[[nodiscard]] int runMockAgent(const std::vector<std::string>& args);
/// `llm-agent --workflow-dir D --item I --tier T --attempt N --engine NAME=URL[,model=..]`: one real engine call per attempt (tier `frontier-stub` stands in
/// with the mock templates: no frontier provider exists in this build). Reports where its tokens went.
[[nodiscard]] int runLlmAgent(const std::vector<std::string>& args);
/// `sandbox-gate --workflow-dir D --item I --attempt N`: allowlist check, compile with -Wall -Werror, run the item's test.
[[nodiscard]] int runSandboxGate(const std::vector<std::string>& args);
/// `sandbox-integrate --workflow-dir D`: merge accepted items into project/, build, run the combined tests and a smoke check.
[[nodiscard]] int runSandboxIntegrate(const std::vector<std::string>& args);

/// The calculator source for `file` (correct, or with the deliberate defect that makes this file's item fail its gate).
/// Empty string if `file` is not part of the calculator project.
[[nodiscard]] std::string calculatorFile(const std::string& file, bool buggy);
/// The file whose content carries the injected defect for `item` ("" if unknown).
[[nodiscard]] std::string calculatorBugTarget(const std::string& item);

}  // namespace sub0squad
