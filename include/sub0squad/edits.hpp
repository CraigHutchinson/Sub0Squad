#pragma once
/// @file edits.hpp
/// Edit-style replies: instead of writing a whole file again to change a few lines, a worker replies with search and replace blocks (docs/PROMPT_REVIEW.md:
/// only about 19 % of the lines of repair replies had changed). Whole files stay the fallback.

#include <map>
#include <optional>
#include <set>
#include <string>

namespace sub0squad {

/// Apply every `=== EDIT: path ===` block of `reply` (each holding `<<<<<<< SEARCH` / `=======` / `>>>>>>> REPLACE` blocks) to `files`. A SEARCH must match
/// the current text exactly once. Returns the complete updated set of files, or nullopt with `error` naming the edit that did not apply.
[[nodiscard]] std::optional<std::map<std::string, std::string>> applyEdits(const std::string& reply, std::map<std::string, std::string> files,
                                                                            const std::set<std::string>& allowed, std::string& error);

}  // namespace sub0squad
