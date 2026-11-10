#pragma once
/// @file trace.hpp
/// The conversation trace of one attempt: every message sent, every reply (with the model's reasoning when the server returns it), the numbers
/// of the call and the verdict that followed, appended turn by turn so a job that is killed or times out still leaves what it had done.
/// Written to `items/<node>/attempt-N/conversation.md` and shown in the portal.

#include "sub0squad/engine.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

/// Start (or restart) a trace with the settings of the call.
void traceBegin(const std::filesystem::path& file, const std::string& heading, const EngineProfile& engine, int maxTokens);
/// Messages sent to the model this turn: `from` is the index of the first one not yet traced.
void traceSent(const std::filesystem::path& file, int turn, const std::vector<ChatMessage>& convo, std::size_t from, long long seed);
void traceReply(const std::filesystem::path& file, int turn, const ChatResult& reply);
/// What happened to the reply: the validation error, the gate output, "accepted" and so on.
void traceVerdict(const std::filesystem::path& file, int turn, const std::string& text);

}  // namespace sub0squad
