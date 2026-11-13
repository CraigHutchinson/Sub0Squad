#pragma once
/// @file fork.hpp
/// Forked sections on the canvas (docs/CANVAS.md "Forked sections", docs/WORK_GRAPH.md section 23, docs/BLOCK_PROVING_GROUNDS.md block 6): a primer session
/// writes the skeleton, which becomes revision 1 of a canvas; every stubbed function is a SECTION owned by a session of its own; the sessions run side by
/// side up to the idle slots (throughput by default: `planParallel`); each replies with one transaction on its own function, committed by the coordinator
/// against the skeleton's revision (the canvas rebases it through the others' commits) after the harness precheck; the item's gate judges the merged file
/// and a failure goes back to the sections it names. The model call is a function, so the same code runs against a real engine, a fake HTTP engine or the
/// in-process slot-model engine of the proving ground.

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
    int rounds = 2;                     ///< integration rounds: a failing merged file goes back to the sections the failure names
    EngineProfile engine;
    int parallel = 0;                   ///< 0: up to the idle slots (the default); N: exactly N at once (an experiment)
    std::string sequentialReason;       ///< non-empty: one section at a time, and this is why
    long memFloorMiB = 0;               ///< > 0: below this much available memory forks are shed (one section at a time)
    int skeletonMaxTokens = 1500;       ///< the skeleton is declarations only: its reply is bounded at the server ...
    int skeletonMaxLines = 120;         ///< ... and cancelled while it streams when it runs past this many lines (row 46)
    int wallBudgetSec = 1500;           ///< the whole attempt's wall budget (0 = none): when it is spent no further session starts
    ChatFn chat;                        ///< empty: the engine over HTTP
    std::function<int()> idleSlots;     ///< empty: the engine's `/slots`, else its configured slot count
    std::function<long()> availableMiB; ///< empty: /proc/meminfo
};

struct ForkSection
{
    std::string name, file, symbol;
    double startSec = 0, endSec = 0;    ///< of its first session, from the start of the section phase
    long promptTokens = 0, completionTokens = 0;
    int calls = 0, refused = 0;
    bool committed = false;
};

struct ForkResult
{
    int exitCode = 1;                   ///< 0 the merged file passes its gate; 1 it does not; 2 no usable skeleton
    bool passed = false;
    int width = 1;                      ///< sections generated at once
    std::string widthReason;
    bool shed = false;
    bool budgetSpent = false;           ///< the wall budget ran out before every section had its session
    int skeletonTries = 0;              ///< skeleton replies asked for (1 when the first was usable)
    int rounds = 0;
    int conflicts = 0;                  ///< commits the canvas refused because another section had changed the same lines
    int lostEdits = 0;                  ///< committed sections whose text is not in the merged file (must be 0)
    int refused = 0;                    ///< transactions refused (not the section's own function, the precheck, an unreadable reply)
    long skeletonTokens = 0, sectionTokens = 0, integrationTokens = 0;   ///< prompt and completion tokens per phase
    double wallSec = 0, sectionWallSec = 0, idleShare = 0;
    std::vector<ForkSection> sections;
    std::string error;
    [[nodiscard]] std::string toJson() const;
};

/// Run one item as forked sections on a canvas. Writes the attempt's `out/`, `canvas.json`, the conversation traces and `fork.json`.
[[nodiscard]] ForkResult runCanvasFork(const ForkOptions& options);

}  // namespace sub0squad
