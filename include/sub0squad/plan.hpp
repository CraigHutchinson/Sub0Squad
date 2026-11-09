#pragma once
/// @file plan.hpp
/// `plan-1`: the data a planner produces. A plan is validated as data and never executed on trust (docs/DESIGN.md section 6).

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// The whole input of one small item (docs/SQUAD_UX_AND_HARNESS.md section 3).
struct WorkCard
{
    std::string context;                  ///< one or two sentences: why this item exists
    std::string contract;                 ///< the interface, as code or a table
    std::vector<std::string> acceptance;  ///< observable checks
    std::vector<std::string> include;     ///< files from accepted dependency outputs to inline (size capped)
    std::vector<std::string> notes;
    [[nodiscard]] int estimatedTokens() const;  ///< length/4 over every field
};

/// Declarative gate. A plan never carries commands: the harness maps a gate kind to what it runs.
struct GateSpec
{
    std::string kind;                ///< "" (legacy C++ gate) or "node-test"
    std::vector<std::string> tests;  ///< workspace-relative test files
    int minTests = 1;
    bool mustFailOnStub = false;     ///< tester gate: at least 70 % of the tests must FAIL against the harness stubs
};

/// One unit of delegated work.
struct WorkItem
{
    std::string id;                  ///< short, [a-z0-9_-]
    std::string title;
    std::string goal;
    std::vector<std::string> files;  ///< the editable-file allowlist, relative paths; disjoint across items
    std::vector<std::string> deps;   ///< items that must be accepted first
    int complexity = 1;              ///< planner's estimate 1..5 (input to the decision policy)
    std::string persona = "developer";  ///< developer | tester (reviewer is reserved)
    WorkCard card{};
    GateSpec gate{};
};

struct Plan
{
    std::string task;
    std::string harness;               ///< project harness: "" (legacy C++ sandbox) or "web-static"
    std::string project;               ///< project name
    std::vector<WorkItem> items;
    std::vector<std::string> reasons;  ///< why the planner chose this split (or none), kept with the decision record

    [[nodiscard]] std::string toJson() const;
    /// Parse and validate: unique ids, known deps, acyclic, disjoint relative files, 1..32 items, personas write only their own kind
    /// of file, cards within `cardBudgetTokens`. Accepts plan-1 and plan-2.
    [[nodiscard]] static std::expected<Plan, std::string> fromJson(std::string_view text, int cardBudgetTokens = 1500);
    [[nodiscard]] const WorkItem* find(std::string_view id) const;
};

}  // namespace sub0squad
