#pragma once
/// @file agent_impl.hpp
/// Private to the agents layer (llm_agent, canvas_agent, fork_agent, agent_card): what every model-driven worker shares. Not a public header.

#include "sub0squad/engine.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/harness.hpp"
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

/// Everything an attempt's turn loop needs, prepared once by `runLlmAgent` (the card, the best earlier draft and its failure, the guidance).
struct AttemptContext
{
    std::filesystem::path dir;
    std::string itemId, tier, treatment;
    int attempt = 1, maxTokens = 3000, timeoutSec = 1700, repairTurns = 0;
    EngineProfile engine;
    const Plan* plan = nullptr;
    const WorkItem* item = nullptr;
    std::string card;                                  ///< the work card text
    std::string guidance;                              ///< a dispute, a send-down or the manager's note for this attempt ("" when none)
    std::map<std::string, std::string> baselineFiles;  ///< the best earlier draft of each file (empty on a first attempt)
    std::string draftGate, draftOrigin;                ///< that draft's failure text, and the ancestor it came from when it predates a division
    std::chrono::steady_clock::time_point began;
};

/// The canvas turn loop (docs/CANVAS.md slice 0; canvas_agent.cpp): replies are transactions against a revisioned document, the harness precheck gates
/// each commit, the conversation is rebuilt each turn from the card and a folded view of the current revision. Same exit codes as the classic loop.
[[nodiscard]] int runCanvasAttempt(const AttemptContext& a);
/// Apply a parsed reply to `canvas` (whole files, transactions against the current revisions, the older search edits): "" when everything applied, else why
/// not. The caller passes a scratch copy, so a refusal leaves the real canvas untouched (all or nothing across the whole reply).
[[nodiscard]] std::string applyCanvasReply(Canvas& canvas, const CanvasReply& reply, const std::set<std::string>& allowed, const std::string& by);
/// The harness's cheapest check over the canvas's current files: "" when they pass (or the harness has no precheck).
[[nodiscard]] std::string precheckCanvas(const Canvas& canvas, const Harness* harness, const std::filesystem::path& dir, const std::string& itemId, int attempt, const std::filesystem::path& attemptDir);
/// Write `canvas.json` (the log) and every file's current revision under `canvas/` in the attempt directory.
void persistCanvas(const Canvas& canvas, const std::filesystem::path& attemptDir);
/// The fixed system block of a canvas worker (transaction forms, a worked LINES example, the read rule), persona last.
[[nodiscard]] std::string canvasSystemBlock(const std::string& persona, const std::string& brief);
/// A seed derived from the run's base seed and a label (item, attempt, turn), so retries differ and a rerun replays; -1 stays -1.
[[nodiscard]] long long turnSeed(long long base, const std::string& label);

}  // namespace sub0squad::agent
