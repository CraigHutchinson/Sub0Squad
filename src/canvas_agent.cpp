// Canvas slice 0 (docs/CANVAS.md): a single-worker canvas per attempt. The document is the state and a reply is a TRANSACTION against the revision the
// worker was shown. Each turn the conversation is rebuilt from the card, the verdict of the last turn and a folded view of the current revision: it never
// carries files or earlier replies. A transaction is applied to a scratch copy, the harness's cheapest check (compile or parse) gates the commit, and only
// then do the item's tests run. A rejected transaction changes nothing and says why; a commit that makes the tests worse is reverted. Reads are bounded,
// a runaway reply is cancelled while it streams, a reply cut off at the token limit is continued by appending. Off by default (`--canvas 1`).
#include "agent_impl.hpp"
#include "sub0squad/attempt_failure.hpp"
#include "sub0squad/harness_common.hpp"

#include "sub0squad/canvas.hpp"
#include "sub0squad/doc_layers.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/stance.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/turn_control.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace sub0squad::agent {

namespace fs = std::filesystem;

const char* const kCanvasSyntax =
    "You edit a shared document. Each turn you are shown the files as they now stand, with line numbers (`N| text`; the numbers are not part of the file) and long function bodies "
    "FOLDED to `... (N lines folded: a-b; name)`. Reply with one TRANSACTION and nothing else (no explanations, no markdown fences). The forms:\n"
    "=== EDIT: <path> SYMBOL <function name> ===\n<the whole function, from its signature to its last line>\n=== END EDIT ===\n"
    "=== EDIT: <path> ===\n<<<<<<< SEARCH\n<a few lines copied exactly from the file, without the line numbers>\n=======\n<what replaces them>\n>>>>>>> REPLACE\n=== END EDIT ===\n"
    "=== FILE: <path> ===\n<the complete new file>\n=== END FILE ===\n"
    "Pick the form ONCE, by the first rule that fits, and do not reconsider it:\n"
    "1. The file does not exist yet, or is only a stub: FILE, the complete new file. It replaces everything, folded parts included, so do not read them "
    "first. A long file may be written in steps: FILE with its first part, then one SYMBOL edit for each function still to add.\n"
    "2. A whole function is wrong or missing: SYMBOL. It replaces the function of that name, or adds it when there is none. A file that exists is improved "
    "this way, not replaced, even when its approach is wrong: a new design goes in function by function. FILE over it is allowed only once the harness says so.\n"
    "3. A few lines are wrong: SEARCH/REPLACE. The SEARCH part is at most about 15 lines and must occur once in the file (indentation need not match exactly); "
    "the REPLACE part may be empty to delete them.\n"
    "To change a folded function you have not seen, read it first: reply with ONLY read requests, `=== READ: <path> SYMBOL name ===` or "
    "`=== READ: <path> LINES a-b ===` (all you need in one reply; at most two replies may be reads, then you must edit).\n"
    "When several things are wrong (several failing tests, several errors), fix ALL of them in one reply, one edit for each: a reply that fixes one of "
    "several costs a turn per defect. Each edit applies on its own: one that cannot apply is named and the others are kept. A worked example. "
    "The view shows\n"
    "  41|     if (d == 0) return 0;\n"
    "and it must report the error instead. The whole reply is:\n"
    "=== EDIT: src/calc.cpp ===\n<<<<<<< SEARCH\n    if (d == 0) return 0;\n=======\n    if (d == 0) throw std::domain_error(\"division by zero\");\n>>>>>>> REPLACE\n=== END EDIT ===\n"
    "Never copy unchanged text or the numbered view back beyond the few SEARCH lines. Every edit is kept, and checked: when the files do not compile or parse "
    "afterwards you are shown the errors, with the line numbers of the files as they then stand, and fix them with small edits (not a rewrite).\n";

std::string canvasSystemBlock(const std::string& persona, const std::string& brief, const std::string& stance)
{
    std::string s = kCanvasSyntax;
    if (stance == "specialist") s += "(A CONCERN block, described below, may come before the transaction.)\n";
    if (!brief.empty()) s += brief + "\n";
    return s + stanceText(stance, persona, WorkerShape::canvas);
}

long long turnSeed(long long base, const std::string& label)
{
    if (base < 0) return base;
    std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(base);
    for (const char c : label) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    return static_cast<long long>(h & 0x7fffffffull);
}

namespace {
constexpr double kThinkingShare = 0.6;               ///< share of a turn's token budget thinking may use before it is stopped and carried over
constexpr double kCharsPerToken = 3.5;               ///< characters per token, to judge streamed reasoning without a tokenizer
constexpr int kTokensPerLine = 12;                    ///< tokens per line of code (about 40 characters), to tell the worker its reply limit in lines
/// What a thinking turn is asked to reason about (experiment SUB0SQUAD_THINKPLAN): the cause and the plan, never the code itself.
constexpr std::string_view kThinkPlanNote =
    "Before you answer, think about the problem, not the code: what is wrong or missing and why, its root cause, and the plan (which "
    "functions or lines change, in what order, and what each change does). Keep the reasoning short and do not write the code in it: "
    "the code goes in your reply, written once.";
/// The plan turn of "steps": the reply is the plan, the transaction comes next turn.
constexpr std::string_view kPlanStepNote =
    "\nThis turn, reply with your plan only, no edits and no code, at most 150 words: === PLAN === then the plan, then === END PLAN ===. "
    "You write the transaction in the next turn.";
constexpr std::size_t kCarriedReasoningChars = 3000; ///< how much of stopped reasoning comes back with the next turn (its end, where it was heading)

/// The last `n` characters of `s`, starting at a line boundary when one is near.
std::string tailAtLine(const std::string& s, std::size_t n)
{
    if (s.size() <= n) return s;
    std::size_t from = s.size() - n;
    if (const std::size_t nl = s.find('\n', from); nl != std::string::npos && nl - from < 200) from = nl + 1;
    return s.substr(from);
}

/// A diagnostic or refusal with its numbers removed: the same error at moved line numbers gives the same key.
std::string errorKey(const std::string& text)
{
    std::string key;
    for (const char c : text.substr(0, 400)) if (!std::isdigit(static_cast<unsigned char>(c))) key += c;
    return key;
}

/// Every file of the canvas at its current revision, as the worker is told it: "src/a.cpp r3, src/b.cpp r1".
std::string revisionList(const Canvas& canvas)
{
    std::string s;
    for (const auto& path : canvas.paths()) s += (s.empty() ? "" : ", ") + path + " r" + std::to_string(canvas.revision(path));
    return s;
}

/// `=== EDIT: path ===` blocks of SEARCH/REPLACE pairs split into one block per pair, so each can apply on its own. Text that is not a recognisable pair
/// (an unclosed block) is passed on whole, so `applyEdits` names what is wrong with it.
std::vector<std::string> splitSnippets(const std::string& edits)
{
    std::vector<std::string> out;
    std::size_t at = 0;
    while ((at = edits.find("=== EDIT:", at)) != std::string::npos) {
        const std::size_t headEnd = edits.find('\n', at);
        if (headEnd == std::string::npos) { out.push_back(edits.substr(at)); break; }
        const std::string head = edits.substr(at, headEnd - at + 1);
        std::size_t end = edits.find("=== END EDIT ===", headEnd);
        const std::string body = edits.substr(headEnd + 1, (end == std::string::npos ? edits.size() : end) - headEnd - 1);
        std::size_t b = 0;
        bool any = false;
        while (true) {
            const std::size_t s0 = body.find("<<<<<<< SEARCH", b);
            if (s0 == std::string::npos) break;
            const std::size_t r0 = body.find(">>>>>>> REPLACE", s0);
            if (r0 == std::string::npos) { out.push_back(head + body.substr(s0) + "\n=== END EDIT ===\n"); any = true; break; }
            const std::size_t r1 = r0 + std::string_view{">>>>>>> REPLACE"}.size();
            out.push_back(head + body.substr(s0, r1 - s0) + "\n=== END EDIT ===\n");
            any = true;
            b = r1;
        }
        if (!any) out.push_back(head + body + "\n=== END EDIT ===\n");
        at = end == std::string::npos ? edits.size() : end + 1;
    }
    if (out.empty()) out.push_back(edits);
    return out;
}
}  // namespace

/// Tests failing by the gate's text; -1 when it names no count (a pass, or a file that did not build).
int failingTests(const std::string& gateText)
{
    const auto [failed, total] = harnessutil::gateTestCounts(gateText);
    return gateText.rfind("GATE FAIL: ", 0) == 0 ? failed : -1;
}

std::string applyCanvasReply(Canvas& canvas, const CanvasReply& reply, const std::set<std::string>& allowed, const std::string& by, std::vector<std::string>* skipped)
{
    if (!reply.error.empty()) return reply.error;
    // One part that cannot apply: all or nothing returns it as the refusal; partial records it and goes on with the rest.
    const auto fail = [&](const std::string& why) { if (skipped) skipped->push_back(why); return !skipped; };
    std::map<std::string, int> shown;   // the revision each file was at when the worker saw it: every operation's line numbers are in that view
    for (const auto& path : canvas.paths()) shown[path] = canvas.revision(path);
    for (const auto& [path, text] : reply.wholeFiles) {
        if (!allowed.count(path)) { const std::string why = "'" + path + "' is not one of the files you may write"; if (fail(why)) return why; continue; }
        (void)canvas.replaceText(path, text, by);
    }
    for (Transaction tx : reply.transactions) {
        std::string why;
        if (!allowed.count(tx.path)) why = "'" + tx.path + "' is not one of the files you may write";
        else if (!shown.count(tx.path)) why = "there is no '" + tx.path + "' yet: write it whole with === FILE: " + tx.path + " ===";
        else if (reply.wholeFiles.count(tx.path)) why = "'" + tx.path + "' is both rewritten whole and edited in one reply: use one or the other";
        if (!why.empty()) { if (fail(why)) return why; continue; }
        tx.by = by;
        if (!skipped) {   // all or nothing: the file's operations as one transaction
            tx.baseRevision = canvas.revision(tx.path);
            if (const TxResult r = canvas.apply(tx); !r.ok) return tx.path + ": " + r.error;
            continue;
        }
        // Partial: each operation on its own, all against the revision that was shown (the canvas rebases each over the ones applied before it), so one
        // bad operation never costs the good ones beside it.
        for (const auto& op : tx.ops) {
            Transaction one{tx.path, shown[tx.path], by, {op}};
            if (const TxResult r = canvas.apply(one); !r.ok) skipped->push_back(tx.path + ": " + r.error);
        }
    }
    // The snippet form (SEARCH/REPLACE): its result is committed as a whole-text revision. A snippet names its place by its text, not by line numbers, so
    // in partial mode each one applies on its own to the text as it then stands.
    const auto applySnippets = [&](const std::string& edits) {
        std::string err;
        const auto before = canvas.files();
        const auto merged = applyEdits(edits, before, allowed, err);
        if (!merged) return err;
        for (const auto& [path, text] : *merged) if (!before.count(path) || before.at(path) != text) (void)canvas.replaceText(path, text, by);
        return std::string{};
    };
    if (!reply.searchEdits.empty()) {
        if (!skipped) { if (std::string err = applySnippets(reply.searchEdits); !err.empty()) return err; }
        else for (const auto& one : splitSnippets(reply.searchEdits)) if (std::string err = applySnippets(one); !err.empty()) skipped->push_back(err);
    }
    return {};
}

std::optional<std::map<std::string, std::string>> applyAsTransactions(const std::string& reply, const std::map<std::string, std::string>& files, const std::set<std::string>& allowed, const std::string& by, std::string& error)
{
    Canvas canvas;
    for (const auto& [path, text] : files) canvas.addFile(path, text);
    const CanvasReply parsed = parseCanvasReply(reply);
    if (parsed.error.empty() && !parsed.hasEdits()) { error = "the reply holds no edit blocks"; return std::nullopt; }
    error = applyCanvasReply(canvas, parsed, allowed, by);
    if (!error.empty()) return std::nullopt;
    return canvas.files();
}

std::string precheckCanvas(const Canvas& canvas, const Harness* harness, const fs::path& dir, const std::string& itemId, int attempt, const fs::path& attemptDir, bool partial)
{
    if (!harness || !harness->precheck) return {};
    const fs::path candidate = attemptDir / "candidate";
    std::error_code ec;
    fs::remove_all(candidate, ec);
    for (const auto& [path, text] : canvas.files()) (void)writeFile(candidate / path, text);
    std::string bad = harness->precheck({"--workflow-dir", dir.string(), "--item", itemId, "--attempt", std::to_string(attempt), "--files-dir", candidate.string(), "--partial", partial ? "1" : "0"});
    fs::remove_all(candidate, ec);
    return bad;
}

void persistCanvas(const Canvas& canvas, const fs::path& attemptDir)
{
    (void)writeFile(attemptDir / "canvas.json", canvas.logJson() + "\n");
    for (const auto& path : canvas.paths()) {   // every revision is kept, so any of them can be restored or compared afterwards
        std::string flat = path;
        for (char& c : flat) if (c == '/') c = '_';
        (void)writeFile(attemptDir / "canvas" / (flat + ".r" + std::to_string(canvas.revision(path))), canvas.text(path));
    }
}

int runCanvasAttempt(const AttemptContext& a)
{
    const fs::path attemptDir = a.dir / "items" / a.itemId / ("attempt-" + std::to_string(a.attempt)), out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    const std::set<std::string> allowed(a.item->files.begin(), a.item->files.end());
    const Harness* harness = findHarness(a.plan->harness);
    const std::string system = canvasSystemBlock(a.item->persona, harnessBrief(*a.plan), a.stance);

    Canvas canvas;
    for (const auto& [path, text] : a.baselineFiles) canvas.addFile(path, text);
    // No earlier draft: the canvas starts from what the workspace starts with (the harness's stubs, the plan's scaffold), so the first turn edits a file that
    // compiles instead of writing every file from nothing in one reply (tier 1 of 2026-11-17: an 11k-character first draft, cut off three times, refused).
    std::vector<std::string> seeded;
    if (a.baselineFiles.empty())
        for (const auto& [path, text] : startingFiles(harness, *a.plan))
            if (allowed.count(path) && !text.empty()) { canvas.addFile(path, text); seeded.push_back(path); }
    persistCanvas(canvas, attemptDir);

    const fs::path tracePath = attemptDir / "conversation.md";
    traceBegin(tracePath, "Conversation of " + a.itemId + ", attempt " + std::to_string(a.attempt) + " (" + a.tier + ", canvas)", a.engine, a.maxTokens);
    (void)writeFile(attemptDir / "prompt.txt", "SYSTEM:\n" + system + "\nUSER (rebuilt each turn; the card):\n" + a.card);

    // What the next message says about the last turn: the failure of the earlier draft to begin with, then a gate failure, a refusal or the lines a read asked for.
    std::string seededNote;
    for (const auto& p : seeded) seededNote += (seededNote.empty() ? "" : ", ") + p;
    std::string verdict = a.baselineFiles.empty() ? (seeded.empty() ? std::string{} : "These files are what the workspace starts with (" + seededNote + "): change them with edits, or write one whole when it is only a stub.") : (a.draftOrigin.empty() ? "These files are your best earlier draft. It failed:\n" : "These files are an earlier draft from before '" + a.draftOrigin + "' was divided (keep what is right, fix the rest). It failed:\n") + clip(a.draftGate, 1200);
    std::string focus = a.draftGate;      // the text whose file:line references keep regions of the view open
    std::string partial;                  // a reply cut off at the token limit, so far: the next reply is appended to it
    std::string prefill;                  // this turn continues `partial` as the assistant's own message, from this text (empty: not a prefill turn)
    int prefilled = 0;                    // continuations sent as a prefill (the rest fell back to the tail prompt)
    int spent = 0;                        // turns used of repairTurns + 1
    int readReplies = 0, rejects = 0, aborts = 0, continuations = 0, calls = 0, commits = 0, reverts = 0, concernCount = 0, concernOnly = 0;
    const int freeRejects = 3, freeAborts = 2, freeReads = 2, freeContinuations = 3;
    bool verified = false, haveFiles = false, stagnated = false;
    int bestFailing = -1;                 // fewest failing tests of any committed revision, and where it was
    std::map<std::string, int> bestRevision;
    bool bestIsSeed = false;              // the best tested revision is the starting files, not the worker's: a draft is not traded back for them
    std::string lastFinish, lastGate, lastRefusal, lastRefusalKey;
    int sameRefusals = 0;
    // Scope escalation. A file that exists is improved by edits; FILE may replace only a file that does not exist yet or is a stub. When edits stop
    // making progress (kRestructureAfter tested revisions in a row without fewer failing tests), the harness widens the scope: the worker is told the
    // approach may be wrong and FILE is allowed for every file. Tier 0 wrong-approach (2026-11-18): offered a whole rewrite on turn 0, the 35B wrote
    // 300-650-line files past the token limit; stitched from continuations they did not compile, and the attempts went on repairing seams.
    constexpr int kRestructureAfter = 2;
    bool restructure = false;
    int noProgress = 0, restructuredAt = -1;
    // A file cut off part way (its braces still open at its end: T0 half-written, 2026-11-18, 3/3 first attempt before the edits-first scope, 0/3
    // after it: the completion stopped as a big edit over and over). It is named as cut off, with the form that finishes it (the A/B of 2026-11-18 on
    // the 35B, 5 runs an arm: the hint 2/5 in 4-7 turns; treating it as a stub 0/5; lifting the edit size limits 0/5; neither 4/19).
    const auto cutOff = [&](const std::string& path, const std::string& text) { return !isPythonPath(path) && endsInsideBlock(text); };
    const auto isStub = [&](const std::string& path, const std::string& text) {   // the harness's own stub, or a skeleton's `not implemented` bodies
        if (text.find("not implemented") != std::string::npos) return true;
        return harness && harness->scaffoldFile && !text.empty() && text == harness->scaffoldFile(path);
    };
    // After a loop's rollback the worker may move on to other blocks instead of continuing the kept one (35B, 2026-11-18: an EDIT of the file's enum, the
    // step its looping comments had been circling). The kept FILE is not thrown away: it is closed, made whole from the file's current revision (its
    // unreached functions appended), and the new blocks follow it. Empty when the kept text is not an open FILE or the reply continues it.
    const auto keptFileMadeWhole = [&](const std::string& kept, const std::string& next) -> std::string {
        std::size_t h = kept.rfind("\n=== FILE:");
        h = h == std::string::npos ? (kept.rfind("=== FILE:", 0) == 0 ? 0 : std::string::npos) : h + 1;
        if (h == std::string::npos || kept.find("=== END FILE", h) != std::string::npos) return {};
        const auto eol = kept.find('\n', h);
        if (eol == std::string::npos) return {};
        const std::string header = kept.substr(h, eol - h);
        std::istringstream in(next);
        std::string first;
        while (std::getline(in, first) && (first.find_first_not_of(" \t\r") == std::string::npos || first.rfind("```", 0) == 0)) {}
        // a continuation (its close included), or the file begun again
        if (first.rfind("=== ", 0) != 0 || first.rfind("=== END", 0) == 0 || first.substr(0, first.find_last_not_of(" \t\r") + 1) == header) return {};
        std::string path = header.substr(9);
        if (const auto close = path.rfind("==="); close != std::string::npos) path.erase(close);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t\r") + 1);
        std::size_t b = eol + 1;
        if (kept.compare(b, 3, "```") == 0) b = kept.find('\n', b) == std::string::npos ? kept.size() : kept.find('\n', b) + 1;
        const std::string body = kept.substr(b), previous = canvas.has(path) ? canvas.text(path) : std::string{};
        return kept.substr(0, h) + header + "\n" + completeCutFile(body, previous, isPythonPath(path)) + "=== END FILE ===\n" + next;
    };
    // After a tested revision: a revision with fewer failing tests than the best so far is progress; anything else counts towards the escalation.
    const auto scopeRefusalFor = [&](const std::string& path) -> std::string {   // why a FILE over `path` is refused now ("" when it is allowed)
        if (restructure || !canvas.has(path) || isStub(path, canvas.text(path))) return {};
        return "FILE would replace " + path + ", which exists and is not a stub. Improve it with edits instead: SYMBOL for a whole function (replaced, or added "
               "when it is new), SEARCH/REPLACE for a few lines; a new design goes in function by function. The harness allows a restructure once edits stop "
               "making progress.";
    };
    // The second way edits fail to make progress: the worker keeps writing the rewrite and every reply is stopped (2026-11-18, 35B wrong-approach: 0 of 3,
    // every turn stopped). kRestructureAfter scope stops in a row, with no tested revision between them, widen the scope too.
    int scopeStops = 0;
    const auto widen = [&](const std::string& why, int call, std::string& verdictOut) {
        restructure = true;
        restructuredAt = call;
        verdictOut += "\nRESTRUCTURE ALLOWED: " + why + ", so the approach itself may be wrong (its structure, its grammar, how data flows through it). You may "
                      "now replace a file whole: FILE, the complete new file, written in steps when it is long (FILE with its first part, then one SYMBOL edit "
                      "for each function still to add). Edits remain allowed.";
        traceVerdict(tracePath, call, "scope widened: restructure allowed: " + why);
        std::printf("canvas turn %d: restructure allowed (%s)\n", call, why.c_str());
        events::note("restructure allowed: " + why);
    };
    const auto noteProgress = [&](bool improved, int call, std::string& verdictOut) {
        noProgress = improved ? 0 : noProgress + 1;
        scopeStops = 0;
        if (restructure || noProgress < kRestructureAfter) return;
        widen(std::to_string(noProgress) + " tested revisions in a row have not reduced the failing tests", call, verdictOut);
    };
    const auto noteScopeStop = [&](int call, std::string& verdictOut) {
        if (restructure || ++scopeStops < kRestructureAfter) return;
        widen(std::to_string(scopeStops) + " replies in a row were stopped for growing into a rewrite", call, verdictOut);
    };
    double totIn = 0, totOut = 0, decodeTps = 0, prefillTps = 0;
    long changedLines = 0;

    // What the loop does in several places, each said once.
    const auto writeOut = [&] {   // out/ holds exactly the canvas's current files: what the gate judges and what the attempt hands on
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, textNow] : canvas.files())
            if (!writeFile(out / path, textNow)) { std::fprintf(stderr, "llm-agent: cannot write %s\n", path.c_str()); return false; }
        return true;
    };
    const auto selfCheck = [&] {   // the item's gate over out/, not published; afterwards gate.txt (and lastGate) describe out/
        fs::remove(attemptDir / "failures.txt", ec);   // written by a gate whose tests fail: never one of an earlier check
        const int rc = harness->gate({"--workflow-dir", a.dir.string(), "--item", a.itemId, "--attempt", std::to_string(a.attempt), "--no-publish", "1"});
        lastGate = slurp(attemptDir / "gate.txt");
        return rc;
    };
    const auto markBest = [&](int failing, bool seed) {
        bestFailing = failing;
        bestIsSeed = seed;
        for (const auto& path : canvas.paths()) bestRevision[path] = canvas.revision(path);
    };
    const auto restoreBest = [&] {   // back to the best tested revision, as new revisions (the ones undone stay in the history); returns which were undone
        std::string undone;
        for (const auto& [path, rev] : bestRevision)
            if (canvas.has(path) && canvas.revision(path) != rev) { undone += (undone.empty() ? "" : ", ") + path + " r" + std::to_string(canvas.revision(path)); (void)canvas.revertTo(path, rev, "harness"); }
        persistCanvas(canvas, attemptDir);
        return undone;
    };
    if (const int f = failingTests(a.draftGate); f >= 0 && !a.baselineFiles.empty()) markBest(f, false);

    // The issues layer (docs/LAYERED_DOCUMENT.md): a verdict is parsed into issues bound to stable block ids, the worker is shown their summary (every
    // failure, each with the function it is in or likely in), the first few in full with their functions' code (slice 2), and reads any one in full with
    // `=== READ: ISSUE n ===`. SUB0SQUAD_ISSUE_LAYER picks the mode, so one build is measured every way: 0 off (the gate's text), 1 always on, 2 (the
    // default) adaptive: on only where the gate's text is clipped, by length or by more than kAdaptiveIssues failures, since on tier 0 the summary helped
    // long, clipped verdicts and cost short ones. SUB0SQUAD_ISSUE_INLINE is how many issues are shown in full (default 3; 0 is slice 1's summary alone).
    // kAdaptiveIssues is the gate's own excerpt (gate.txt keeps six failures): at 3, adaptive showed the layer for fix-many's five short failures, which
    // the gate's text already held, and cost it as much as always-on (docs/LAYERED_DOCUMENT.md, "Slice 2 on tier 0").
    constexpr std::size_t kAdaptiveIssues = 6;
    const auto envInt = [](const char* name, int fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return fallback;
        char* end = nullptr;
        const long n = std::strtol(v, &end, 10);
        return end && *end == '\0' && n >= 0 && n <= 100 ? static_cast<int>(n) : fallback;
    };
    const int issueMode = std::min(envInt("SUB0SQUAD_ISSUE_LAYER", 2), 2);
    const bool issueLayer = issueMode != 0;
    const auto inlineIssues = static_cast<std::size_t>(envInt("SUB0SQUAD_ISSUE_INLINE", 3));
    BlockIndex blockIndex;
    std::vector<LayerItem> issues;
    int issueReads = 0;
    int layerShown = 0, layerPassed = 0;   // failing verdicts shown through the layer, and those adaptive mode left as the gate's text
    // The verdict for a failing gate (`tests`: the gate ran the tests; else `text` is the compile or parse check's output) and the focus that keeps the
    // anchored functions open. Without the layer, or where adaptive mode finds the gate's own text enough: the clipped text, as before.
    const auto layeredVerdict = [&](const std::string& text, bool tests, std::size_t clipAt, std::string& focusOut) {
        focusOut = text;
        if (!issueLayer) return clip(text, clipAt);
        blockIndex.setTurn(calls);
        for (const auto& [path, textNow] : canvas.files()) blockIndex.update(path, textNow);
        const std::string all = tests ? slurp(attemptDir / "failures.txt") : std::string{};
        std::map<std::string, std::string> testSources;   // the item's tests as the gate ran them: a test is localised from its own function's expressions
        for (const auto& t : a.item->gate.tests) if (const std::string src = slurp(attemptDir / "ws" / t); !src.empty()) testSources[t] = src;
        issues = parseIssues(all.empty() ? text : all, blockIndex, testSources);
        std::map<std::string, int> revisions;
        for (const auto& path : canvas.paths()) revisions[path] = canvas.revision(path);
        (void)writeFile(attemptDir / "layers" / ("turn-" + std::to_string(calls) + ".json"), layeredJson(calls, canvas.files(), revisions, blockIndex, {{"issues", issues}}) + "\n");
        if (issues.empty()) return clip(text, clipAt);
        if (issueMode == 2 && text.size() <= clipAt && issues.size() <= kAdaptiveIssues) { ++layerPassed; return text; }   // it fits: sent unchanged
        ++layerShown;
        focusOut = text + "\n" + layerFocus(issues, blockIndex);
        const std::string summary = layerSummary(issues, blockIndex);
        const std::string inlined = layerInline(issues, blockIndex, canvas.files(), inlineIssues);
        if (tests) return text.substr(0, text.find('\n') + 1) + summary + inlined;   // the gate's count line, every failure, the first few in full
        // Diagnostics keep the compiler's own words (source lines and carets) unless every one is already shown in full.
        const bool allInline = !inlined.empty() && inlined.rfind("In full:", 0) == 0;
        return summary + inlined + (allInline ? std::string{} : "The compiler's own words:\n" + clip(text, clipAt));
    };

    // EXPERIMENT (SUB0SQUAD_FIXSITE=1, the A/B of 2026-11-18): a compile verdict says the cause may be away from the lines the errors name. T0
    // undeclared-member/-many (T1's check_eol): the 35B edited the definition and the call the errors named, never the declaration the class lacked.
    // EXPERIMENT SUB0SQUAD_THINKPLAN: "1" asks a thinking turn to reason about the cause and the plan, not the code; "steps" also splits it into a
    // plan turn and an act turn. Unset: neither.
    const std::string thinkPlanMode = [] { const char* v = std::getenv("SUB0SQUAD_THINKPLAN"); return std::string{v ? v : ""}; }();
    bool planStep = false;      // this turn asks for the plan only
    std::string carriedPlan;    // the plan the last turn wrote, for this turn to carry out
    int plans = 0;
    const bool fixSite = [] { const char* v = std::getenv("SUB0SQUAD_FIXSITE"); return v && std::string_view{v} == "1"; }();
    // The owner's wording (2026-11-18): every error reviewed, the root cause addressed directly, the how left to the worker.
    const std::string causeNote = "Review all of these errors together before you edit: several may come from one root cause, and an error is reported "
                                  "where the compiler noticed it, which is not always where the cause is. Address the root cause directly; how is yours "
                                  "to decide.\n";

    // A seeded canvas is judged before the first turn, so the worker starts from what fails (a fix-one-bug task names its bug; stubs say every test fails)
    // instead of having to edit before it can see anything. It is also the first best revision: a commit worse than the seed is undone.
    // Starting files that do not compile say so with their errors, in the numbering the worker is shown (else its first sight of the problem is a
    // refusal of its own edit).
    bool headCompiles = true;
    const bool seedComplete = !seeded.empty() && harness && seeded.size() == a.item->files.size();
    const std::string seedBad = seedComplete ? precheckCanvas(canvas, harness, a.dir, a.itemId, a.attempt, attemptDir) : std::string{};
    if (!seedBad.empty()) {
        headCompiles = false;
        verdict += "\nThey do not compile as they stand:\n" + layeredVerdict(seedBad, false, 1500, focus);
        if (fixSite) verdict += "\n" + causeNote;
        traceVerdict(tracePath, -1, "the starting files were checked before the first turn and do not compile: " + clip(seedBad, 600));
    }
    if (seedComplete && seedBad.empty() && harness->gate) {
        (void)writeOut();
        const int rc = selfCheck();
        traceVerdict(tracePath, -1, "the starting files were checked before the first turn: " + (rc == 0 ? std::string{"the item's tests pass"} : lastGate));
        if (rc == 0) verified = haveFiles = true;
        else {
            if (const int f = failingTests(lastGate); f >= 0) markBest(f, true);
            verdict += "\nThe tests were run on them as they stand and failed:\n" + layeredVerdict(lastGate, true, 1500, focus);
        }
    }
    std::string lastReadAnswer;           // the latest read answered, kept in view when a read past the allowance is refused
    // What the worker read stays open in the view for the rest of the attempt (`path:line` references, the form the folded view's focus takes), and a
    // read of lines already read on an unchanged revision is named as such (tier 1 of 2026-11-18: only the latest answer stayed in view, the regions
    // folded again, and the 35B read the same two ranges at least six times, its reads used up and turns spent).
    std::string readFocus;
    std::vector<ReadRequest> readKept;    // the line and symbol reads of this attempt, answered again when the view cannot keep them open
    std::string readCarried;              // this turn's re-answered reads (empty when the view holds them all)
    std::map<std::string, std::pair<int, int>> readBefore;   // "path LINES a-b" | "path SYMBOL name" -> (turn, revision of the file then)
    bool verdictIsRead = false;           // the verdict is a read answer (set where it is written, cleared once a message carried it)
    std::string carriedReasoning;         // the end of reasoning that ran out before an edit, shown with the next turn
    std::string carriedLoop;              // ... and what repeated, when the reasoning was stopped in a loop (rolled back to where it began)
    bool decideNext = false;              // the next turn writes down what was reasoned, with thinking off
    std::string loopKept;                 // a loop stop: the reply up to where the loop began (Runaway::keep), set by the stream guard
    std::string interception;             // the next turn resumes `partial` after a loop: the reply so far in full, then this instruction
    int loopStreak = 0;                   // loop stops in a row: the first is rolled back and continued, a second starts afresh
    bool rolledBack = false;              // `partial` is a loop's rollback: a reply that moves on to other blocks keeps it, made whole (completeCutFile)
    int thinkingStops = 0;
    // Staged revisions: a transaction that does not compile is still KEPT (as revisions with its diagnostics), never discarded; it is fixed by the next
    // edits. The out/ directory holds only revisions that compile, and the gate runs only on those.
    int staged = 0, sameStaged = 0;
    std::string lastStagedKey;
    while (!verified && spent <= a.repairTurns) {
        // ---- the message: the card, the verdict, and the current revision folded to fit what the window has left ----
        // A verdict is clipped to 2,400 characters (a gate's output, a refusal), but a READ answer is what the worker asked to see: it gets the window
        // left after a minimal view of every file. Clipped like a gate output, the worker never saw the lines it read and asked again until its turns ran
        // out (tier 1 of 2026-11-17: attempts 2 and 4 only ever read, and committed nothing).
        const long availTokens = static_cast<long>(a.engine.contextTokens) - a.maxTokens - 300;
        const long windowChars = static_cast<long>(static_cast<double>(availTokens) * 3.2) - static_cast<long>(system.size() + a.card.size() + a.guidance.size()) - 1500;
        const std::size_t verdictCap = verdictIsRead
            ? static_cast<std::size_t>(std::max(2400L, windowChars - 800L * static_cast<long>(std::max<std::size_t>(1, canvas.paths().size()))))
            : 2400;
        std::string state;
        if (canvas.paths().empty()) state = "No file exists yet. Write every file whole: === FILE: <path> === ... === END FILE ===\n";
        else {
            const auto viewWith = [&](long availChars) {
                const std::size_t perFile = static_cast<std::size_t>(std::clamp(availChars / std::max<long>(1, static_cast<long>(canvas.paths().size())), 800L, 9000L));
                std::string s;
                for (const auto& path : canvas.paths()) {
                    const std::string& text = canvas.text(path);
                    s += "=== CANVAS: " + path + " (revision " + std::to_string(canvas.revision(path)) + ", " + std::to_string(std::count(text.begin(), text.end(), '\n')) + " lines) ===\n" + draftView(path, text, focus + "\n" + readFocus, perFile) + "=== END CANVAS ===\n";
                }
                return s;
            };
            const long availChars = windowChars - static_cast<long>(std::min(verdict.size(), verdictCap));
            state = viewWith(availChars);
            // A short window has no room to keep what was read open (T0 at ctx 6144: every body folded again, so the worker reads the same lines again). The
            // regions read in this attempt that the view folded are answered again from the files as they now stand and shown above the view, which folds
            // further to make room: what was read is never lost from sight, whatever the window.
            readCarried.clear();
            if (!verdictIsRead) {
                std::set<std::string> done;
                for (const auto& rq : readKept) {
                    const std::string key = rq.path + " " + rq.symbol + " " + std::to_string(rq.from) + "-" + std::to_string(rq.to);
                    if (!canvas.has(rq.path) || !done.insert(key).second) continue;
                    const std::string ans = answerRead(rq, canvas.text(rq.path), isPythonPath(rq.path));
                    std::string last;   // the answer's last numbered line: in the view when the region is open there
                    std::istringstream in(ans);
                    for (std::string l; std::getline(in, l);) if (l.find("| ") != std::string::npos && l.find("| ") < 8) last = l;
                    if (!last.empty() && state.find(last) == std::string::npos) readCarried += ans;
                }
                if (!readCarried.empty()) {
                    readCarried = "The lines you read earlier in this attempt, as they now stand (the view below has no room to keep them open):\n" + clip(readCarried, 4000);
                    state = viewWith(availChars - static_cast<long>(readCarried.size()));
                }
            }
            for (const auto& f : a.item->files) if (!canvas.has(f)) state += "(" + f + " does not exist yet: write it whole with === FILE: " + f + " ===)\n";
            // A cut-off file is named as such, with the form that finishes it under the edits-first scope (without it the 35B's first moves were a
            // 43-line SEARCH of the cut region and a 165-line completion, both stopped; appending after the last line never came up).
            for (const auto& path : canvas.paths())
                if (const std::string& t = canvas.text(path); cutOff(path, t)) {
                    const auto n = std::to_string(std::count(t.begin(), t.end(), '\n') + (t.empty() || t.back() == '\n' ? 0 : 1));
                    state += "(" + path + " is cut off: it ends at line " + n + " inside a block that is never closed. Finish it by appending only what is "
                             "missing after its last line: === EDIT: " + path + " AFTER " + n + " === then the rest of the code, then === END EDIT ===)\n";
                }
        }
        const std::string user = a.card + (a.guidance.empty() ? std::string{} : "\n" + a.guidance + "\n") + (verdict.empty() ? std::string{} : "\n" + clip(verdict, verdictCap) + "\n") + "\n" + readCarried + state +
                                 (canvas.paths().empty() ? "" : "Reply with the transaction that fixes EVERYTHING listed above (every failing test, every error, one edit each), its form picked once by the rules above: " +
                                                                    std::string{restructure ? "a few functions wrong, edit just them; the approach wrong, the complete new file (in steps when it is long).\n"
                                                                                            : "a few lines wrong, SEARCH/REPLACE; a function wrong or missing, SYMBOL (as many as it takes; FILE only for a file that is new or a stub).\n"} +
                                                                    // The worker cannot know the reply limit, and its overrun was not thoroughness (T0 write-engine, 2026-11-18, docs/BACKLOG.md:
                                                                    // the same features as the reference at 1.7 times the code, through per-case copies, dead code and reasoning in comments).
                                                                    // So the ask is quality, never fewer checks: decide the design first, each rule in one place.
                                                                    "A reply holds about " + std::to_string(a.maxTokens / kTokensPerLine) + " lines at most. Decide the design before you write; "
                                                                    "keep each rule in one place (a shared helper, not a copy per case), no unused code, no reasoning in comments; keep every check "
                                                                    "the contract needs. A longer file is written in steps (its first part, then the remaining functions as SYMBOL edits).\n");
        verdictIsRead = false;
        // Reasoning that ran out before an edit is kept, not thrown away: its end comes back with this turn, which runs with thinking off so the decision
        // already reasoned out is written down (the 35B runs: 19 turns, 114k tokens of thinking that never reached an edit).
        // A loop in the thinking is the same, rolled back to where the loop began, and the decision asked for names the loop: the "change strategy"
        // step of the loop ladder, with a fresh decoding state and no penalty (docs/BACKLOG.md, Loops).
        const std::string carried = carriedReasoning.empty() ? std::string{}
            : !carriedLoop.empty()
            ? "\nYour reasoning in the last turn went round in a loop (" + carriedLoop + ") and was stopped where the loop began. What came before it is kept. Its end:\n[reasoning]\n" +
                  carriedReasoning + "\n[end of reasoning]\nDo not reason over it again. Pick one approach (a different one if the loop was going nowhere) and write the transaction now.\n"
            : "\nYour reasoning from the last turn is kept: it ran out before you wrote an edit. Its end:\n[reasoning]\n" + carriedReasoning +
              "\n[end of reasoning]\nDo not start the reasoning over. Write the transaction it was leading to now.\n";
        carriedReasoning.clear();
        carriedLoop.clear();
        const std::string planned = carriedPlan.empty() ? std::string{}
            : "\nYour plan from the last turn:\n" + carriedPlan + "\nCarry it out now: reply with the transaction it describes (the forms above), written once.\n";
        carriedPlan.clear();
        std::vector<ChatMessage> convo = {{"system", system}, {"user", user + carried + planned}};
        // A reply cut off at the token limit is continued as itself: its text so far goes back as the assistant's own unfinished message (a prefill), cut to
        // its last complete line, so the model carries on token by token, seeing everything it wrote (2026-11-18: shown only a 700-character tail, the 35B
        // lost its own design and wrote a second engine after the cut; a prefill ending mid-token, `ret`, is ended at once, one at a line end is continued).
        // Prompt, partial and the reply's budget must fit the context; when they do not, the tail is shown and the rest asked for (the fallback).
        prefill.clear();
        if (!partial.empty() && !interception.empty()) {   // after a loop: the reply rolled back to where the loop began, shown whole, then the interception
            convo.push_back({"assistant", partial});
            convo.push_back({"user", interception + "\nYour reply so far is above, rolled back to where the loop began; it is kept. Continue it from its end: "
                                                    "only what is still missing, nothing already written, no header, no fence, and finish it."});
            interception.clear();
        } else if (!partial.empty()) {
            const auto nl = partial.rfind('\n');
            const std::string atLine = nl == std::string::npos ? std::string{} : partial.substr(0, nl + 1);
            const auto estimate = static_cast<int>(static_cast<double>(system.size() + user.size() + carried.size() + atLine.size()) / kCharsPerToken) + a.maxTokens;
            if (!atLine.empty() && estimate < a.engine.contextTokens) {
                prefill = atLine;
                convo.push_back({"assistant", prefill});
            } else {
                convo.push_back({"assistant", "[your reply so far is kept; it was cut off at the token limit]"});
                convo.push_back({"user", continuationPrompt(partial)});   // resumed at a line boundary: mid-line resumes lost spaces and lost their place
            }
        }

        EngineProfile turnEngine = a.engine;
        turnEngine.seed = turnSeed(a.engine.seed, a.itemId + "#" + std::to_string(a.attempt) + "#" + std::to_string(calls));
        const bool decide = decideNext;   // this turn writes down what the last one reasoned: no new reasoning
        decideNext = false;
        if (decide || !prefill.empty()) turnEngine.thinking = false;   // a prefill continues the answer: the template closes the reasoning before it
        // EXPERIMENT (SUB0SQUAD_THINKPLAN=1, owner 2026-11-19): a thinking turn reasons about the cause and the plan, and writes the code only in its reply.
        // The 35B drafted the whole file in its reasoning (T0 think-room: every stop at the cap, the carried tails mid-function), so the thinking never
        // ended, was cut, and the code was written again from its last 3000 characters.
        // "steps" (owner, 2026-11-19: think, plan, act): the thinking turn replies with its plan only (no turn used); the next turn, thinking off,
        // writes the transaction from that plan.
        planStep = turnEngine.thinking && thinkPlanMode == "steps" && partial.empty();
        if (turnEngine.thinking && !thinkPlanMode.empty())
            for (auto it = convo.rbegin(); it != convo.rend(); ++it)
                if (it->role == "user") {
                    it->content += "\n" + std::string{kThinkPlanNote} + (planStep ? std::string{kPlanStepNote} : std::string{});
                    break;
                }
        events::progress(spent, a.repairTurns + 1, calls == 0 ? "generating" : "editing");
        traceSent(tracePath, calls, convo, 0, turnEngine.seed);
        const fs::path livePath = attemptDir / "live.txt";
        (void)writeFile(livePath, "(turn " + std::to_string(calls) + " sent; waiting for the first token)\n");
        bool liveStarted = false, inReasoning = false;
        std::size_t reasoningChars = 0, reasoningJudged = 0;
        std::string reasoningSoFar;
        const DeltaSink liveSink = [&](std::string_view content, std::string_view reasoning) {
            reasoningChars += reasoning.size();
            reasoningSoFar += reasoning;
            std::ofstream o(livePath, std::ios::app | std::ios::binary);   // streamed in pieces as tokens arrive
            if (!liveStarted) { o.close(); (void)writeFile(livePath, ""); o.open(livePath, std::ios::app | std::ios::binary); liveStarted = true; }
            if (!reasoning.empty()) { if (!inReasoning) { o << "[thinking]\n"; inReasoning = true; } o << reasoning; }
            if (!content.empty()) { if (inReasoning) { o << "\n[answer]\n"; inReasoning = false; } o << content; }
        };
        // Early abort: the reply is judged while it streams and cancelled as soon as it is going wrong in a known way (twice at most per attempt). Thinking
        // that passes its share of the budget with no edit begun is stopped too, with room left: what it reasoned is carried into the next turn.
        Runaway runaway;
        const auto drafts = canvas.files();
        std::map<std::string, std::string> locked;   // under the narrow scope: the files that exist and are not stubs, improved by edits of a function's size
        if (!restructure) for (const auto& [p, t] : drafts) if (!isStub(p, t)) locked[p] = t;
        const std::size_t thinkingCap = turnEngine.thinking ? static_cast<std::size_t>(a.maxTokens * kThinkingShare * kCharsPerToken) : 0;
        const StreamGuard guard = [&](const std::string& streamed) {
            // The whole reply so far: a prefill turn's stream repeats the prefill (llama.cpp); a fallback continuation is appended to the partial.
            const std::string soFar = !prefill.empty() ? (streamed.rfind(prefill, 0) == 0 ? streamed : prefill + streamed) : partial + streamed;
            if (thinkingCap > 0 && reasoningChars > thinkingCap && soFar.find("=== ") == std::string::npos) { runaway.kind = "thinking"; return true; }
            // Thinking that goes round in a loop (the same verbatim lines, as in a reply) is stopped where the loop began, not at the cap: what came
            // before is carried, and the next turn decides with thinking off (judged every 400 characters of reasoning: the rule walks the whole text).
            if (thinkingCap > 0 && reasoningSoFar.size() >= reasoningJudged + 400 && soFar.find("=== ") == std::string::npos) {
                reasoningJudged = reasoningSoFar.size();
                if (auto r = detectRunaway(reasoningSoFar, {}); r.kind == "repeat") { runaway = {"thinking-loop", r.instruction, r.keep, r.detail}; return true; }
            }
            // The scope rules never run out (2026-11-18: after two free stops the 35B's third rewrite went through, ran past the token limit and failed
            // to apply); a scope stop past the free aborts costs a turn instead.
            // A FILE header over a file the scope does not yet open: stopped at the header, before thousands of tokens of rewrite are spent on it.
            for (std::size_t at = soFar.find("=== FILE:"); at != std::string::npos; at = soFar.find("=== FILE:", at + 1)) {
                const std::size_t eol = soFar.find('\n', at);
                if (eol == std::string::npos) break;
                std::string path = soFar.substr(at + 9, eol - at - 9);
                if (const auto close = path.rfind("==="); close != std::string::npos) path.erase(close);
                path.erase(0, path.find_first_not_of(" \t"));
                path.erase(path.find_last_not_of(" \t\r") + 1);
                if (auto why = scopeRefusalFor(path); !why.empty()) { runaway = {"scope", "Your reply was stopped: " + why}; return true; }
            }
            // The same rewrite in disguise (35B, 2026-11-18): a SEARCH of the whole file with a 600-line REPLACE, or one edit growing past a function's size.
            if (!locked.empty()) if (runaway = detectEditOverrun(soFar, locked); runaway) return true;
            // After a rollback the kept text was judged when it was written: it is not a restatement now (its head may be the draft's, as written).
            runaway = detectRunaway(soFar, rolledBack ? std::map<std::string, std::string>{} : drafts);
            if (runaway && aborts >= freeAborts && runaway.kind != "repeat") runaway = {};   // a loop is always stopped (past the free aborts it costs a turn)
            if (runaway.kind == "repeat") loopKept = soFar.substr(0, std::min(runaway.keep, soFar.size()));
            return static_cast<bool>(runaway);
        };
        auto reply = chatCompleteMessages(turnEngine, convo, a.maxTokens, a.timeoutSec, liveSink, guard);
        { std::error_code lec; fs::remove(livePath, lec); }
        const int call = calls++;
        if (!reply) {
            traceVerdict(tracePath, call, "the engine call failed: " + reply.error());
            std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str());
            if (!haveFiles) { recordAttemptFailure(attemptDir, "agent", "engine", "the engine call of turn " + std::to_string(call) + " failed: " + reply.error()); return 2; }
            break;
        }
        traceReply(tracePath, call, *reply);
        (void)writeFile(attemptDir / ("reply-" + std::to_string(call) + ".txt"), reply->content);
        (void)writeFile(attemptDir / "reply.txt", reply->content);
        totIn += static_cast<double>(reply->promptTokens);
        totOut += static_cast<double>(reply->completionTokens);
        if (reply->predictedMs > 0) decodeTps = static_cast<double>(reply->completionTokens) / (reply->predictedMs / 1000.0);
        if (reply->promptMs > 0) prefillTps = static_cast<double>(reply->promptTokens) / (reply->promptMs / 1000.0);
        lastFinish = reply->finishReason;
        std::printf("canvas turn %d: %ld prompt tokens, %ld completion tokens, finish=%s\n", call, reply->promptTokens, reply->completionTokens, reply->finishReason.c_str());

        if (reply->finishReason == "aborted" && runaway.kind == "thinking-loop") {   // a loop in the thinking: rolled back to where it began, then decided
            if (++thinkingStops > 1) ++spent;
            const std::string& r = reply->reasoning.size() >= reasoningSoFar.size() ? reply->reasoning : reasoningSoFar;
            carriedReasoning = tailAtLine(r.substr(0, std::min(runaway.keep, r.size())), kCarriedReasoningChars);
            carriedLoop = runaway.detail;
            decideNext = true;
            traceVerdict(tracePath, call, "thinking stopped in a loop (" + runaway.detail + "): rolled back to where it began and carried into the next turn, which runs with thinking off");
            std::printf("canvas turn %d: thinking loop (%s), carried over\n", call, runaway.detail.c_str());
            events::note("thinking loop stopped and carried over");
            continue;
        }
        if (reply->finishReason == "aborted" && runaway.kind == "thinking") {   // stopped thinking: kept, and written down next turn (the verdict stands)
            if (++thinkingStops > 1) ++spent;
            carriedReasoning = tailAtLine(reply->reasoning, kCarriedReasoningChars);
            decideNext = true;
            traceVerdict(tracePath, call, "thinking stopped after " + std::to_string(reasoningChars) + " characters with no edit begun: the reasoning is carried into the next turn, which runs with thinking off");
            std::printf("canvas turn %d: thinking stopped (%zu chars), carried over\n", call, reasoningChars);
            events::note("thinking stopped and carried over");
            continue;
        }
        if (reply->finishReason == "aborted") {   // cancelled while streaming: nothing is applied, the worker is told exactly what to do instead
            if (++aborts > freeAborts) ++spent;   // only a scope stop or a loop gets here once the free aborts are used
            loopStreak = runaway.kind == "repeat" ? loopStreak + 1 : 0;
            partial.clear();
            verdict = runaway.instruction;
            // A loop is caught after the fact and rolled back, not thrown away (owner, 2026-11-18): what came before it (its first copy included) is kept
            // when it holds a block, and the next turn continues it with the interception. No repetition penalty: in code, repetition is often right.
            // The second loop in a row is not rolled back: the kept text led straight back into it (Gemini CLI's loop service likewise recovers once
            // with feedback, then stops).
            if (loopStreak == 1 && loopKept.find("=== ") != std::string::npos) {
                partial = loopKept;
                rolledBack = true;
                interception = verdict;
                verdict.clear();
            } else if (loopStreak > 1) {
                verdict += "\nThis is the second loop in a row, so the reply is not kept this time: start it afresh, built a different way.";
            }
            loopKept.clear();
            if (runaway.kind == "scope" || runaway.kind == "rewrite-search" || runaway.kind == "big-edit") noteScopeStop(call, verdict);
            traceVerdict(tracePath, call, "cancelled while streaming (" + runaway.kind + ") after " + std::to_string(reply->content.size()) + " characters");
            std::printf("canvas turn %d: cancelled early (%s)\n", call, runaway.kind.c_str());
            events::note("reply cancelled early: " + runaway.kind);
            continue;
        }
        loopStreak = 0;
        std::string text;
        if (!prefill.empty()) {   // the server's reply repeats the prefill (llama.cpp): what follows it is the continuation, joined at the line it was cut back to
            const std::string& c = reply->content;
            text = prefill + (c.rfind(prefill, 0) == 0 ? c.substr(prefill.size()) : c);
            ++prefilled;
        } else if (const std::string whole = rolledBack ? keptFileMadeWhole(partial, reply->content) : std::string{}; !whole.empty()) {
            text = whole;
        } else {
            text = partial.empty() ? reply->content : joinContinuation(partial, reply->content);
        }
        rolledBack = false;
        // The plan turn's reply: the plan is carried into the act turn (thinking off), and the verdict it answered stands. A reply that went straight to
        // edits is taken as the transaction.
        if (planStep && text.find("=== EDIT") == std::string::npos && text.find("=== FILE") == std::string::npos) {
            std::string plan = text;
            if (const auto b = plan.find("=== PLAN"); b != std::string::npos) {
                plan.erase(0, plan.find('\n', b) == std::string::npos ? plan.size() : plan.find('\n', b) + 1);
                if (const auto e = plan.find("=== END PLAN"); e != std::string::npos) plan.erase(e);
            }
            plan.erase(0, plan.find_first_not_of(" \t\r\n"));
            if (!plan.empty() && plans < 2 * (a.repairTurns + 2)) {
                ++plans;
                carriedPlan = clip(plan, 2000);
                decideNext = true;
                traceVerdict(tracePath, call, "plan written (" + std::to_string(plan.size()) + " characters): carried into the next turn, which runs with thinking off; no turn used");
                std::printf("canvas turn %d: plan written\n", call);
                continue;
            }
        }
        if (reply->finishReason == "length" && continuations < freeContinuations && (text.find("=== EDIT:") != std::string::npos || text.find("=== FILE:") != std::string::npos)) {
            ++continuations;
            partial = text;
            traceVerdict(tracePath, call, "cut off at the token limit after " + std::to_string(text.size()) + " characters: asked to continue (continuation " + std::to_string(continuations) + ")");
            std::printf("canvas turn %d: cut off, continuing (%d)\n", call, continuations);
            continue;
        }
        partial.clear();

        // Concerns (stance.hpp) come out before the transaction is read: recorded, never applied, never counted as stray text.
        if (const auto concerns = takeConcerns(text); !concerns.empty()) {
            concernCount += static_cast<int>(concerns.size());
            (void)recordConcerns(attemptDir, a.itemId, call, concerns);
            traceVerdict(tracePath, call, std::to_string(concerns.size()) + " concern(s) recorded in concerns.md");
            events::note("worker raised " + std::to_string(concerns.size()) + " concern(s)");
            if (text.find("=== ") == std::string::npos) {   // concerns and no work: acknowledged; the first such reply costs no turn
                if (++concernOnly > 1) ++spent;
                verdict = concernAcknowledgement(concerns);
                continue;
            }
        }

        const CanvasReply parsed = parseCanvasReply(text);
        const std::vector<int> issueAsks = issueLayer ? parseIssueReads(text) : std::vector<int>{};
        if (parsed.error.empty() && !parsed.hasEdits()) {
            const bool asksToRead = !parsed.reads.empty() || !issueAsks.empty();
            if (asksToRead && !canvas.paths().empty() && readReplies >= freeReads) {
                // Past the allowance a read is refused, not answered: answered, it let a worker read until its turns ran out. What it read last stays in view.
                ++readReplies;
                ++spent;
                verdict = (lastReadAnswer.empty() ? std::string{} : "The lines you read before:\n" + lastReadAnswer) +
                          (restructure ? "Your reads are used up, so this read was not answered. Reply now with an edit (=== EDIT: ... ===) or, if the approach is wrong, the complete new file (=== FILE: ... ===).\n"
                                       : "Your reads are used up, so this read was not answered. Reply now with an edit (=== EDIT: ... ===).\n");
                verdictIsRead = !lastReadAnswer.empty();
                traceVerdict(tracePath, call, "read request refused: the reads are used up (a turn is spent)");
                std::printf("canvas turn %d: read refused (reads used up), turn spent\n", call);
                continue;
            }
            if (asksToRead && !canvas.paths().empty()) {
                // A read (docs/CANVAS.md (Folded views)): the first two read-only replies are answered without using a turn; after that the worker must edit.
                std::string answer;
                int readEarlier = -1;   // every region read before, on the same revision: the turn it was first read in; -1 when any region is new
                for (const auto& rq : parsed.reads) {
                    if (!canvas.has(rq.path)) { answer += "=== " + rq.path + ": not one of the files of this item ===\n"; readEarlier = -2; continue; }
                    const std::string& textNow = canvas.text(rq.path);
                    answer += answerRead(rq, textNow, isPythonPath(rq.path));
                    int from = rq.from, to = rq.to;
                    if (!rq.symbol.empty())
                        for (const auto& r : foldRegions(textNow, isPythonPath(rq.path), true))
                            if (r.name == rq.symbol || (rq.symbol.size() > r.name.size() && rq.symbol.ends_with("::" + r.name))) { from = r.first; to = r.bodyTo; break; }
                    if (from <= 0) { readEarlier = -2; continue; }   // an outline: nothing to keep open
                    readKept.push_back(rq);
                    for (int l = from; l <= std::max(from, to); l += 8) readFocus += rq.path + ":" + std::to_string(l) + "\n";
                    const std::string key = rq.path + (rq.symbol.empty() ? " LINES " + std::to_string(rq.from) + "-" + std::to_string(rq.to) : " SYMBOL " + rq.symbol);
                    const int rev = canvas.revision(rq.path);
                    if (const auto seen = readBefore.find(key); seen != readBefore.end() && seen->second.second == rev) { if (readEarlier != -2) readEarlier = std::max(readEarlier, seen->second.first); }
                    else { readEarlier = -2; readBefore[key] = {call, rev}; }
                }
                if (readEarlier >= 0 && issueAsks.empty())
                    answer = "You read all of these lines before (turn " + std::to_string(readEarlier) + ") and they have not changed since: the canvas view keeps "
                             "them open from now on while it has room. Do not read them again; write the edit.\n" + answer;
                for (const int n : issueAsks) {   // an issue in full, its anchors resolved to the lines of the files as they now stand
                    ++issueReads;
                    for (const auto& [path, textNow] : canvas.files()) blockIndex.update(path, textNow);
                    answer += n >= 1 && n <= static_cast<int>(issues.size()) ? layerDetail(issues[static_cast<std::size_t>(n) - 1], blockIndex, canvas.files())
                                                                             : "=== ISSUE " + std::to_string(n) + ": there is no such issue (" + std::to_string(issues.size()) + " listed) ===\n";
                }
                ++readReplies;
                lastReadAnswer = answer;
                verdict = "The lines you asked to read:\n" + answer + (readReplies >= freeReads ? "You have used your reads: your next reply MUST be an edit (=== EDIT: ... ===) or a file, not another read.\n" : "Now reply with your edit.\n");
                verdictIsRead = true;
                traceVerdict(tracePath, call, "read request: " + std::to_string(parsed.reads.size() + issueAsks.size()) + " region(s) or issue(s) shown, no turn used");
                std::printf("canvas turn %d: read answered (%zu region(s))\n", call, parsed.reads.size() + issueAsks.size());
                continue;
            }
            ++spent;
            const bool cutThinking = reply->finishReason == "length" && !reply->reasoning.empty() && text.find("=== ") == std::string::npos;
            if (cutThinking) { carriedReasoning = tailAtLine(reply->reasoning, kCarriedReasoningChars); decideNext = true; }   // kept, not thrown away
            verdict = std::string{reply->finishReason == "length" ? "Your reply was cut off at the token limit before it held an edit. Write less. " : ""} + "Your reply held no transaction. Reply with one transaction (the forms above) and nothing else.";
            traceVerdict(tracePath, call, "no transaction in the reply (finish " + reply->finishReason + ")" + (cutThinking ? "; its reasoning is carried into the next turn" : ""));
            continue;
        }

        // ---- the transaction: every part that applies is applied (one bad edit never costs the good ones beside it), then the cheap check ----
        Canvas scratch = canvas;
        std::vector<std::string> skipped;
        std::string scopeRefusal;   // FILE over a file that exists and is not a stub, before the scope is widened: refused, nothing applied
        for (const auto& [path, body] : parsed.wholeFiles)
            if (scopeRefusal = scopeRefusalFor(path); !scopeRefusal.empty()) break;
        std::string refusal = scopeRefusal.empty() ? applyCanvasReply(scratch, parsed, allowed, a.itemId, &skipped) : scopeRefusal;
        std::string skippedNote;
        for (const auto& s : skipped) skippedNote += "- " + s + "\n";
        if (!skippedNote.empty()) skippedNote = "These parts of your reply did not apply (the rest did):\n" + skippedNote;
        if (refusal.empty() && scratch.files() == canvas.files())
            refusal = skipped.empty() ? "the transaction changes nothing: the files are exactly as before" : "no part of it applied:\n" + skippedNote;
        if (!refusal.empty()) {
            ++rejects;
            if (rejects > freeRejects) ++spent;   // the first refusals are answered in the same turn; after that each costs one
            if (refusal.rfind("the transaction changes nothing", 0) == 0 && rejects > 1) { stagnated = true; traceVerdict(tracePath, call, "stagnated: " + refusal); break; }
            // The same refusal three times running (line numbers aside) is a worker that cannot see its error: more turns repeat it (run-v11's tester spent
            // six turns an attempt on one compile error, catalogue row 72). The attempt stops and says why; the ladder decides with that evidence.
            const std::string key = errorKey(refusal);
            sameRefusals = key == lastRefusalKey ? sameRefusals + 1 : 1;
            lastRefusalKey = key;
            lastRefusal = refusal;
            if (sameRefusals >= 3) { stagnated = true; traceVerdict(tracePath, call, "stopped: the same refusal three times running: " + refusal); std::printf("canvas turn %d: the same refusal three times running: the attempt stops\n", call); break; }
            verdict = "Your transaction was REFUSED and nothing changed: " + refusal + "\nThe files are as shown below. Reply with a corrected transaction.";
            traceVerdict(tracePath, call, "transaction refused: " + refusal);
            std::printf("canvas turn %d: transaction refused: %s\n", call, refusal.substr(0, 200).c_str());
            continue;
        }
        for (std::size_t i = canvas.log().size(); i < scratch.log().size(); ++i) { const auto& e = scratch.log()[i]; changedLines += std::max(e.to - e.from + 1, e.to - e.from + 1 + e.delta); }
        const std::string bad = precheckCanvas(scratch, harness, a.dir, a.itemId, a.attempt, attemptDir);
        canvas = std::move(scratch);
        persistCanvas(canvas, attemptDir);
        if (!bad.empty()) {
            // Staged, not discarded (tier 1b of 2026-11-17: 54k tokens of drafts refused over compile errors and warnings, every one written again from
            // nothing). The diagnostics now name lines of the revision the worker is shown next, so it fixes them with small edits.
            headCompiles = false;
            ++staged;
            if (staged > freeRejects) ++spent;
            const std::string badVerdict = layeredVerdict(bad, false, 1200, focus);
            const std::string key = errorKey(bad);
            sameStaged = key == lastStagedKey ? sameStaged + 1 : 1;
            lastStagedKey = key;
            lastRefusal = "kept but does not compile: " + bad;
            if (sameStaged >= 3) {
                stagnated = true;
                traceVerdict(tracePath, call, "stopped: the same compile error three times running (the revisions are kept): " + clip(bad, 600));
                std::printf("canvas turn %d: the same compile error three times running: the attempt stops\n", call);
                break;
            }
            verdict = skippedNote + "Your edits were KEPT (" + revisionList(canvas) + "; nothing was thrown away), but the files do not compile yet:\n" + badVerdict +
                      (fixSite ? "\n" + causeNote + "The line numbers are those of the files shown below. Edit; do not rewrite the file."
                               : std::string{"\nThe line numbers are those of the files shown below. Fix just those lines with edits; do not rewrite the file."});
            traceVerdict(tracePath, call, "staged (does not compile yet): " + clip(bad, 600));
            std::printf("canvas turn %d: staged, does not compile yet\n", call);
            continue;
        }
        headCompiles = true;
        sameStaged = 0;
        lastStagedKey.clear();
        ++commits;
        ++spent;
        if (!writeOut()) return 1;
        haveFiles = true;
        std::string missing;
        for (const auto& f : a.item->files) if (!canvas.has(f)) missing += (missing.empty() ? "" : ", ") + f;
        if (!missing.empty()) {
            verdict = skippedNote + "Committed. Still to write (whole, with === FILE: <path> ===): " + missing;
            traceVerdict(tracePath, call, "committed; missing " + missing);
            continue;
        }
        if (!harness || !harness->gate) break;
        const int rc = selfCheck();
        traceVerdict(tracePath, call, rc == 0 ? "committed (revision log in canvas.json); self-check passed: the item's tests pass" : "committed; " + lastGate);
        events::note(rc == 0 ? "self-check passed" : "self-check failed");
        std::printf("canvas turn %d: committed, self-check %s\n", call, rc == 0 ? "passed" : "failed");
        if (rc == 0) { verified = true; break; }
        // Never lose a better revision: a commit that makes more tests fail than the best one so far is undone, and the worker is told.
        const int failing = failingTests(lastGate);
        if (failing >= 0 && bestFailing >= 0 && failing > bestFailing) {
            const std::string broke = layeredVerdict(lastGate, true, 1200, focus);   // what the worse revision broke, before it is undone
            const std::string undone = restoreBest();   // the worse revisions are not lost: they stay in the history, and the worker is told where
            ++reverts;
            (void)writeOut();
            verdict = skippedNote + "Your transaction made it worse (" + std::to_string(failing) + " tests failing, " + std::to_string(bestFailing) + " before), so the files below are back at the better revision (yours stays in the history as " + undone + "). What your change broke:\n" + broke;
            traceVerdict(tracePath, call, "reverted to the best revision (" + std::to_string(bestFailing) + " failing)");
            std::printf("canvas turn %d: reverted (%d failing, best %d)\n", call, failing, bestFailing);
            (void)selfCheck();   // gate.txt describes what is in out/
            noteProgress(false, call, verdict);
        } else {
            const bool improved = failing >= 0 && (bestFailing < 0 || failing < bestFailing);
            if (failing >= 0 && (bestFailing < 0 || failing <= bestFailing)) markBest(failing, false);
            verdict = skippedNote + "Your files were checked by running the tests and failed:\n" + layeredVerdict(lastGate, true, 1500, focus);
            noteProgress(improved, call, verdict);
        }
    }

    if (!headCompiles) {
        // The attempt ended on a staged revision. A decision, recorded: a tested revision is handed on in its place (out/ already holds it) and the draft
        // stays in the history; with none, the draft itself is handed on (its gate says why it fails), so the next attempt starts from it, not from nothing.
        const std::string draft = revisionList(canvas);
        if (bestFailing >= 0 && !bestRevision.empty() && !bestIsSeed) {
            (void)restoreBest();
            traceVerdict(tracePath, calls, "ended on a draft that does not compile (" + draft + ", kept in the history): the best tested revision (" + std::to_string(bestFailing) + " failing) is handed on");
        } else if ((!haveFiles || bestIsSeed) && staged > 0) {   // only the worker's own draft: unchanged starting files are not its output
            haveFiles = writeOut();
            traceVerdict(tracePath, calls, "ended on a draft that does not compile (" + draft + "): no revision of the worker's was tested, so the draft is handed on");
        }
    }
    long fileLines = 0;
    for (const auto& [path, textNow] : canvas.files()) fileLines += std::count(textNow.begin(), textNow.end(), '\n');
    events::metric("tokens.in", totIn);
    events::metric("tokens.out", totOut);
    events::metric("agent.turns", static_cast<double>(calls));
    events::metric("agent.selfcheck_passed", verified ? 1.0 : 0.0);
    events::metric("agent.stagnated", stagnated ? 1.0 : 0.0);
    events::metric("canvas.commits", static_cast<double>(commits));
    events::metric("canvas.refused", static_cast<double>(rejects));
    events::metric("canvas.staged", static_cast<double>(staged));
    events::metric("canvas.thinking_stops", static_cast<double>(thinkingStops));
    events::metric("canvas.aborted", static_cast<double>(aborts));
    events::metric("canvas.reads", static_cast<double>(readReplies));
    events::metric("canvas.changed_lines", static_cast<double>(changedLines));
    events::metric("agent.concerns", static_cast<double>(concernCount));
    events::metric("canvas.issue_reads", static_cast<double>(issueReads));
    if (decodeTps > 0) events::metric("engine.decode_tps", decodeTps);
    if (prefillTps > 0) events::metric("engine.prefill_tps", prefillTps);
    std::printf("done after %d call(s): canvas, %d commit(s), %d refused, %d read(s), %d cancelled, %d reverted, self-check %s, finish=%s\n", calls, commits, rejects, readReplies, aborts, reverts, verified ? "passed" : "not passed", lastFinish.c_str());
    if (!haveFiles && !a.baselineFiles.empty()) haveFiles = writeOut();   // nothing was committed: the earlier draft stands, the best there is to hand on
    events::metric("tokens.work", haveFiles ? deliverableTokens(a.engine, out, totOut) : 0.0);   // the ledger's work ratio
    {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.began).count();
        std::ostringstream m;
        m << "{\"treatment\":\"" << a.treatment << "\",\"stance\":\"" << a.stance << "\",\"concerns\":" << concernCount << ",\"engine\":\"" << a.engine.name << "\",\"tokensIn\":" << totIn << ",\"tokensOut\":" << totOut << ",\"turns\":" << calls << ",\"issueLayer\":" << (issueLayer ? "true" : "false") << ",\"issueMode\":" << issueMode
          << ",\"issueInline\":" << inlineIssues << ",\"layerShown\":" << layerShown << ",\"layerPassed\":" << layerPassed << ",\"issueReads\":" << issueReads
          << ",\"selfcheck\":" << (verified ? "true" : "false") << ",\"seconds\":" << secs
          << ",\"canvas\":{\"commits\":" << commits << ",\"refused\":" << rejects << ",\"reads\":" << readReplies << ",\"aborted\":" << aborts << ",\"continued\":" << continuations << ",\"prefilled\":" << prefilled
          << ",\"reverted\":" << reverts << ",\"restructuredAt\":" << restructuredAt << ",\"staged\":" << staged << ",\"thinkingStops\":" << thinkingStops << ",\"changedLines\":" << changedLines << ",\"fileLines\":" << fileLines << "}}";
        (void)writeFile(attemptDir / "agent.json", m.str());
    }
    if (!haveFiles) {
        std::fprintf(stderr, "llm-agent: no turn produced the requested files\n");
        recordAttemptFailure(attemptDir, "agent", "format", "no reply was kept: " + std::to_string(calls) + " call(s), " + std::to_string(rejects) + " transaction(s) refused" + (sameRefusals >= 3 ? ", the same refusal three times running" : "") +
                                                                           (lastRefusal.empty() ? std::string{"; last finish "} + lastFinish : ". The last refusal: " + lastRefusal));
        return 3;
    }
    events::progress(a.repairTurns + 1, a.repairTurns + 1, "written");
    if (!verified && lastFinish == "length") {
        recordAttemptFailure(attemptDir, "agent", "oversize", "the reply was cut off at the " + std::to_string(a.maxTokens) + " token limit and the item's tests do not pass: it does not fit one attempt");
        events::note("reply cut off at the token limit: the item is too big for one attempt");
        std::printf("size verdict: split (the reply was cut off at the %d token limit)\n", a.maxTokens);
        return 4;
    }
    return 0;
}

}  // namespace sub0squad::agent
