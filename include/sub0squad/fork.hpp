#pragma once
/// @file fork.hpp
/// Forked sections on the canvas (docs/CANVAS.md "As built" and "Assembly convergence", WORK_GRAPH.md section 23 (archived: docs/ARCHIVE.md), docs/BLOCK_PROVING_GROUNDS.md
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
#include <map>
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
    int skeletonMaxTokens = 1500;       ///< the declaration reply is bounded at the server, with room to reason when thinking is on
    int skeletonMaxLines = 0;           ///< an explicit caller may set a line guard for an experiment
    int wallBudgetSec = 900;            ///< the attempt's wall budget: this base (0 = no budget at all) ...
    int wallPerSectionSec = 150;        ///< ... plus this for every section, so a file of 14 functions is not given the time of a file of 4
    bool fresh = false;                 ///< never resume the previous attempt's canvas: start from a new skeleton
    /// The sections start from the item's CONTRACT (contract.hpp, catalogue row 59): every function the harness's stub of a file defines is a mandatory
    /// section with the contract's signature, added by the harness to the skeleton and to a resumed canvas that lacks it. Off only to reproduce the defect.
    bool contractSections = true;
    /// Every member function a class of the file declares without a body is a section too (row 71), with a generated stub; a skeleton that leaves one
    /// without a section, or overloads a member, is refused. A switch of its own (the contract may be kept while this is off). Off only to reproduce the defect.
    bool memberSections = true;
    /// Thrash control (rows 74 and 75): a refusal that matches a known diagnostic gets its hint, and its mechanical edit where there is one; an identical
    /// consecutive reply is answered by saying so, with the hint, not by the same refusal; after three refusals the section gets one turn under the
    /// `think` strategy before it is given up. Off only to reproduce the defect.
    bool thrashControl = true;
    /// The mend `add-section` (mends.hpp): continue the previous attempt's files whatever it failed on, add `mendSection` (in `mendFile`) as a stubbed
    /// section with the contract's signature (or the file's own prototype of it), put back sections an earlier assembly removed as uncalled, and write ONLY
    /// the new section. Empty: an ordinary attempt.
    std::string mendSection, mendFile;
    std::string stance = "baseline";    ///< what each section's worker is told it is (stance.hpp): baseline | specialist
    ChatFn chat;                       ///< empty: the engine over HTTP
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
    int concerns = 0;                   ///< CONCERN blocks its replies raised (stance.hpp; recorded in the attempt's concerns.md)
    bool committed = false;
    bool removed = false;               ///< nobody called it: the harness removed it from the assembled file
    std::string lastRefusal;            ///< why its last reply was refused ("" when it was kept)
    /// Convergence of its refused turns (catalogue row 74, docs/METRICS.md 4a): a refusal whose first error differs from the turn before is PROGRESS, one
    /// that repeats it is THRASH; `identicalReplies` counts replies that were, text for text, the reply before.
    int progress = 0, thrash = 0, identicalReplies = 0;
    bool thought = false;               ///< three refusals handed it to the `think` strategy (one more turn, reasoning on)
    std::vector<std::string> hinted;    ///< known diagnostics (mends.hpp) whose hint was attached to a retry prompt
    std::vector<std::string> mended;    ///< known diagnostics whose mechanical edit the harness applied to the reply, which then passed
    std::string brief;                  ///< why the harness added this section (a mend): shown to the session that writes it
};

struct ForkResult
{
    int exitCode = 1;                   ///< 0 the merged file passes its gate; 1 it does not; 2 no usable skeleton; 3 a NEAR PASS (it compiles and at most 15 % of at least 10 tests fail: the gate's own rule)
    bool passed = false;
    /// Why it did not pass: "" (passed) | "skeleton" | "budget" (the wall budget ran out) | "sections-missing" (a section was never accepted) |
    /// "compile" (errors remain in sections) | "compile-outside-sections" (errors in the declared structure itself) | "tests" | "mendable" (the gate failed on an error the mends table
    /// knows: the attempt stops, the engine applies the mend).
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
    int routedByMessage = 0;            ///< sessions sent to a section because a failing test raised a message that section produces (row 64)
    int memberSections = 0;             ///< member functions a class declared without a body that the harness made sections (row 71)
    int testsTotal = -1, testsFailed = -1;   ///< of the state the attempt ended on (its best: row 70); -1 when the tests never judged a file
    bool nearPass = false;
    int reverted = 0;                   ///< assembly rounds undone because they left the file worse than the best state, or not compiling
    int contractSections = 0;           ///< contract functions the harness added as sections because the skeleton or the resumed file lacked them
    int conflicts = 0;                  ///< commits the canvas refused because another section had changed the same lines
    int lostEdits = 0;                  ///< committed sections whose text is not in the merged file (must be 0)
    int refused = 0;                    ///< transactions refused (the compile check, an unreadable reply, an edit outside the section)
    long skeletonTokens = 0, sectionTokens = 0, integrationTokens = 0;   ///< prompt and completion tokens per phase
    long promptTokens = 0, completionTokens = 0;   ///< every model call of the attempt, all phases: the job's `tokens.in` and `tokens.out`
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

/// Which sections a failing TEST points at when it names no function (catalogue row 64): a section owns a failure line when one of its own string
/// literals (8 characters or more) occurs in that line. A test that fails with `unexpected exception: Unknown function: sinh` belongs to the function that
/// holds the text "Unknown function: ". `sections` are (name, the function's text); the result maps a section's name to the failure lines it owns.
[[nodiscard]] std::map<std::string, std::string> sectionsOwningFailure(const std::string& gateText, const std::vector<std::pair<std::string, std::string>>& sections);

/// The source of every failing test a gate's text names (`FAIL 6 - name: ...`, `not ok 6 - name`), quoted from `testFiles` (path, text): the
/// assertion a section must satisfy, as written (catalogue rows 25/26). At most 16 lines a test and about 1,600 characters in all; "" when none is found.
[[nodiscard]] std::string failingTestSources(const std::string& gateText, const std::vector<std::pair<std::string, std::string>>& testFiles);

/// One compiler finding in a file: the 1-based line and the message with its context lines.
struct CompileFinding { int line = 0; std::string text; bool unusedFunction = false; };
/// The findings of a compiler's output that are located in `path` (`...path:LINE:COL: error: ...`, GCC and Clang form), in order.
[[nodiscard]] std::vector<CompileFinding> compileFindings(const std::string& output, const std::string& path);

}  // namespace sub0squad
