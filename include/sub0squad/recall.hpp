#pragma once
/// @file recall.hpp
/// Search over what a workflow already did: every attempt's conversation (messages, replies, reasoning), gate outputs, briefings and sizing
/// records. It lets a retry, the harness-expert, an architect or the owner ask "what did the worker see and say when this went wrong?" and gives a
/// later knowledge-gathering agent its raw material (docs/WORK_GRAPH.md section 19). Plain text, deterministic, read only.

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

struct RecallHit
{
    std::string path;   ///< relative to the workflow sandbox, e.g. items/engine/attempt-2/conversation.md
    std::string kind;   ///< conversation | gate | reply | prompt | briefing | sizing | report
    int line = 0;       ///< 1-based
    int score = 0;      ///< how many of the query's words the line holds
    std::string text;   ///< the line, trimmed
};

/// Lines of the workflow's records that hold at least half of the query's words (case-insensitive, words of three letters or more), best first,
/// at most `limit`. `item` (optional) restricts the search to one node's files.
[[nodiscard]] std::vector<RecallHit> recallSearch(const std::filesystem::path& sandbox, const std::string& query, int limit = 40, const std::string& item = {});

/// The tail of an item's latest conversation file (what the worker was last sent and answered), at most `maxChars`; empty when there is none.
[[nodiscard]] std::string recallLastConversation(const std::filesystem::path& sandbox, const std::string& item, std::size_t maxChars = 2200);

}  // namespace sub0squad
