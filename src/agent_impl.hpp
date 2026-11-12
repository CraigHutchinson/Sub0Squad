#pragma once
/// @file agent_impl.hpp
/// Private to the agents layer (llm_agent, canvas_agent, fork_agent, agent_card): what every model-driven worker shares. Not a public header.

#include "sub0squad/engine.hpp"
#include "sub0squad/plan.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sub0squad::agent {

[[nodiscard]] std::string slurp(const std::filesystem::path& p);
bool writeFile(const std::filesystem::path& p, const std::string& text);
/// The value after `--key` in a job's arguments, or `d`.
[[nodiscard]] std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& d = {});
[[nodiscard]] std::string clip(const std::string& s, std::size_t n);

/// The fixed-system-block description of a project harness (empty when the plan has none).
[[nodiscard]] std::string harnessBrief(const std::string& name);
/// The system block: persona-neutral text first (shared by the server's prompt cache), the persona sentence last.
[[nodiscard]] std::string systemBlock(const std::string& persona, const std::string& brief);
/// An earlier draft as the worker sees it: numbered, whole when it fits `budget`, else folded with the regions the gate named left open.
[[nodiscard]] std::string draftView(const std::string& path, const std::string& text, const std::string& gate, std::size_t budget);
/// Names of the tests a test file defines, one `- name` line each.
[[nodiscard]] std::string testNames(const std::string& source);

/// The files of a whole-file reply.
struct Parsed { std::map<std::string, std::string> files; std::size_t rejectedChars = 0, proseChars = 0, fileChars = 0; };
/// Parse `=== FILE: path ===` blocks; tolerate a missing END and markdown fences around the content.
[[nodiscard]] Parsed parseReply(const std::string& reply, const std::set<std::string>& allowed);
/// Ids of every item `id` depends on, directly or not.
[[nodiscard]] std::set<std::string> closure(const Plan& plan, const std::string& id);
/// How an edit-style reply is written (the text the repair prompts end with).
extern const char* const kEditSyntax;
/// The work card as the worker reads it. Counts the tests it showed.
[[nodiscard]] std::string buildCard(const std::filesystem::path& dir, const Plan& plan, const WorkItem& item, const std::string& itemId, const std::string& treatment, long& testCount);

}  // namespace sub0squad::agent
