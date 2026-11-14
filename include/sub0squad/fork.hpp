#pragma once
/// @file fork.hpp
/// Forked sections on the canvas (docs/CANVAS.md "As built" and "Assembly convergence", docs/WORK_GRAPH.md section 23, docs/BLOCK_PROVING_GROUNDS.md
/// block 6). A primer session DECLARES the structure (imports, types, one `@@ signature` line per function) and the HARNESS completes it into a skeleton
/// with placeholder bodies (`expandSkeleton`), which becomes revision 1 of a canvas once it passes the compile check; every stubbed function is a SECTION
/// owned by a session of its own; the sessions run side by side up to the idle slots (throughput by default: `planParallel`); each replies with one
/// transaction on its own function, whose SIGNATURE the harness keeps (a section supplies a body, never a declaration), committed by the coordinator
/// against the revision the section was shown (the canvas rebases it through the others' commits) after the partial compile check.
/// ASSEMBLY then converges by routed findings, not by whole-file rounds: a section that was never accepted is retried alone; a helper nobody calls is
/// removed by the harness; a compile error goes to the owner of the function it is in, with only that error and that function; only when the file
/// compiles strictly do the item's tests judge it. An attempt that ran out of its wall budget is RESUMED by the next one from its canvas, with the
/// accepted sections kept. The model call is a function, so the same code runs against a real engine, a fake HTTP engine or the slot-model engine.

#include "sub0squad/engine.hpp"
#include "sub0squad/throughput.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sub0squad {

/// One chat completion. `guard` (may be empty) is asked while the reply streams and cancels it by returning true: the result then has `finishReason`
/// "aborted". An engine that cannot stream may ignore the guard.
using ChatFn = std::function<std::expected<ChatResult, std::string>(const EngineProfile&, const std::vector<ChatMessage>&, int maxTokens, int timeoutSec, const StreamGuard& guard)>;

struct ForkOptions
{
    std::filesystem::path dir;          ///< the workflow directory
    std::string itemId;
    int attempt = 1, maxTokens = 3500, timeoutSec = 3600;
    int rounds = 2;                     ///< assembly rounds after the first writing phase (each sends only routed findings)
    EngineProfile engine;
    int parallel = 0;                   ///< 0: up to the idle slots (the default); N: exactly N at once (an experiment)
    std::string sequentialReason;       ///< non-empty: one section at a time, and this is why
    long memFloorMiB = 0;               ///< > 0: below this much available memory forks are shed (one section at a time)
    int skeletonMaxTokens = 1500;       ///< the skeleton is declarations only: its reply is bounded at the server ...
    int skeletonMaxLines = 120;         ///< ... and cancelled while it streams when it runs past this many lines (row 46)
    int wallBudgetSec = 900;            ///< the attempt's wall budget: this base (0 = no budget at all) ...
    int wallPerSectionSec = 150;        ///< ... plus this for every section, so a file of 14 functions is not given the time of a file of 4
    bool fresh = false;                 ///< never resume the previous attempt's canvas: start from a new skeleton
    /// The sections start from the item's CONTRACT (contract.hpp, catalogue row 59): every function the harness's stub of a file defines is a mandatory
    /// section with the contract's signature, added by the harness to the skeleton and to a resumed canvas that lacks it. Off only to reproduce the defect.
    bool contractSections = true;
    /// The mend `add-section` (mends.hpp): continue the previous attempt's files whatever it failed on, add `mendSection` (in `mendFile`) as a stubbed
    /// section with the contract's signature (or the file's own prototype of it), put back sections an earlier assembly removed as uncalled, and write ONLY
    /// the new section. Empty: an ordinary attempt.
    std::string mendSection, mendFile;
    ChatFn chat;                        ///< empty: the engine over HTTP
    std::function<int()> idleSlots;     ///< empty: the engine's `/slots`, else its configured slot count
    std::function<long()> availableMiB; ///< empty: /proc/meminfo
};

struct ForkSection
{
    std::string name, file, symbol;
    std::string signature, indent;      ///< the declaration as the skeleton has it: the harness writes it, a section's reply supplies only the body
    double startSec = 0, endSec = 0;    ///< of its first session, from the start of the section phase
    long promptTokens = 0, completionTokens = 0;
    int calls = 0, refused = 0;
    bool committed = false;
    bool removed = false;               ///< nobody called it: the harness removed it from the assembled file
    std::string lastRefusal;            ///< why its last reply was refused ("" when it was kept)
    std::string brief;                  ///< why the harness added this section (a mend): shown to the session that writes it
};

struct ForkResult
{
    int exitCode = 1;                   ///< 0 the merged file passes its gate; 1 it does not; 2 no usable skeleton
    bool passed = false;
    /// Why it did not pass: "" (passed) | "skeleton" | "budget" (the wall budget ran out) | "sections-missing" (a section was never accepted) |
    /// "compile" (errors remain in sections) | "compile-outside-sections" (errors in the declared structure itself) | "tests".
    std::string failure;
    int resumedFrom = 0;                ///< the attempt whose canvas this one continued (0: a new skeleton)
    int width = 1;                      ///< sections generated at once
    std::string widthReason;
    bool shed = false;
    bool budgetSpent = false;
    int budgetSec = 0;                  ///< the wall budget this attempt had (base plus per section)
    int skeletonTries = 0;
    int rounds = 0;                     ///< assembly rounds used
    int targeted = 0;                   ///< targeted sessions in assembly (a finding routed to one section)
    int mechanicalMends = 0;            ///< functions nobody called that the harness removed, and unused names it marked `[[maybe_unused]]`
    int contractSections = 0;           ///< contract functions the harness added as sections because the skeleton or the resumed file lacked them
    int conflicts = 0;                  ///< commits the canvas refused because another section had changed the same lines
    int lostEdits = 0;                  ///< committed sections whose text is not in the merged file (must be 0)
    int refused = 0;                    ///< transactions refused (the compile check, an unreadable reply, an edit outside the section)
    long skeletonTokens = 0, sectionTokens = 0, integrationTokens = 0;   ///< prompt and completion tokens per phase
    double wallSec = 0, sectionWallSec = 0, idleShare = 0;
    std::vector<ForkSection> sections;
    std::string error;
    [[nodiscard]] std::string toJson() const;
};

/// Run one item as forked sections on a canvas. Writes the attempt's `out/`, `canvas.json`, the conversation traces and `fork.json`.
[[nodiscard]] ForkResult runCanvasFork(const ForkOptions& options);

/// A section's reply with the skeleton's declaration put back (the harness owns signatures: run-v9's sections dropped a `Class::` qualifier, changed
/// parameters or lost the indentation, and each such slip breaks the file for everyone). `text` is the function as the model wrote it, or only its
/// body; the result starts with `indent` + `signature` and keeps the model's body. `path` selects the language (braces or indentation).
[[nodiscard]] std::string applySectionSignature(const std::string& text, const std::string& signature, const std::string& indent, const std::string& path);

/// One compiler finding in a file: the 1-based line and the message with its context lines.
struct CompileFinding { int line = 0; std::string text; bool unusedFunction = false; };
/// The findings of a compiler's output that are located in `path` (`...path:LINE:COL: error: ...`, GCC and Clang form), in order.
[[nodiscard]] std::vector<CompileFinding> compileFindings(const std::string& output, const std::string& path);

}  // namespace sub0squad
