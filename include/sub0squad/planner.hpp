#pragma once
/// @file planner.hpp
/// The planner role (docs/CLEAN_SLATE.md): a model turns the owner's request, and the owner's answers to the planning questions, into a `plan-2` document
/// with no blueprint to copy. The reply is validated like any plan (`Plan::fromJson`), then by the mechanical plan checks (`checkPlan`) and the harness's
/// check of the plan's own scaffold; every problem goes back to the planner on a repair turn, as a compiler's errors go back to a developer. The first
/// plan that passes is written to `<workflow-dir>/plan.json`; the record of every turn is kept in `<workflow-dir>/planner/` and `planner.json`.

#include "sub0squad/plan.hpp"

#include <string>
#include <vector>

namespace sub0squad {

/// `llm-planner --workflow-dir DIR --engine PROFILE --request TEXT [--max-turns N] [--timeout SEC] [--max-tokens N] [--wall SEC] [--slots N] [--prose] [--min-speedup X]
/// [--stance baseline|specialist]` (slots default to the engine profile's): exit 0 plan written (with `--min-speedup`, the plan that reaches that expected speedup on the
/// slots, else the best valid one, said so in `planner.json` as `barMet`), 1 no valid plan within the turns, 2 the engine failed, 64 usage.
[[nodiscard]] int runLlmPlanner(const std::vector<std::string>& args);

/// The system prompt: the planner's role, the plan-2 form and its rules, and what each registered harness is and does. Exposed so a test can read it.
/// `slots` > 0 tells the planner how many requests the engine runs at once and asks it to over-allocate ready work (plan-lint measures the result).
/// `stance` (stance.hpp): `specialist` says every item becomes a dedicated assignment whose owner may contest the plan, so cards must carry the why.
[[nodiscard]] std::string plannerSystemPrompt(int slots = 0, const std::string& stance = "baseline");

/// The prompt of the prose step (`--prose`): the planning lead decides the split, contracts and examples in plain words; a formatter turns them into the plan form.
[[nodiscard]] std::string prosePlannerPrompt(int slots = 0);

/// The `=== FILE: path === ... === END FILE ===` blocks of a reply (the plan's scaffold, as plain text); `rest` is the reply without them.
[[nodiscard]] std::vector<PlanFile> extractFileBlocks(const std::string& reply, std::string& rest);

/// The JSON object in a model's reply (fences and chatter around it dropped); "" when there is none.
[[nodiscard]] std::string extractJsonObject(const std::string& reply);

/// A model's JSON with the usual slips mended: a double quote inside a string escaped, a newline inside a string escaped, a bracket closed a level too early
/// or never, text after the object dropped. Tried only when the strict parse fails; "" when the reply holds no object.
[[nodiscard]] std::string repairJson(const std::string& reply);

}  // namespace sub0squad
