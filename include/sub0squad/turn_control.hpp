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
    int maxCommentRun = 25;        ///< this many comment lines in a row inside a block: reasoning written as comments (35B, 2026-11-18: 140 of 240 lines)
};


/// Why a reply should be cancelled: `kind` is empty when the reply is fine so far.
struct Runaway
{
    std::string kind;              ///< "" | "long-search" | "repeat" | "restate" | "view-echo" | "comment-run" | "rewrite-search" | "big-edit"
    std::string instruction;       ///< what the worker is told in the re-prompt
    /// A loop ("repeat"): how much of the reply is worth keeping, in characters: everything before the loop began, its first copy included (0: nothing).
    /// The agent rolls the reply back to it and asks for the rest with the interception, instead of throwing the reply away.
    std::size_t keep = 0;
    std::string detail = {};       ///< a loop: what repeated ("the same 11 lines 3 times over"), for a message of the caller's own (reasoning loops)
    [[nodiscard]] explicit operator bool() const { return !kind.empty(); }
};

/// Judge the text of a reply so far. `drafts` are the files the worker was shown (path to text); a whole-file block that reproduces one of them is a
/// restatement. Only complete lines are judged, so a verdict never depends on where the stream happened to split.
[[nodiscard]] Runaway detectRunaway(std::string_view replySoFar, const std::map<std::string, std::string>& drafts, const RunawayLimits& limits = {});

/// Where a reply under the narrow scope (canvas: edits first) grows into a rewrite of a file that exists and is not a stub (`locked`, path to text): a
/// SEARCH that copies most of the file (at least 10 lines and `searchShare` of them: the file replaced in disguise), or one edit body (REPLACE, SYMBOL, LINES)
/// past `maxEditLines`. Judged on complete lines; `kind` is "rewrite-search" or "big-edit", or empty. The SEARCH rule applies to files of `minFileLines` or
/// more: in a small file one function is most of the file, and replacing it is an edit of a function's size (35B, 2026-11-18: a 35-line seed, every turn stopped).
[[nodiscard]] Runaway detectEditOverrun(std::string_view replySoFar, const std::map<std::string, std::string>& locked, int maxEditLines = 100,
                                        double searchShare = 0.8, int minFileLines = 60);

/// The continuation without the part that repeats the end of `partial`: the longest suffix of `partial` that is a prefix of `continuation`, when it is at
/// least `minChars` long (row 35: a continuation that began a few tokens before the cut duplicated a string literal).
[[nodiscard]] std::string trimOverlap(std::string_view partial, std::string_view continuation, std::size_t minChars = 8);

/// The text of a continuation reply as it should be appended: a markdown fence around it and a repeated block header at its start removed (the model was
/// asked for "only the rest"), then the overlap with `partial` trimmed.
[[nodiscard]] std::string continuationText(std::string_view partial, const std::string& continuationReply);

/// The whole reply once a continuation is joined to the reply cut off at the token limit (`partial`, its last line unfinished or not). The worker is asked
/// (continuationPrompt) to write the unfinished line again in full, so the join is at a line boundary; what the 35B also sends is handled: the cut line
/// continued from the cut (a token boundary's lost space restored), a few lines before the cut written again (dropped), a stray `===` or fence (dropped), the
/// rest as a SEARCH/REPLACE of the block's tail (spliced). A reply that is a block of its own (read requests, or the block begun again from its first line)
/// replaces the partial: the result is the reply.
[[nodiscard]] std::string joinContinuation(std::string_view partial, const std::string& continuationReply);

/// The re-prompt for a reply cut off at the token limit: its last complete lines (about `tailChars`, whole lines), the unfinished line named, and the
/// instruction to write that line again in full and finish the block.
[[nodiscard]] std::string continuationPrompt(std::string_view partial, std::size_t tailChars = 700);

/// The body of a FILE block cut off part-way (`cut`: a loop rolled back, the worker then moving on to other blocks) made whole from the file's previous
/// revision, so no code on either side is lost (owner, 2026-11-18): the cut text back to its last complete definition, then the functions of `previous`
/// after the last one it reached (by name), in their order. `python` selects indentation instead of braces.
[[nodiscard]] std::string completeCutFile(std::string_view cut, const std::string& previous, bool python);

/// Whether a reply that ended at the token limit stopped inside an EDIT block (its last `=== EDIT:` has no `=== END EDIT ===`): such a reply is continued
/// by appending, like a cut-off file (row 34).
[[nodiscard]] bool cutOffInsideEdit(std::string_view reply);

}  // namespace sub0squad
