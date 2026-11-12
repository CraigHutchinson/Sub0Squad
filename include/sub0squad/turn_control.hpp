#pragma once
/// @file turn_control.hpp
/// Turn control (docs/INTERVENTIONS.md rows 34 to 36): pure rules over the text of a reply, used while it streams and after it ends. A reply that is going
/// wrong in a known way is cancelled early and re-prompted with the specific instruction; a reply cut off at the token limit is continued by appending, with
/// the overlap between the partial text and its continuation removed. No I/O: every rule is a function of text, so each is tested with recorded replies.

#include <map>
#include <string>
#include <string_view>

namespace sub0squad {

/// When a streaming reply is cancelled. The defaults are the figures the first 35B runs suggested; a limit of 0 turns its rule off.
struct RunawayLimits
{
    int maxSearchLines = 40;       ///< a SEARCH block longer than this is a rewrite in disguise (row 34)
    int maxRepeats = 10;           ///< the same non-trivial line this many times in a row, or the same short block of lines over and over
    int restateMinLines = 30;      ///< a whole-file block is judged once it has this many lines ...
    double restateShare = 0.9;     ///< ... and is a restatement when this share of them equal the draft's lines at the same position
};

/// Why a reply should be cancelled: `kind` is empty when the reply is fine so far.
struct Runaway
{
    std::string kind;              ///< "" | "long-search" | "repeat" | "restate" | "view-echo"
    std::string instruction;       ///< what the worker is told in the re-prompt
    [[nodiscard]] explicit operator bool() const { return !kind.empty(); }
};

/// Judge the text of a reply so far. `drafts` are the files the worker was shown (path to text); a whole-file block that reproduces one of them is a
/// restatement. Only complete lines are judged, so a verdict never depends on where the stream happened to split.
[[nodiscard]] Runaway detectRunaway(std::string_view replySoFar, const std::map<std::string, std::string>& drafts, const RunawayLimits& limits = {});

/// The continuation without the part that repeats the end of `partial`: the longest suffix of `partial` that is a prefix of `continuation`, when it is at
/// least `minChars` long (row 35: a continuation that began a few tokens before the cut duplicated a string literal).
[[nodiscard]] std::string trimOverlap(std::string_view partial, std::string_view continuation, std::size_t minChars = 8);

/// The text of a continuation reply as it should be appended: a markdown fence around it and a repeated block header at its start removed (the model was
/// asked for "only the rest"), then the overlap with `partial` trimmed.
[[nodiscard]] std::string continuationText(std::string_view partial, const std::string& continuationReply);

/// Whether a reply that ended at the token limit stopped inside an EDIT block (its last `=== EDIT:` has no `=== END EDIT ===`): such a reply is continued
/// by appending, like a cut-off file (row 34).
[[nodiscard]] bool cutOffInsideEdit(std::string_view reply);

}  // namespace sub0squad
