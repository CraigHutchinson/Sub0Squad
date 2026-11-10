#pragma once
/// @file qa.hpp
/// The QA head's first, rule-based eyes: scan the code a workflow has produced (accepted outputs, or any directory of sources) for what independent workers
/// tend to leave behind, and say it as findings the design can use while the work is still going on (docs/WORK_GRAPH.md section 24). It never changes
/// acceptance; it communicates quality. No engine is needed, so it is free to run whenever there is something new to look at.

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

struct QaFinding
{
    std::string axis;        ///< duplication | simplicity
    std::string severity;    ///< warn | info
    std::vector<std::string> where;  ///< "file:from-to" locations
    std::string what;
    std::string suggestion;
};

/// Scan every .js / .mjs source under `root` (test files and node_modules left out).
[[nodiscard]] std::vector<QaFinding> qaScan(const std::filesystem::path& root);

/// The sources of a workflow as one tree: every node's accepted outputs, by path (a later node's file wins).
[[nodiscard]] std::filesystem::path qaCollect(const std::filesystem::path& sandbox, const std::filesystem::path& into);

[[nodiscard]] std::string qaText(const std::vector<QaFinding>& findings);

}  // namespace sub0squad
