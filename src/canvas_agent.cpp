// Canvas slice 0 (docs/CANVAS.md): a single-worker canvas per attempt. The document is the state and a reply is a TRANSACTION against the revision the
// worker was shown. Each turn the conversation is rebuilt from the card, the verdict of the last turn and a folded view of the current revision: it never
// carries files or earlier replies. A transaction is applied to a scratch copy, the harness's cheapest check (compile or parse) gates the commit, and only
// then do the item's tests run. A rejected transaction changes nothing and says why; a commit that makes the tests worse is reverted. Reads are bounded,
// a runaway reply is cancelled while it streams, a reply cut off at the token limit is continued by appending. Off by default (`--canvas 1`).
#include "agent_impl.hpp"
#include "sub0squad/harness_common.hpp"

#include "sub0squad/canvas.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/turn_control.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sub0squad::agent {

namespace fs = std::filesystem;

const char* const kCanvasSyntax =
    "You edit a shared document. Each turn you are shown the files as they now stand, with line numbers (`N| text`; the numbers are not part of the file) and long function bodies "
    "FOLDED to `... (N lines folded: a-b; name)`. Reply with one small TRANSACTION and nothing else (no explanations, no markdown fences). The forms:\n"
    "=== EDIT: <path> SYMBOL <function name> ===\n<the whole new function, from its signature to its last line>\n=== END EDIT ===\n"
    "=== EDIT: <path> LINES a-b ===\n<the new text for those lines; empty to delete them>\n=== END EDIT ===\n"
    "=== EDIT: <path> AFTER n ===\n<lines to insert after line n>\n=== END EDIT ===\n"
    "=== FILE: <path> ===\n<the complete file>\n=== END FILE ===   (only for a file that does not exist yet, or when most of it changes)\n"
    "Several edits may be given in one reply: line numbers are always those of the view you were shown, and all of them apply together or none does. "
    "A worked example. The view shows\n"
    "  41|     if (d == 0) return 0;\n"
    "and line 41 must report the error instead. The whole reply is:\n"
    "=== EDIT: src/calc.cpp LINES 41 ===\n    if (d == 0) throw std::domain_error(\"division by zero\");\n=== END EDIT ===\n"
    "Never copy the file or the numbered view back. To see folded lines, reply with ONLY read requests: `=== READ: <path> LINES a-b ===` or `=== READ: <path> SYMBOL name ===`; "
    "at most two replies may be reads, then you must edit. Every transaction is checked before it is kept: one that does not compile or parse is refused and changes nothing.\n";

std::string canvasSystemBlock(const std::string& persona, const std::string& brief)
{
    std::string s = kCanvasSyntax;
    if (!brief.empty()) s += brief + "\n";
    s += "You are the " + persona + " on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done, "
         "keep to your contract and the exact interfaces you are given so the other pieces fit, and change only the files you are asked to write. ";
    s += persona == "tester" ? "Write tests for the contract; do not write the implementation.\n" : "Implement the contract so that the provided tests pass.\n";
    return s;
}

long long turnSeed(long long base, const std::string& label)
{
    if (base < 0) return base;
    std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(base);
    for (const char c : label) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    return static_cast<long long>(h & 0x7fffffffull);
}

int failingTests(const std::string& gateText)
{
    if (gateText.rfind("GATE FAIL: ", 0) == 0 && gateText.find(" of ") != std::string::npos && std::isdigit(static_cast<unsigned char>(gateText[11]))) return std::atoi(gateText.c_str() + 11);
    return -1;
}

std::string applyCanvasReply(Canvas& canvas, const CanvasReply& reply, const std::set<std::string>& allowed, const std::string& by)
{
    if (!reply.error.empty()) return reply.error;
    for (const auto& [path, text] : reply.wholeFiles) {
        if (!allowed.count(path)) return "'" + path + "' is not one of the files you may write";
        (void)canvas.replaceText(path, text, by);
    }
    for (Transaction tx : reply.transactions) {
        if (!allowed.count(tx.path)) return "'" + tx.path + "' is not one of the files you may write";
        if (!canvas.has(tx.path)) return "there is no '" + tx.path + "' yet: write it whole with === FILE: " + tx.path + " ===";
        if (reply.wholeFiles.count(tx.path)) return "'" + tx.path + "' is both rewritten whole and edited in one reply: use one or the other";
        tx.baseRevision = canvas.revision(tx.path);
        tx.by = by;
        if (const TxResult r = canvas.apply(tx); !r.ok) return tx.path + ": " + r.error;
    }
    if (!reply.searchEdits.empty()) {   // the older search-and-replace form still works: its result is committed as a whole-text revision
        std::string err;
        const auto before = canvas.files();
        const auto merged = applyEdits(reply.searchEdits, before, allowed, err);
        if (!merged) return err;
        for (const auto& [path, text] : *merged) if (before.at(path) != text) (void)canvas.replaceText(path, text, by);
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
    const std::string system = canvasSystemBlock(a.item->persona, harnessBrief(a.plan->harness));

    Canvas canvas;
    for (const auto& [path, text] : a.baselineFiles) canvas.addFile(path, text);
    persistCanvas(canvas, attemptDir);

    const fs::path tracePath = attemptDir / "conversation.md";
    traceBegin(tracePath, "Conversation of " + a.itemId + ", attempt " + std::to_string(a.attempt) + " (" + a.tier + ", canvas)", a.engine, a.maxTokens);
    (void)writeFile(attemptDir / "prompt.txt", "SYSTEM:\n" + system + "\nUSER (rebuilt each turn; the card):\n" + a.card);

    // What the next message says about the last turn: the failure of the earlier draft to begin with, then a gate failure, a refusal or the lines a read asked for.
    std::string verdict = a.baselineFiles.empty() ? std::string{} : (a.draftOrigin.empty() ? "These files are your best earlier draft. It failed:\n" : "These files are an earlier draft from before '" + a.draftOrigin + "' was divided (keep what is right, fix the rest). It failed:\n") + clip(a.draftGate, 1200);
    std::string focus = a.draftGate;      // the text whose file:line references keep regions of the view open
    std::string partial;                  // a reply cut off at the token limit, so far: the next reply is appended to it
    int spent = 0;                        // turns used of repairTurns + 1
    int readReplies = 0, rejects = 0, aborts = 0, continuations = 0, calls = 0, commits = 0, reverts = 0;
    const int freeRejects = 3, freeAborts = 2, freeReads = 2, freeContinuations = 3;
    bool verified = false, haveFiles = false, stagnated = false;
    int bestFailing = -1;                 // fewest failing tests of any committed revision, and where it was
    std::map<std::string, int> bestRevision;
    if (const int f = failingTests(a.draftGate); f >= 0 && !a.baselineFiles.empty()) { bestFailing = f; for (const auto& path : canvas.paths()) bestRevision[path] = 1; }
    std::string lastFinish, lastGate, lastRefusal, lastRefusalKey;
    int sameRefusals = 0;
    double totIn = 0, totOut = 0, decodeTps = 0, prefillTps = 0;
    long changedLines = 0;

    while (spent <= a.repairTurns) {
        // ---- the message: the card, the verdict, and the current revision folded to fit what the window has left ----
        std::string state;
        if (canvas.paths().empty()) state = "No file exists yet. Write every file whole: === FILE: <path> === ... === END FILE ===\n";
        else {
            const long availTokens = static_cast<long>(a.engine.contextTokens) - a.maxTokens - 300;
            const long availChars = static_cast<long>(static_cast<double>(availTokens) * 3.2) - static_cast<long>(system.size() + a.card.size() + a.guidance.size()) - 1500 - static_cast<long>(std::min<std::size_t>(verdict.size(), 2400));
            const std::size_t perFile = static_cast<std::size_t>(std::clamp(availChars / std::max<long>(1, static_cast<long>(canvas.paths().size())), 800L, 9000L));
            for (const auto& path : canvas.paths()) {
                const std::string& text = canvas.text(path);
                state += "=== CANVAS: " + path + " (revision " + std::to_string(canvas.revision(path)) + ", " + std::to_string(std::count(text.begin(), text.end(), '\n')) + " lines) ===\n" + draftView(path, text, focus, perFile) + "=== END CANVAS ===\n";
            }
            for (const auto& f : a.item->files) if (!canvas.has(f)) state += "(" + f + " does not exist yet: write it whole with === FILE: " + f + " ===)\n";
        }
        const std::string user = a.card + (a.guidance.empty() ? std::string{} : "\n" + a.guidance + "\n") + (verdict.empty() ? std::string{} : "\n" + clip(verdict, 2400) + "\n") + "\n" + state +
                                 (canvas.paths().empty() ? "" : "Reply with the transaction that fixes this: change only what is needed.\n");
        std::vector<ChatMessage> convo = {{"system", system}, {"user", user}};
        if (!partial.empty()) {   // continue a reply that was cut off: its tail is shown, the rest is appended (never started over)
            convo.push_back({"assistant", "[your reply so far is kept; it was cut off at the token limit]"});
            convo.push_back({"user", "Your reply was cut off at the token limit. It ended with:\n```\n" + (partial.size() > 700 ? partial.substr(partial.size() - 700) : partial) + "\n```\nContinue from exactly that point: reply with only the rest (no header, no repetition, no explanation) and finish it."});
        }

        EngineProfile turnEngine = a.engine;
        turnEngine.seed = turnSeed(a.engine.seed, a.itemId + "#" + std::to_string(a.attempt) + "#" + std::to_string(calls));
        events::progress(spent, a.repairTurns + 1, calls == 0 ? "generating" : "editing");
        traceSent(tracePath, calls, convo, 0, turnEngine.seed);
        const fs::path livePath = attemptDir / "live.txt";
        (void)writeFile(livePath, "(turn " + std::to_string(calls) + " sent; waiting for the first token)\n");
        bool liveStarted = false, inReasoning = false;
        const DeltaSink liveSink = [&](std::string_view content, std::string_view reasoning) {
            std::ofstream o(livePath, std::ios::app);
            if (!liveStarted) { o.close(); (void)writeFile(livePath, ""); o.open(livePath, std::ios::app); liveStarted = true; }
            if (!reasoning.empty()) { if (!inReasoning) { o << "[thinking]\n"; inReasoning = true; } o << reasoning; }
            if (!content.empty()) { if (inReasoning) { o << "\n[answer]\n"; inReasoning = false; } o << content; }
        };
        // Early abort: the reply is judged while it streams and cancelled as soon as it is going wrong in a known way (twice at most per attempt).
        Runaway runaway;
        const auto drafts = canvas.files();
        const StreamGuard guard = [&](const std::string& soFar) { runaway = detectRunaway(partial + soFar, drafts); return static_cast<bool>(runaway); };
        auto reply = chatCompleteMessages(turnEngine, convo, a.maxTokens, a.timeoutSec, liveSink, aborts < freeAborts ? guard : StreamGuard{});
        { std::error_code lec; fs::remove(livePath, lec); }
        const int call = calls++;
        if (!reply) {
            traceVerdict(tracePath, call, "the engine call failed: " + reply.error());
            std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str());
            if (!haveFiles) { harnessutil::recordAttemptFailure(attemptDir, "agent", "engine", "the engine call of turn " + std::to_string(call) + " failed: " + reply.error()); return 2; }
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

        if (reply->finishReason == "aborted") {   // cancelled while streaming: nothing is applied, the worker is told exactly what to do instead
            ++aborts;
            partial.clear();
            verdict = runaway.instruction;
            traceVerdict(tracePath, call, "cancelled while streaming (" + runaway.kind + ") after " + std::to_string(reply->content.size()) + " characters");
            std::printf("canvas turn %d: cancelled early (%s)\n", call, runaway.kind.c_str());
            events::note("reply cancelled early: " + runaway.kind);
            continue;
        }
        const std::string text = partial.empty() ? reply->content : partial + continuationText(partial, reply->content);
        if (reply->finishReason == "length" && continuations < freeContinuations && (text.find("=== EDIT:") != std::string::npos || text.find("=== FILE:") != std::string::npos)) {
            ++continuations;
            partial = text;
            traceVerdict(tracePath, call, "cut off at the token limit after " + std::to_string(text.size()) + " characters: asked to continue (continuation " + std::to_string(continuations) + ")");
            std::printf("canvas turn %d: cut off, continuing (%d)\n", call, continuations);
            continue;
        }
        partial.clear();

        const CanvasReply parsed = parseCanvasReply(text);
        if (parsed.error.empty() && !parsed.hasEdits()) {
            if (!parsed.reads.empty() && !canvas.paths().empty()) {
                // A read (docs/FOLDED_VIEWS.md): the first two read-only replies are answered without using a turn; after that the worker must edit.
                std::string answer;
                for (const auto& rq : parsed.reads)
                    answer += canvas.has(rq.path) ? answerRead(rq, canvas.text(rq.path), isPythonPath(rq.path)) : "=== " + rq.path + ": not one of the files of this item ===\n";
                ++readReplies;
                const bool free = readReplies <= freeReads;
                if (!free) ++spent;
                verdict = "The lines you asked to read:\n" + answer + (readReplies >= freeReads ? "You have used your reads: your next reply MUST be an edit (=== EDIT: ... ===) or a file, not another read.\n" : "Now reply with your edit.\n");
                traceVerdict(tracePath, call, "read request: " + std::to_string(parsed.reads.size()) + " region(s) shown" + (free ? ", no turn used" : ", counted as a turn (reads are used up)"));
                std::printf("canvas turn %d: read answered (%zu region(s))%s\n", call, parsed.reads.size(), free ? "" : ", turn spent");
                continue;
            }
            ++spent;
            verdict = std::string{reply->finishReason == "length" ? "Your reply was cut off at the token limit before it held an edit. Write less. " : ""} + "Your reply held no transaction. Reply with === EDIT: <path> ... === blocks (or === FILE: <path> === for a file that does not exist yet) and nothing else.";
            traceVerdict(tracePath, call, "no transaction in the reply (finish " + reply->finishReason + ")");
            continue;
        }

        // ---- the transaction: scratch copy, all or nothing, then the cheap check gates the commit ----
        Canvas scratch = canvas;
        std::string refusal = applyCanvasReply(scratch, parsed, allowed, a.itemId);
        if (refusal.empty() && scratch.files() == canvas.files()) refusal = "the transaction changes nothing: the files are exactly as before";
        if (refusal.empty()) {
            if (const std::string bad = precheckCanvas(scratch, harness, a.dir, a.itemId, a.attempt, attemptDir); !bad.empty()) {
                refusal = "it does not pass the check, so it was not kept (line numbers below are those of your edited text, which was discarded):\n" + clip(bad, 1200);
                focus = bad;
            }
        }
        if (!refusal.empty()) {
            ++rejects;
            if (rejects > freeRejects) ++spent;   // the first refusals are answered in the same turn; after that each costs one
            if (refusal.rfind("the transaction changes nothing", 0) == 0 && rejects > 1) { stagnated = true; traceVerdict(tracePath, call, "stagnated: " + refusal); break; }
            // The same refusal three times running (line numbers aside) is a worker that cannot see its error: more turns repeat it (run-v11's tester spent
            // six turns an attempt on one compile error, catalogue row 72). The attempt stops and says why; the ladder decides with that evidence.
            std::string key;
            for (const char c : refusal.substr(0, 400)) if (!std::isdigit(static_cast<unsigned char>(c))) key += c;
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
        canvas = std::move(scratch);
        ++commits;
        ++spent;
        persistCanvas(canvas, attemptDir);
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, textNow] : canvas.files()) if (!writeFile(out / path, textNow)) { std::fprintf(stderr, "llm-agent: cannot write %s\n", path.c_str()); return 1; }
        haveFiles = true;
        std::string missing;
        for (const auto& f : a.item->files) if (!canvas.has(f)) missing += (missing.empty() ? "" : ", ") + f;
        if (!missing.empty()) {
            verdict = "Committed. Still to write (whole, with === FILE: <path> ===): " + missing;
            traceVerdict(tracePath, call, "committed; missing " + missing);
            continue;
        }
        if (!harness || !harness->gate) break;
        const int rc = harness->gate({"--workflow-dir", a.dir.string(), "--item", a.itemId, "--attempt", std::to_string(a.attempt), "--no-publish", "1"});
        lastGate = slurp(attemptDir / "gate.txt");
        traceVerdict(tracePath, call, rc == 0 ? "committed (revision log in canvas.json); self-check passed: the item's tests pass" : "committed; " + lastGate);
        events::note(rc == 0 ? "self-check passed" : "self-check failed");
        std::printf("canvas turn %d: committed, self-check %s\n", call, rc == 0 ? "passed" : "failed");
        if (rc == 0) { verified = true; break; }
        // Never lose a better revision: a commit that makes more tests fail than the best one so far is undone, and the worker is told.
        const int failing = failingTests(lastGate);
        if (failing >= 0 && bestFailing >= 0 && failing > bestFailing) {
            for (const auto& [path, rev] : bestRevision) if (canvas.has(path) && canvas.revision(path) != rev) (void)canvas.revertTo(path, rev, "harness");
            ++reverts;
            persistCanvas(canvas, attemptDir);
            for (const auto& [path, textNow] : canvas.files()) (void)writeFile(out / path, textNow);
            verdict = "Your transaction made it worse (" + std::to_string(failing) + " tests failing, " + std::to_string(bestFailing) + " before), so it was UNDONE; the files below are the earlier revision. What your change broke:\n" + clip(lastGate, 1200);
            traceVerdict(tracePath, call, "reverted to the best revision (" + std::to_string(bestFailing) + " failing)");
            std::printf("canvas turn %d: reverted (%d failing, best %d)\n", call, failing, bestFailing);
            (void)harness->gate({"--workflow-dir", a.dir.string(), "--item", a.itemId, "--attempt", std::to_string(a.attempt), "--no-publish", "1"});   // gate.txt describes what is in out/
            lastGate = slurp(attemptDir / "gate.txt");
        } else {
            if (failing >= 0 && (bestFailing < 0 || failing <= bestFailing)) { bestFailing = failing; for (const auto& path : canvas.paths()) bestRevision[path] = canvas.revision(path); }
            verdict = "Your files were checked by running the tests and failed:\n" + clip(lastGate, 1500);
        }
        focus = lastGate;
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
    events::metric("canvas.aborted", static_cast<double>(aborts));
    events::metric("canvas.reads", static_cast<double>(readReplies));
    events::metric("canvas.changed_lines", static_cast<double>(changedLines));
    if (decodeTps > 0) events::metric("engine.decode_tps", decodeTps);
    if (prefillTps > 0) events::metric("engine.prefill_tps", prefillTps);
    std::printf("done after %d call(s): canvas, %d commit(s), %d refused, %d read(s), %d cancelled, %d reverted, self-check %s, finish=%s\n", calls, commits, rejects, readReplies, aborts, reverts, verified ? "passed" : "not passed", lastFinish.c_str());
    if (!haveFiles && !a.baselineFiles.empty()) {   // nothing was committed: the earlier draft stands, so the attempt still hands on the best there is
        for (const auto& [path, textNow] : canvas.files()) (void)writeFile(out / path, textNow);
        haveFiles = true;
    }
    {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.began).count();
        std::ostringstream m;
        m << "{\"treatment\":\"" << a.treatment << "\",\"engine\":\"" << a.engine.name << "\",\"tokensIn\":" << totIn << ",\"tokensOut\":" << totOut << ",\"turns\":" << calls
          << ",\"selfcheck\":" << (verified ? "true" : "false") << ",\"seconds\":" << secs
          << ",\"canvas\":{\"commits\":" << commits << ",\"refused\":" << rejects << ",\"reads\":" << readReplies << ",\"aborted\":" << aborts << ",\"continued\":" << continuations
          << ",\"reverted\":" << reverts << ",\"changedLines\":" << changedLines << ",\"fileLines\":" << fileLines << "}}";
        (void)writeFile(attemptDir / "agent.json", m.str());
    }
    if (!haveFiles) {
        std::fprintf(stderr, "llm-agent: no turn produced the requested files\n");
        harnessutil::recordAttemptFailure(attemptDir, "agent", "format", "no reply was kept: " + std::to_string(calls) + " call(s), " + std::to_string(rejects) + " transaction(s) refused" + (sameRefusals >= 3 ? ", the same refusal three times running" : "") +
                                                                           (lastRefusal.empty() ? std::string{"; last finish "} + lastFinish : ". The last refusal: " + lastRefusal));
        return 3;
    }
    events::progress(a.repairTurns + 1, a.repairTurns + 1, "written");
    if (!verified && lastFinish == "length") {
        harnessutil::recordAttemptFailure(attemptDir, "agent", "oversize", "the reply was cut off at the " + std::to_string(a.maxTokens) + " token limit and the item's tests do not pass: it does not fit one attempt");
        events::note("reply cut off at the token limit: the item is too big for one attempt");
        std::printf("size verdict: split (the reply was cut off at the %d token limit)\n", a.maxTokens);
        return 4;
    }
    return 0;
}

}  // namespace sub0squad::agent
