#pragma once
/// @file division.hpp
/// Harness-side completion of a division written by a small model (docs/WORK_GRAPH.md section 22).

#include "sub0squad/plan.hpp"

#include <string>
#include <vector>

namespace sub0squad {

/// A common slip: the architect writes the tests of a part (tests/lexer.test.mjs) and the parts that use it, and forgets the part that implements it
/// (src/lexer.js). For each file of `parent` that no part delivers and whose tests are in the division, add a developer part graded by those tests, and
/// make the developer parts that waited on the tests wait for it too. Returns a description of each part added.
[[nodiscard]] std::vector<std::string> completeDivision(Plan& fragment, const WorkItem& parent);

}  // namespace sub0squad
