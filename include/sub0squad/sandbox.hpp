#pragma once
/// @file sandbox.hpp
/// The worker job commands: the mock planner and mock agent (a registered harness's blueprint and reference deliverables, correct or deliberately
/// broken per failure injection), the model-driven agents and the architect jobs. Each is a plain process job: `sub0squad <name> <options>`.
/// Gates and integrators belong to the project harnesses (include/sub0squad/harness.hpp).

#include "sub0squad/plan.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

/// `mock-planner --task NAME --out FILE`: write the blueprint plan a registered harness has for NAME (exit 3 when none does).
[[nodiscard]] int runMockPlanner(const std::vector<std::string>& args);
/// `mock-agent --workflow-dir D --item I --tier T --attempt N [--inject ITEM[:N]]... [--delay-ms MS]`
[[nodiscard]] int runMockAgent(const std::vector<std::string>& args);
/// `llm-agent --workflow-dir D --item I --tier T --attempt N --engine NAME=URL[,model=..]`: one real engine call per attempt (tier `frontier-stub` stands in
/// with the mock templates: no frontier provider exists in this build). Reports where its tokens went.
[[nodiscard]] int runLlmAgent(const std::vector<std::string>& args);
/// `llm-fork --workflow-dir D --item I --attempt N --engine E [--parallel N] [--sequential-reason TEXT] [--mem-floor-mib N] [--attempt-wall SEC] [--agent-exit 1]`: forked sections
/// on the canvas (include/sub0squad/fork.hpp): a primer writes the skeleton, each function is a section written by a session of its own, side by side up
/// to the idle slots. Exit 0 the merged file passes its gate, 1 it does not, 2 no usable skeleton (`--agent-exit 1`: 0 whenever the files were written).
[[nodiscard]] int runLlmFork(const std::vector<std::string>& args);
/// `mock-architect --mode decompose|rearchitect ...` and `llm-architect --mode ... --engine ...`: the dedicated jobs of the escalation ladder.
/// decompose writes `attempt-N/fragment.json` (a plan-2 fragment the daemon merges) or declines (exit 3); rearchitect writes `card-override.json`.
[[nodiscard]] int runMockArchitect(const std::vector<std::string>& args);
[[nodiscard]] int runLlmArchitect(const std::vector<std::string>& args);
/// Read a `card-override.json` written by a rearchitect job into `card` (false if absent or invalid).
[[nodiscard]] bool readCardOverride(const std::filesystem::path& file, WorkCard& card);
/// `optimize-job --workflow-dir D --item I --engine E --treatments a,b --rounds R`: run ONE item in isolation (its inputs fixed) under each prompt
/// treatment, gate every attempt without publishing, and report pass rate, failing tests, tokens and time per treatment.
[[nodiscard]] int runOptimizeJob(const std::vector<std::string>& args);

}  // namespace sub0squad
