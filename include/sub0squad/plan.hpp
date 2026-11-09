#pragma once
/// @file plan.hpp
/// `plan-1`: the data a planner produces. A plan is validated as data and never executed on trust (docs/DESIGN.md section 6).

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// One unit of delegated work.
struct WorkItem
{
    std::string id;                  ///< short, [a-z0-9_-]
    std::string title;
    std::string goal;
    std::vector<std::string> files;  ///< the editable-file allowlist, relative paths; disjoint across items
    std::vector<std::string> deps;   ///< items that must be accepted first
    int complexity = 1;              ///< planner's estimate 1..5 (input to the decision policy)
};

struct Plan
{
    std::string task;
    std::vector<WorkItem> items;
    std::vector<std::string> reasons;  ///< why the planner chose this split (or none), kept with the decision record

    [[nodiscard]] std::string toJson() const;
    /// Parse and validate: unique ids, known deps, acyclic, disjoint relative files, 1..32 items.
    [[nodiscard]] static std::expected<Plan, std::string> fromJson(std::string_view text);
    [[nodiscard]] const WorkItem* find(std::string_view id) const;
};

}  // namespace sub0squad
