#include "agent_impl.hpp"
#include "sub0squad/harness_common.hpp"

#include "sub0squad/edits.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/policy.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/turn_control.hpp"
#include "sub0squad/sandbox.hpp"

#include <algorithm>
#include <regex>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace agent;

int runLlmAgent(const std::vector<std::string>& args)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // a killed job must not lose what it already printed
    const fs::path dir = arg(args, "workflow-dir");
    const std::string itemId = arg(args, "item"), tier = arg(args, "tier", "local");
    const int attempt = std::atoi(arg(args, "attempt", "1").c_str());
    int maxTokens = std::atoi(arg(args, "max-tokens", "3000").c_str());
    int timeoutSec = std::atoi(arg(args, "timeout", "1700").c_str());
    const int repairTurns = std::max(0, std::atoi(arg(args, "repair-turns", "0").c_str()));  // extra turns after the first: run the tests, feed failures back, repeat
    const long sizeBudget = std::atol(arg(args, "size-budget", "0").c_str());  // plan phase: above this estimate an item with tests is handed back to be divided
    const std::string treatment = arg(args, "treatment", "baseline");  // baseline | index | planfirst (docs/OPTIMIZATION.md)
    bool strategyThink = false;  // the manager's strategy for this item (written by the daemon): reasoning on and a bigger budget
    { std::ifstream sf(fs::path(arg(args, "workflow-dir")) / "items" / arg(args, "item") / "strategy.json"); std::stringstream sss; sss << sf.rdbuf(); strategyThink = sss.str().find("\"think\":true") != std::string::npos; }
    const auto began = std::chrono::steady_clock::now();

    if (tier == "frontier-stub") {  // no frontier provider exists: the stub tier is the reference implementation, recorded as a stub
        std::vector<std::string> a = {"--workflow-dir", dir.string(), "--item", itemId, "--tier", tier, "--attempt", std::to_string(attempt), "--delay-ms", "50"};
        return runMockAgent(a);
    }
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-agent: %s\n", engine.error().c_str()); return 64; }
    if (strategyThink) {  // the manager chose to think harder about this item: reasoning on, room for it, and time for the longer reply
        engine->thinking = true;
        maxTokens = std::max(maxTokens, 6000);
        timeoutSec = std::max(timeoutSec, 3600);
        std::printf("manager strategy: thinking on, %d token budget\n", maxTokens);
    }
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::fprintf(stderr, "llm-agent: %s\n", plan.error().c_str()); return 1; }
    WorkItem effective;
    if (const WorkItem* found = plan->find(itemId)) effective = *found;
    const WorkItem* item = plan->find(itemId) ? &effective : nullptr;
    if (item) (void)readCardOverride(dir / "items" / itemId / "card-override.json", effective.card);  // a rearchitect job rewrote the card
    if (!item) { std::fprintf(stderr, "llm-agent: no item '%s'\n", itemId.c_str()); return 1; }

    // ---- the prompt: a fixed system block, the work card, included files from accepted dependencies, and the previous failure only ----
    const std::string system = systemBlock(item->persona, harnessBrief(plan->harness));
    long testCount = 0;
    std::ostringstream card;
    card << buildCard(dir, *plan, *item, itemId, treatment, testCount);
    std::string feedback;
    std::map<std::string, std::string> baselineFiles;  // the best earlier version a retry starts from: its edits apply to these
    const fs::path disputeFile = dir / "items" / itemId / "dispute.txt";
    std::string disputeText;
    if (fs::exists(disputeFile)) {  // guidance for this item (a disputed test, a send-down, the manager): consumed once
        disputeText = clip(slurp(disputeFile), 1500);
        std::error_code mv;
        fs::rename(disputeFile, dir / "items" / itemId / ("dispute-used-" + std::to_string(attempt) + ".txt"), mv);
    }
    // Progress is never thrown away. Whatever this node's files looked like at their best is handed back, with the failure text, whether this is a retry, a redo after
    // guidance, or the first attempt of a part whose parent already tried (the draft from before the division). An attempt cut off at the token limit or crashing on a
    // syntax error still counts: it is the closest thing there is, and a fix to it is smaller than a rewrite. Edits then apply to these drafts.
    auto bestDraft = [&](const std::string& owner, const std::string& file, std::string& gateOut) -> std::string {
        int bestFail = 1 << 30;
        std::string bestText;
        for (int k = 1; k <= 12; ++k) {
            const fs::path d = dir / "items" / owner / ("attempt-" + std::to_string(k));
            const std::string t = slurp(d / "out" / file);
            if (t.empty()) continue;
            const std::string g = slurp(d / "gate.txt");
            if (g.rfind("GATE PASS", 0) == 0) continue;
            int f = 1 << 29;
            if (g.rfind("GATE FAIL: ", 0) == 0 && g.find(" of ") != std::string::npos) f = std::atoi(g.c_str() + 11);
            if (f <= bestFail) { bestFail = f; bestText = t; gateOut = g; }   // fewest failing tests; the latest on a tie, and the latest of those that could not be run
        }
        return bestText;
    };
    std::string draftGate, origin;
    {
        std::string drafts;
        for (const auto& f : item->files) {
            std::string g, text = bestDraft(itemId, f, g);
            if (text.empty()) {   // a part's first attempt: the drafts of the item it was divided from, and of that item's parents
                for (std::string anc = itemId; text.empty() && anc.find('.') != std::string::npos;) {
                    anc = anc.substr(0, anc.rfind('.'));
                    text = bestDraft(anc, f, g);
                    if (!text.empty()) origin = anc;
                }
            }
            if (text.empty()) continue;
            drafts += "=== DRAFT: " + f + " (" + std::to_string(std::count(text.begin(), text.end(), '\n') + 1) + " lines) ===\n" + draftView(f, text, g, 9000) + "=== END DRAFT ===\n";
            baselineFiles[f] = text;
            if (draftGate.empty()) draftGate = g;
        }
        if (!drafts.empty())
            feedback = (origin.empty() ? std::string{"Your best earlier draft is below. It failed:\n"} : "An earlier draft from before '" + origin + "' was divided is below (it may be incomplete or wrong: keep what is right, fix the rest). It failed:\n") +
                       clip(draftGate, 1200) + "\n" + drafts + "Change only what is needed and keep everything else. " + kEditSyntax + "\n";
        else if (attempt > 1) feedback = "Your previous attempt produced nothing usable. Reply in the required file format.\n";
        if (!disputeText.empty()) feedback = disputeText + (feedback.empty() ? std::string{} : "\n" + feedback);
    }
    // ---- plan phase: size the work before writing any of it. Calibrated on the first real run: about 350 tokens plus 30 per test
    // (69 tests came to about 2,400 tokens). Too big for the budget and still divisible: exit 4 hands the item back to be divided.
    const long estimate = std::max(350 + 30 * testCount, 1200L * static_cast<long>(item->files.size()));  // tests-based, with a floor of about 1,200 tokens per file (the 9B wrote an engine of three files past 3,500)
    // Divide only when it pays (shouldDivide): the item cannot fit one attempt, or the time saved by running parts side by side beats the cost of the division.
    std::string capacity;
    bool divide = false;
    if (sizeBudget > 0 && attempt == 1 && feedback.empty() && testCount > 0 && std::count(itemId.begin(), itemId.end(), '.') < 2) {
        int idle = -1, total = 0;
        if (const auto sc = engineSlots(*engine)) { idle = sc->idle; total = sc->total; capacity = std::to_string(sc->idle) + " of " + std::to_string(sc->total) + " engine slots idle"; }
        divide = shouldDivide(estimate, sizeBudget, static_cast<long>(maxTokens) * 8 / 10, idle, total);
    }
    const long budget = sizeBudget;
    if (divide) {
        writeFile(dir / "items" / itemId / "size.json", "{\"estimateTokens\":" + std::to_string(estimate) + ",\"budget\":" + std::to_string(budget) + ",\"configuredBudget\":" + std::to_string(sizeBudget) + ",\"capacity\":\"" + capacity + "\",\"tests\":" + std::to_string(testCount) + ",\"verdict\":\"split\"}\n");
        writeFile(dir / "items" / itemId / "attempt-1" / "gate.txt", "PLAN PHASE: not attempted. Estimated " + std::to_string(estimate) + " completion tokens for " + std::to_string(testCount) + " tests, over the budget of " + std::to_string(sizeBudget) + ". Divide the item so each part is small.\n");
        events::note("plan phase: estimated " + std::to_string(estimate) + " tokens against a budget of " + std::to_string(budget) + (capacity.empty() ? std::string{} : " (" + capacity + ")") + ": split before implementing");
        std::printf("size verdict: split (estimate %ld tokens for %ld tests, budget %ld; %s)\n", estimate, testCount, budget, capacity.empty() ? "capacity unknown" : capacity.c_str());
        harnessutil::recordAttemptFailure(dir / "items" / itemId / "attempt-1", "agent", "oversize", "plan phase: not attempted; estimated " + std::to_string(estimate) + " completion tokens against a budget of " + std::to_string(budget));
        return 4;
    }
    const std::string executor = arg(args, "executor", "session");
    if (executor != "session" && executor != "canvas") { std::fprintf(stderr, "llm-agent: --executor must be session or canvas (forked sections are the llm-fork command)\n"); return 64; }
    if (executor == "canvas") {   // docs/CANVAS.md slice 0: the attempt runs on a revisioned canvas
        AttemptContext ctx;
        ctx.dir = dir; ctx.itemId = itemId; ctx.tier = tier; ctx.treatment = treatment;
        ctx.attempt = attempt; ctx.maxTokens = maxTokens; ctx.timeoutSec = timeoutSec; ctx.repairTurns = repairTurns;
        ctx.engine = *engine; ctx.plan = &*plan; ctx.item = item;
        ctx.card = card.str(); ctx.guidance = disputeText;
        ctx.baselineFiles = baselineFiles; ctx.draftGate = draftGate; ctx.draftOrigin = origin;
        ctx.began = began;
        return runCanvasAttempt(ctx);
    }
    const std::string user = card.str() + (feedback.empty() ? "" : "\n" + feedback);

    bool est1 = false, est2 = false, est3 = false;
    const long tSystem = countTokens(*engine, system, est1), tCard = countTokens(*engine, card.str(), est2), tFeedback = feedback.empty() ? 0 : countTokens(*engine, feedback, est3);
    if (const char* slot = std::getenv("SUB0SQUAD_LEASE_INDEX"); slot && *slot) events::metric("engine.slot", static_cast<double>(std::atoi(slot)));
    events::progress(0, 2, "waiting for the engine");
    events::metric("tokens.system", static_cast<double>(tSystem));
    events::metric("tokens.card", static_cast<double>(tCard));
    events::metric("tokens.feedback", static_cast<double>(tFeedback));

    std::string userFinal = user;
    long extraIn = 0, extraOut = 0;
    if (treatment == "planfirst") {  // a focusing step: a short plan first, then the code written against that plan
        auto plan1 = chatComplete(*engine, system, card.str() + "\nBefore writing any code, reply with a short plan only (at most 120 words, no code): the structure you will use and the tricky cases you must handle.", 300, -1, timeoutSec);
        if (plan1) { userFinal = user + "\nYour plan:\n" + plan1->content + "\nNow write the complete files.\n"; extraIn = plan1->promptTokens; extraOut = plan1->completionTokens; }
    }
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    const fs::path out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    writeFile(attemptDir / "prompt.txt", "SYSTEM:\n" + system + "\nUSER:\n" + userFinal);
    std::set<std::string> allowed(item->files.begin(), item->files.end());

    // The agent checks its own work: write the files, run the item's tests (the same gate, without publishing), and on failure continue the
    // SAME conversation with the failure excerpt, up to `repairTurns` more times. Only then is it done.
    std::vector<ChatMessage> convo = {{"system", system}, {"user", userFinal}};
    const fs::path tracePath = attemptDir / "conversation.md";
    traceBegin(tracePath, "Conversation of " + itemId + ", attempt " + std::to_string(attempt) + " (" + tier + ")", *engine, maxTokens);
    std::size_t tracedUpTo = 0;
    // Keep the conversation flat: the system block, the card, and only the latest reply with its feedback. Older replies are stale copies of the files, and a
    // reply cut off at the token limit is not repeated at all (the first real run overflowed its 8,192 token slot on the second turn).
    auto continueWith = [&](const std::string& assistantText, bool cutOff, const std::string& userText) {
        convo.resize(2);
        convo.push_back({"assistant", cutOff ? std::string{"[your previous reply was cut off at the token limit and is not repeated here]"} : assistantText});
        convo.push_back({"user", userText});
        tracedUpTo = 2;
    };
    double totIn = static_cast<double>(extraIn), totOut = static_cast<double>(extraOut), lastWork = 0, decodeTps = 0, prefillTps = 0;
    int turns = 0;
    bool verified = false, haveFiles = false;
    std::string lastFinish, lastGate, previousFiles;
    bool stagnated = false;
    std::size_t lastFiles = 0, lastRejected = 0;
    std::string partialPath, partialText;               // a file cut off at the token limit, and what was written of it: the next turn continues it instead of starting over
    std::map<std::string, std::string> carryFiles;      // the files that were complete before the cut
    int continuations = 0, reads = 0, aborts = 0;
    std::string partialEdit;                            // an EDIT reply cut off at the token limit, so far: the next reply is appended to it
    std::map<std::string, std::string> currentFiles = baselineFiles;  // the files of the latest turn (or the best earlier attempt): what an edit applies to
    int turnLimit = repairTurns;   // continuing a cut-off file does not use up a repair turn
    for (int turn = 0; turn <= turnLimit; ++turn) {
        events::progress(turn, turnLimit + 1, turn == 0 ? "generating" : "repairing");
        EngineProfile turnEngine = *engine;
        if (engine->seed >= 0) {  // a reproducible run: one derived seed per (item, attempt, turn), so retries differ from each other but a rerun replays them
            std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(engine->seed);
            for (const char c : itemId + "#" + std::to_string(attempt) + "#" + std::to_string(turn)) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
            turnEngine.seed = static_cast<long long>(h & 0x7fffffffull);
            std::printf("seed for turn %d: %lld (run seed %lld)\n", turn, turnEngine.seed, engine->seed);
        }
        traceSent(tracePath, turn, convo, tracedUpTo, turnEngine.seed);
        tracedUpTo = convo.size();
        // The turn streams: what the model has written so far is in live.txt (the portal appends it to the transcript) until the turn ends.
        const fs::path livePath = attemptDir / "live.txt";
        writeFile(livePath, "(turn " + std::to_string(turn) + " sent; waiting for the first token)\n");
        bool liveStarted = false, inReasoning = false;
        const DeltaSink liveSink = [&](std::string_view content, std::string_view reasoning) {
            std::ofstream o(livePath, std::ios::app);
            if (!liveStarted) { o.close(); writeFile(livePath, ""); o.open(livePath, std::ios::app); liveStarted = true; }
            if (!reasoning.empty()) { if (!inReasoning) { o << "[thinking]\n"; inReasoning = true; } o << reasoning; }
            if (!content.empty()) { if (inReasoning) { o << "\n[answer]\n"; inReasoning = false; } o << content; }
        };
        // Early abort (row 34): the reply is judged while it streams and cancelled when it is going wrong in a known way (twice at most per attempt).
        Runaway runaway;
        const StreamGuard guard = [&](const std::string& soFar) { runaway = detectRunaway(partialEdit + soFar, currentFiles); return static_cast<bool>(runaway); };
        auto reply = chatCompleteMessages(turnEngine, convo, maxTokens, timeoutSec, liveSink, aborts < 2 ? guard : StreamGuard{});
        { std::error_code lec; fs::remove(livePath, lec); }
        if (!reply) {
            traceVerdict(tracePath, turn, "the engine call failed: " + reply.error());
            std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str());
            if (!haveFiles) { harnessutil::recordAttemptFailure(attemptDir, "agent", "engine", "the engine call of turn " + std::to_string(turn) + " failed: " + reply.error()); return 2; }
            break;  // keep the files of the earlier turn
        }
        ++turns;
        traceReply(tracePath, turn, *reply);
        writeFile(attemptDir / ("reply-" + std::to_string(turn) + ".txt"), reply->content);
        writeFile(attemptDir / "reply.txt", reply->content);
        if (reply->finishReason == "aborted") {
            ++aborts;
            ++turnLimit;   // nothing was applied: the re-prompt is not a repair turn
            partialEdit.clear();
            traceVerdict(tracePath, turn, "cancelled while streaming (" + runaway.kind + ") after " + std::to_string(reply->content.size()) + " characters");
            std::printf("turn %d: cancelled early (%s)\n", turn, runaway.kind.c_str());
            events::note("reply cancelled early: " + runaway.kind);
            continueWith("[your previous reply was stopped and is not repeated here]", false, runaway.instruction);
            continue;
        }
        // A cut-off EDIT reply is continued by appending, like a cut-off file (row 34); the overlap a continuation repeats is trimmed (row 35).
        if (!partialEdit.empty()) { reply->content = partialEdit + continuationText(partialEdit, reply->content); partialEdit.clear(); }
        if (reply->finishReason == "length" && partialPath.empty() && continuations < 3 && cutOffInsideEdit(reply->content)) {
            ++continuations;
            ++turnLimit;
            partialEdit = reply->content;
            traceVerdict(tracePath, turn, "cut off at the token limit inside an edit after " + std::to_string(partialEdit.size()) + " characters: asked to continue it (continuation " + std::to_string(continuations) + ")");
            std::printf("turn %d: edit cut off, continuing (%d)\n", turn, continuations);
            continueWith(reply->content, true, "Your reply was cut off at the token limit in the middle of an edit. It ended with:\n```\n" + (partialEdit.size() > 700 ? partialEdit.substr(partialEdit.size() - 700) : partialEdit) + "\n```\nContinue from exactly that point: reply with only the rest (no header, no repetition, no explanation) and finish it.");
            continue;
        }
        if (!currentFiles.empty() && reply->content.find("=== EDIT:") == std::string::npos && reply->content.find("=== FILE:") == std::string::npos) {
            // A read request (docs/FOLDED_VIEWS.md): the worker asks for folded lines before editing. Answered in the same conversation; it costs no repair turn.
            const auto requests = parseReadRequests(reply->content);
            if (!requests.empty()) {
                std::string answer;
                for (const auto& rq : requests) {
                    const auto f = currentFiles.find(rq.path);
                    answer += f == currentFiles.end() ? "=== " + rq.path + ": not one of the files of this item ===\n" : answerRead(rq, f->second, isPythonPath(rq.path));
                }
                // Reads are bounded (row 36): two read-only replies are answered without using a turn; from the third each one is a spent turn.
                ++reads;
                const bool freeRead = reads <= 2;
                if (freeRead) ++turnLimit;
                traceVerdict(tracePath, turn, "read request: " + std::to_string(requests.size()) + " region(s) shown" + (freeRead ? ", no repair turn used" : ", counted as a turn (reads are used up)"));
                std::printf("turn %d: read request answered (%zu region(s))%s\n", turn, requests.size(), freeRead ? "" : ", turn spent");
                if (turn == turnLimit) break;
                continueWith(reply->content, false, answer + (reads >= 2 ? "You have used your reads: your next reply MUST be an edit (=== EDIT: ... ===) or a complete file, not another read." : "Now reply with your edits (or complete files)."));
                continue;
            }
        }
        Parsed parsed;
        std::string cutPath = partialPath;
        if (!partialPath.empty()) {   // this reply continues the file that was cut off: append it, never rewrite what was written
            const std::string body = continuationText(partialText, reply->content);
            parsed.files = carryFiles;
            parsed.files[partialPath] = partialText + body;
            if (parsed.files[partialPath].empty() || parsed.files[partialPath].back() != '\n') parsed.files[partialPath].push_back('\n');
            parsed.fileChars = body.size();
            std::printf("turn %d: continued %s (%zu more characters)\n", turn, partialPath.c_str(), body.size());
            partialPath.clear();
        } else {
            parsed = parseReply(reply->content, allowed);
            if (const auto h = reply->content.rfind("=== FILE:"); h != std::string::npos) {
                const auto e = reply->content.find("===", h + 9);
                if (e != std::string::npos) { cutPath = reply->content.substr(h + 9, e - h - 9); cutPath.erase(0, cutPath.find_first_not_of(" \t")); cutPath.erase(cutPath.find_last_not_of(" \t\r\n") + 1); }
            }
        }
        if (!currentFiles.empty() && parsed.files.find("") == parsed.files.end()) {   // SEARCH/REPLACE markers inside a file block: the model mixed the two formats; read it as an edit
            std::string asEdits;
            for (const auto& [path, text] : parsed.files)
                if (text.find("<<<<<<< SEARCH") != std::string::npos) asEdits += "=== EDIT: " + path + " ===\n" + text + "\n=== END EDIT ===\n";
            if (!asEdits.empty()) {
                std::string conflictError;
                if (auto merged = applyEdits(asEdits, currentFiles, allowed, conflictError)) {
                    for (auto it = parsed.files.begin(); it != parsed.files.end();) { if (it->second.find("<<<<<<< SEARCH") != std::string::npos) it = parsed.files.erase(it); else ++it; }
                    for (const auto& [path, text] : *merged) if (!parsed.files.count(path)) parsed.files[path] = text;
                    std::printf("turn %d: edit markers inside a file block were read as edits\n", turn);
                } else parsed.files.clear();   // unusable: falls through to the "no usable files" feedback
            }
        }
        if (parsed.files.empty() && !currentFiles.empty() && reply->content.find("=== EDIT:") != std::string::npos) {
            std::string editError;
            if (auto merged = applyAsTransactions(reply->content, currentFiles, allowed, itemId, editError)) {   // one applier for every edit form (LINES, AFTER, SYMBOL, SEARCH)
                parsed.files = *merged;
                parsed.fileChars = reply->content.size();
                std::printf("turn %d: edits applied to %zu file(s)\n", turn, parsed.files.size());
            } else {
                traceVerdict(tracePath, turn, "the edit could not be applied: " + editError);
                std::printf("turn %d: edit rejected: %s\n", turn, editError.c_str());
                if (turn == turnLimit) break;
                continueWith(reply->content, reply->finishReason == "length", "Your edit could not be applied: " + editError + ". Fix that edit, or reply with the complete files in the === FILE: <path> === form.");
                continue;
            }
        } else if (!parsed.files.empty() && !currentFiles.empty()) {  // a whole-file reply may hold only some of the files: keep the others
            auto merged = currentFiles;
            for (const auto& [path, text] : parsed.files) merged[path] = text;
            parsed.files = std::move(merged);
        }
        totIn += static_cast<double>(reply->promptTokens);
        totOut += static_cast<double>(reply->completionTokens);
        const double totalChars = std::max<double>(1.0, static_cast<double>(reply->content.size() + reply->reasoningChars));  // thinking is overhead, not deliverable
        lastWork = static_cast<double>(reply->completionTokens) * static_cast<double>(parsed.fileChars) / totalChars;
        if (reply->predictedMs > 0) decodeTps = static_cast<double>(reply->completionTokens) / (reply->predictedMs / 1000.0);
        if (reply->promptMs > 0) prefillTps = static_cast<double>(reply->promptTokens) / (reply->promptMs / 1000.0);
        lastFinish = reply->finishReason; lastFiles = parsed.files.size(); lastRejected = parsed.rejectedChars;
        std::printf("turn %d, engine %s: %ld prompt tokens (system %ld, card %ld, feedback %ld), %ld completion tokens, finish=%s, %zu file(s), %zu rejected chars\n",
                    turn, engine->name.c_str(), reply->promptTokens, tSystem, tCard, tFeedback, reply->completionTokens, reply->finishReason.c_str(), parsed.files.size(), parsed.rejectedChars);
        if (reply->finishReason == "length" && continuations < 3 && !cutPath.empty() && parsed.files.count(cutPath) && allowed.count(cutPath)) {
            // Cut off at the token limit in the middle of a file: keep everything written so far and ask for the rest, rather than discarding the reply.
            ++continuations;
            partialPath = cutPath;
            partialText = parsed.files[cutPath];
            // The text exactly as it was cut: the parser ends every file with a newline, but a reply cut in the middle of a line must be continued on that line.
            if (!reply->content.empty() && reply->content.back() != '\n' && !partialText.empty() && partialText.back() == '\n') partialText.pop_back();
            carryFiles = parsed.files;
            carryFiles.erase(cutPath);
            const std::string tail = partialText.size() > 700 ? partialText.substr(partialText.size() - 700) : partialText;
            traceVerdict(tracePath, turn, "cut off at the token limit in " + cutPath + " after " + std::to_string(partialText.size()) + " characters: asked to continue it (continuation " + std::to_string(continuations) + ")");
            continueWith(reply->content, true, "Your reply was cut off at the token limit in the middle of " + cutPath + ". It ended with:\n```\n" + tail + "\n```\nContinue that file from exactly that point: reply with only the rest of its content (no header, no repetition, no explanation) and finish it.");
            ++turnLimit;                               // the continuation is an extra turn
            continue;
        }
        if (parsed.files.empty()) {
            traceVerdict(tracePath, turn, "no usable files in the reply (finish " + reply->finishReason + ")");
            if (turn == turnLimit) break;
            continueWith(reply->content, reply->finishReason == "length", std::string{reply->finishReason == "length" ? "Your reply was cut off at the token limit before it finished. Write less: shorter code, no comments, fewer helpers. " : ""} + "Your reply contained none of the requested files. Reply with the complete content of every file in the required format: === FILE: <path> === ... === END FILE ===");
            continue;
        }
        std::string filesKey;
        for (const auto& [path, text] : parsed.files) filesKey += path + "\n" + text + "\n";
        if (turn > 0 && filesKey == previousFiles) {  // the same files again: another turn would repeat the same mistake, hand the item back to the ladder
            stagnated = true;
            traceVerdict(tracePath, turn, "stagnated: the repair turn returned identical files, so the self-check loop stopped");
            events::note("stagnated: the repair turn returned identical files");
            std::printf("turn %d returned files identical to the previous turn: stopping the self-check loop (stagnated)\n", turn);
            break;
        }
        previousFiles = filesKey;
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, text] : parsed.files) if (!writeFile(out / path, text)) { std::fprintf(stderr, "llm-agent: cannot write %s\n", path.c_str()); return 1; }
        haveFiles = true;
        currentFiles = parsed.files;
        const Harness* hs = findHarness(plan->harness);
        if (!hs || !hs->gate) break;
        const int rc = hs->gate({"--workflow-dir", dir.string(), "--item", itemId, "--attempt", std::to_string(attempt), "--no-publish", "1"});
        lastGate = slurp(attemptDir / "gate.txt");
        traceVerdict(tracePath, turn, rc == 0 ? "self-check passed: the item's tests pass" : lastGate);
        events::note(rc == 0 ? "self-check passed" : "self-check failed");
        if (rc == 0) { verified = true; break; }
        if (turn == turnLimit) break;
        // The files as they now stand, numbered so the fix can name lines. The slot is small (the first real use overflowed it: 7,913 prompt tokens of 8,192 left
        // 279 for the reply), so the view gets only what the window still has after the system block, the card with its draft, the gate text and the reply room.
        const std::size_t fixedChars = convo[0].content.size() + convo[1].content.size();
        const long availTokens = static_cast<long>(engine->contextTokens) - maxTokens - 300;
        const long availChars = static_cast<long>(static_cast<double>(availTokens) * 3.2) - static_cast<long>(fixedChars) - 1500 - 1200;
        const std::size_t perFile = static_cast<std::size_t>(std::clamp(availChars / std::max<long>(1, static_cast<long>(currentFiles.size())), 800L, 9000L));
        std::string numbered;
        for (const auto& [path, text] : currentFiles) numbered += "=== DRAFT: " + path + " ===\n" + draftView(path, text, lastGate, perFile) + "=== END DRAFT ===\n";
        // The earlier reply is not repeated: the numbered files are the current state, and a long edit reply would only crowd the window.
        continueWith("[your previous reply is not repeated: the files as they now stand are shown below]", false, std::string{reply->finishReason == "length" ? "Your reply was cut off at the token limit, so the file is incomplete. " : ""} + "Your files were checked by running the tests and failed:\n" + clip(lastGate, 1500) + "\n" + numbered + "Fix exactly these failures. " + std::string{kEditSyntax});
    }
    events::metric("tokens.in", totIn);
    events::metric("tokens.out", totOut);
    events::metric("tokens.work", lastWork);
    events::metric("tokens.waste", totOut - lastWork);
    events::metric("agent.turns", static_cast<double>(turns));
    events::metric("agent.selfcheck_passed", verified ? 1.0 : 0.0);
    events::metric("agent.stagnated", stagnated ? 1.0 : 0.0);
    if (decodeTps > 0) events::metric("engine.decode_tps", decodeTps);
    if (prefillTps > 0) events::metric("engine.prefill_tps", prefillTps);
    if (const Harness* hs = findHarness(plan->harness); hs && hs->scaffoldBytes) events::metric("harness.bytes", static_cast<double>(hs->scaffoldBytes()));
    std::printf("done after %d turn(s): %sself-check %s, finish=%s, %zu file(s), %zu rejected chars\n", turns, stagnated ? "stagnated, " : "", verified ? "passed" : "not passed", lastFinish.c_str(), lastFiles, lastRejected);
    if (!haveFiles) {
        std::fprintf(stderr, "llm-agent: no turn produced the requested files\n");
        harnessutil::recordAttemptFailure(attemptDir, "agent", "format", "no reply held the requested files in the reply format: " + std::to_string(turns) + " turn(s), last finish " + lastFinish);
        return 3;
    }
    events::progress(turnLimit + 1, turnLimit + 1, "written");
    {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::ostringstream m;
        m << "{\"treatment\":\"" << treatment << "\",\"engine\":\"" << engine->name << "\",\"tokensIn\":" << totIn << ",\"tokensOut\":" << totOut
          << ",\"tokensWork\":" << static_cast<long>(lastWork) << ",\"turns\":" << turns << ",\"selfcheck\":" << (verified ? "true" : "false") << ",\"seconds\":" << secs << "}";
        writeFile(attemptDir / "agent.json", m.str());
    }
    if (!verified && lastFinish == "length") {   // cut off at the token limit and not passing: it does not fit one attempt, so hand it back to be divided at once
        events::note("reply cut off at the token limit: the item is too big for one attempt");
        std::printf("size verdict: split (the reply was cut off at the %d token limit)\n", maxTokens);
        harnessutil::recordAttemptFailure(attemptDir, "agent", "oversize", "the reply was cut off at the " + std::to_string(maxTokens) + " token limit and the item's tests do not pass: it does not fit one attempt");
        return 4;
    }
    return 0;
}

}  // namespace sub0squad
