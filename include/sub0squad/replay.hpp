#pragma once
/// @file replay.hpp
/// Isolate and iterate: take one node of a workflow (a failing one, usually), rebuild just its inputs (the plan, the accepted outputs of everything it
/// depends on, its earlier failure text) in a scratch directory and run it again against the engine, several seeds side by side. Minutes instead of
/// a whole workflow's hours, so a prompt, a setting or a strategy can be tried, compared and kept (docs/WORK_GRAPH.md section 21).

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

struct ReplayOptions
{
    std::filesystem::path sandbox;      ///< the workflow's sandbox (plan.json, items/)
    std::filesystem::path exe;          ///< the sub0squad binary that runs the worker
    std::string node;
    std::string engineProfile;          ///< the engine profile in command-line form; its seed is replaced per variant
    std::vector<long> seeds{1};         ///< one variant per seed, run concurrently
    int maxTokens = 3500, repairTurns = 2;
    bool think = false;                 ///< try the manager's "think" strategy
    int parallel = 0;                   ///< with `fork`: sections at once (0: up to the idle slots)
    std::string sequentialReason;       ///< with `fork`: one section at a time, and why (the sequential arm of a comparison)
    bool fork = false;                  ///< run the node as forked sessions (skeleton, then one session per function) instead of one worker session
    bool decompose = false;             ///< replay the division of the node (the architect job) instead of the worker
    std::string treatment = "baseline";
};

/// Prints a table of the variants (result, turns, completion tokens, wall seconds, first failure, where the conversation trace is). Exit 0 when at
/// least one variant passed, 1 otherwise, 2 on a setup error.
[[nodiscard]] int runReplay(const ReplayOptions& options);

}  // namespace sub0squad
