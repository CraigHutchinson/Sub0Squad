#pragma once
/// @file blocks.hpp
/// Block proving grounds (docs/BLOCK_PROVING_GROUNDS.md): the isolated base concepts the whole is built from, each with a fixture, a trusted judge and a
/// number. This unit holds the proving grounds that need NO model (reply formats, canvas transactions, the parallel-sections harness on a slot-model
/// engine) and the `block` command, which reports which blocks are proven on this build and archives the table next to the scenario results.

#include "sub0squad/fork.hpp"
#include "sub0squad/throughput.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sub0squad {

// ---- block 1: reply formats ----------------------------------------------------------------------------------------------------------------------
/// One recorded-reply case (a `.case` file): the files before, one reply or several (a reply cut off at the token limit and its continuations), and either
/// the files after or the error the reply must be refused with.
struct ReplyCase
{
    std::string name;
    std::map<std::string, std::string> base, want;
    std::vector<std::string> replies;
    std::string expectError;          ///< non-empty: the reply must be refused with a message containing this, and nothing may change
    int expectReads = -1;             ///< >= 0: the reply must hold exactly this many read requests and no edit
    bool classic = false;             ///< the classic applier (`applyEdits`) must give the same result
    std::vector<std::string> allowed; ///< the files the worker may write (default: those of `base` and `want`)
};
/// Parse a `.case` file (format: tests/data/replies/README.md). `error` names what is wrong with the file.
[[nodiscard]] bool parseReplyCase(const std::string& text, ReplyCase& out, std::string& error);
/// Apply the case's replies as the agent does (continuations joined, the reply read as a canvas transaction) and judge: "" when the outcome is the
/// intended one, else what differs.
[[nodiscard]] std::string runReplyCase(const ReplyCase& c);
struct FixtureReport { int cases = 0, failed = 0; std::vector<std::string> problems; };
/// Every `*.case` under `dir`.
[[nodiscard]] FixtureReport runReplyFixtures(const std::filesystem::path& dir);

// ---- block 5: canvas transactions, model-based -------------------------------------------------------------------------------------------------------
struct ModelCheck { int cases = 0, steps = 0, failures = 0; std::string firstFailure; };
/// Random operation sequences against an independent in-memory model: after EVERY step the canvas text must equal the model's (accepted transactions,
/// stale ones rebased, conflicting and malformed ones refused with nothing changed, reverts), every earlier revision must still be retrievable, and the
/// same operations written as a reply and parsed back must give the same text. Deterministic for a seed.
[[nodiscard]] ModelCheck canvasModelCheck(int cases, unsigned seed);

// ---- block 6: parallel sections on a slot-model engine ---------------------------------------------------------------------------------------------
/// An in-process engine for the proving ground: `answer` gives the reply text for a conversation; the call takes as long as the slot model says (prefill,
/// then decode at the per-sequence speed of however many calls are in flight when it starts), scaled so that one simulated second is `msPerSimSecond`
/// real milliseconds. No sockets, no processes.
[[nodiscard]] ChatFn slotModelEngine(std::function<std::string(const std::vector<ChatMessage>&)> answer, SlotModel model, double msPerSimSecond);

struct ParallelProof
{
    bool ran = false;
    std::string error;
    int sections = 0, width = 0;
    bool sequentialPassed = false, parallelPassed = false;
    bool identical = false;            ///< parity: the parallel run's files equal the sequential run's
    int conflicts = 0, lostEdits = 0;
    double idleShare = 0;              ///< measured on the parallel run
    ParallelMetrics model;             ///< speed-up and coordination share from the slot model over the run's token counts (arithmetic on an assumed curve, not a measurement)
    [[nodiscard]] bool proven() const { return ran && sequentialPassed && parallelPassed && identical && conflicts == 0 && lostEdits == 0; }
};
/// The harness of the parallel-sections proving ground, model-free: the Python calculator engine is generated from the same skeleton one section at a
/// time and `width` at once on a slot-model engine that answers each section from the reference; both merged files go through the real gate.
/// `workDir` receives the two workflow directories.
[[nodiscard]] ParallelProof parallelSectionsProof(const std::filesystem::path& workDir, int width, const SlotModel& model = {});

// ---- the report --------------------------------------------------------------------------------------------------------------------------------
struct BlockState
{
    int id = 0;
    std::string name;
    std::string state;      ///< "proven" | "partly proven" (the model-free part; a part that needs a model remains) | "failed" | "not run" | "by ctest" | "needs model" | "not built"
    std::string evidence;   ///< the number, or what proves it elsewhere
    std::string command;    ///< how to prove it when it was not run here
};
struct BlockOptions
{
    int only = -1;                       ///< one block, or -1 for all
    std::filesystem::path fixtures;      ///< the reply cases (block 1)
    std::filesystem::path workDir;       ///< scratch for block 6
    std::string exe;                     ///< the sub0squad binary, for the gate fixtures of block 2 ("" = block 2 is not run here)
    int modelCases = 1000;
};
[[nodiscard]] std::vector<BlockState> runBlocks(const BlockOptions& options);
/// `sub0squad block list | run [--block N] [--fixtures DIR] [--out DIR] [--cases N] [--exe PATH]`: the table, and `block-results.json` under `--out`
/// (default `out/scenarios`, next to `scenario-results.json`) stamped with `build`. Exit 1 when a block that was run failed.
[[nodiscard]] int runBlockCommand(const std::vector<std::string>& args, const std::string& build);

}  // namespace sub0squad
