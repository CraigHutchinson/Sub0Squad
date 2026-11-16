#pragma once
/// @file stance.hpp
/// How a worker is told what it is, and the one channel it has to object (docs/PROMPTS.md, docs/EXPERIMENTS.md E1).
///
/// The STANCE is the part of a worker's system block that says who the worker is and what standing it has. Two are defined, so they can be compared
/// on the same seeds:
/// * `baseline`: the wording every real run so far used ("the tester on a small software squad ... implement the contract so the provided tests pass").
/// * `specialist`: the worker owns a dedicated, specialised assignment; it is the expert for that piece and is trusted to decide how; the card and the
///   provided tests are claims by other specialists to be checked, not orders; it may contest them, ask, and propose to delegate what is not its own.
///
/// A CONCERN is how a worker contests or asks without leaving the reply format (the worker probe, docs/EXPERIMENTS.md section 7, found that a worker's
/// objection had nowhere to go and was discarded as prose):
/// ```
/// === CONCERN: wrong ===
/// tests/eval.test.cpp:14 expects 2 + 3 * 4 to be 7; the contract's precedence and its own example give 14.
/// === END CONCERN ===
/// ```
/// Concerns are taken out of a reply before the reply is parsed (so they never count as stray prose or break an edit) and recorded with the attempt.
/// Concerns are read under every stance; only `specialist` tells the worker the block exists.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// The stances a worker can be given. `baseline` is the default (new behaviour is off until an A/B says otherwise).
[[nodiscard]] bool isKnownStance(std::string_view stance);

/// Which kind of worker the stance text is written for: one whole piece written as files (the session executor) or as transactions on a canvas (the
/// canvas executor), or one function of a shared file (fork sections).
enum class WorkerShape { files, canvas, section };

/// The stance paragraph of a worker's system block, persona last. For `baseline` this is exactly the text used before stances existed.
[[nodiscard]] std::string stanceText(std::string_view stance, std::string_view persona, WorkerShape shape);

/// The one-paragraph description of the CONCERN block (form, kinds, what happens to it). Part of `specialist`'s text; empty for `baseline`.
[[nodiscard]] std::string concernSyntax(std::string_view stance);

/// One concern a worker raised.
struct Concern
{
    std::string kind;   ///< wrong | contradictory | underspecified | missing | too-big | better-idea (as written, lower-cased; an unknown word is kept)
    std::string text;   ///< the worker's evidence and proposal
};

/// The kinds a concern may name, in the order the prompt lists them.
[[nodiscard]] const std::vector<std::string>& concernKinds();

/// Takes every `=== CONCERN: kind ===` block out of `reply` and returns them in order. A block ends at `=== END CONCERN ===`, at the next line that
/// starts with `=== `, or at the end of the reply. The rest of the reply is left as it was, minus the blocks.
[[nodiscard]] std::vector<Concern> takeConcerns(std::string& reply);

/// Appends `concerns` to `concerns.md` in `attemptDir` (who raised them and on which turn). Returns false when the file cannot be written.
bool recordConcerns(const std::filesystem::path& attemptDir, std::string_view who, int turn, const std::vector<Concern>& concerns);

/// What a worker is told after a reply that held concerns and no work: they are recorded for the owner of what was questioned; nobody answers during
/// this attempt; do the work as well as it can be done, and keep code that is right even where an expectation was questioned.
[[nodiscard]] std::string concernAcknowledgement(const std::vector<Concern>& concerns);

}  // namespace sub0squad
