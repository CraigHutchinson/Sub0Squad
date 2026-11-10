#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/sandbox.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
bool writeFile(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << text;
    return static_cast<bool>(o);
}
std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}
std::string clip(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(0, n) + "\n... (truncated)"; }

std::string systemBlock(const std::string& persona, const std::string& harnessBrief)
{
    std::string s = "You are the " + persona + " on a small software squad: you own one small, focused piece of a whole solution: be the expert and do it as well as it can be done, and keep to your contract and the exact interfaces you are given so the other pieces fit. ";
    if (persona == "tester") s += "Write tests for the contract you are given. Do not write the implementation. ";
    else s += "Implement the contract so that the provided tests pass. ";
    s += "Write only the files you are asked to write.\n"
         "Reply with the complete content of every file in exactly this format and nothing else (no explanations, no markdown fences):\n"
         "=== FILE: <path> ===\n<file content>\n=== END FILE ===\n";
    if (!harnessBrief.empty()) s += harnessBrief + "\n";
    return s;
}

// Names of the tests a test file defines: test('name' ... / it("name" ...
std::string testNames(const std::string& source)
{
    std::string out;
    int n = 0;
    for (std::size_t pos = 0; pos < source.size();) {
        std::size_t hit = std::string::npos;
        for (const char* key : {"test(", "it("}) { const auto h = source.find(key, pos); if (h != std::string::npos && h < hit) hit = h; }
        if (hit == std::string::npos) break;
        std::size_t q = source.find_first_of("'\"`", hit);
        if (q == std::string::npos || q - hit > 12) { pos = hit + 3; continue; }
        const auto e = source.find(source[q], q + 1);
        if (e == std::string::npos) break;
        out += "- " + source.substr(q + 1, e - q - 1) + "\n";
        ++n;
        pos = e + 1;
    }
    return n ? out : std::string{};
}

struct Parsed { std::map<std::string, std::string> files; std::size_t rejectedChars = 0, proseChars = 0, fileChars = 0; };

// Parse "=== FILE: path ===" blocks; tolerate a missing END and markdown fences around the content.
Parsed parseReply(const std::string& reply, const std::set<std::string>& allowed)
{
    Parsed p;
    std::size_t pos = 0;
    std::size_t consumed = 0;
    for (;;) {
        const auto start = reply.find("=== FILE:", pos);
        if (start == std::string::npos) break;
        const auto nameEnd = reply.find("===", start + 9);
        if (nameEnd == std::string::npos) break;
        std::string path = reply.substr(start + 9, nameEnd - start - 9);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t\r\n") + 1);
        std::size_t bodyStart = reply.find('\n', nameEnd);
        if (bodyStart == std::string::npos) break;
        ++bodyStart;
        std::size_t end = reply.find("=== END FILE ===", bodyStart);
        const std::size_t nextFile = reply.find("=== FILE:", bodyStart);
        std::size_t after;
        if (end == std::string::npos || (nextFile != std::string::npos && nextFile < end)) { end = nextFile == std::string::npos ? reply.size() : nextFile; after = end; }
        else after = end + std::string{"=== END FILE ==="}.size();
        std::string body = reply.substr(bodyStart, end - bodyStart);
        if (body.rfind("```", 0) == 0) { const auto nl = body.find('\n'); body = nl == std::string::npos ? "" : body.substr(nl + 1); }  // opening fence
        for (const std::string tail : {"\n```\n", "\n```", "```\n", "```"}) {
            if (body.size() >= tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0) { body.erase(body.size() - tail.size()); body.push_back('\n'); break; }
        }
        if (!body.empty() && body.back() != '\n') body.push_back('\n');
        consumed += after - start;
        if (allowed.count(path) && !body.empty()) { p.files[path] = body; p.fileChars += body.size(); }
        else p.rejectedChars += body.size();
        pos = after;
    }
    p.proseChars = reply.size() > consumed ? reply.size() - consumed : 0;
    return p;
}

std::set<std::string> closure(const Plan& plan, const std::string& id)
{
    std::set<std::string> seen;
    std::vector<std::string> work{id};
    while (!work.empty()) {
        const WorkItem* it = plan.find(work.back());
        work.pop_back();
        if (!it) continue;
        for (const auto& d : it->deps) if (seen.insert(d).second) work.push_back(d);
    }
    return seen;
}

}  // namespace

int runLlmAgent(const std::vector<std::string>& args)
{
    const fs::path dir = arg(args, "workflow-dir");
    const std::string itemId = arg(args, "item"), tier = arg(args, "tier", "local");
    const int attempt = std::atoi(arg(args, "attempt", "1").c_str());
    const int maxTokens = std::atoi(arg(args, "max-tokens", "3000").c_str());
    const int timeoutSec = std::atoi(arg(args, "timeout", "1700").c_str());
    const int repairTurns = std::max(0, std::atoi(arg(args, "repair-turns", "0").c_str()));  // extra turns after the first: run the tests, feed failures back, repeat
    const long sizeBudget = std::atol(arg(args, "size-budget", "0").c_str());  // plan phase: above this estimate an item with tests is handed back to be divided
    const std::string treatment = arg(args, "treatment", "baseline");  // baseline | index | planfirst (docs/OPTIMIZATION.md)
    const auto began = std::chrono::steady_clock::now();

    if (tier == "frontier-stub") {  // no frontier provider exists: the stub tier is the reference implementation, recorded as a stub
        std::vector<std::string> a = {"--workflow-dir", dir.string(), "--item", itemId, "--tier", tier, "--attempt", std::to_string(attempt), "--delay-ms", "50"};
        return runMockAgent(a);
    }
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-agent: %s\n", engine.error().c_str()); return 64; }
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::fprintf(stderr, "llm-agent: %s\n", plan.error().c_str()); return 1; }
    WorkItem effective;
    if (const WorkItem* found = plan->find(itemId)) effective = *found;
    const WorkItem* item = plan->find(itemId) ? &effective : nullptr;
    if (item) (void)readCardOverride(dir / "items" / itemId / "card-override.json", effective.card);  // a rearchitect job rewrote the card
    if (!item) { std::fprintf(stderr, "llm-agent: no item '%s'\n", itemId.c_str()); return 1; }

    // ---- the prompt: a fixed system block, the work card, included files from accepted dependencies, and the previous failure only ----
    const std::string system = systemBlock(item->persona, plan->harness == "web-static" ? webHarnessBrief() : std::string{});
    std::ostringstream card;
    card << "Task: " << item->goal << "\nFiles to write: ";
    for (std::size_t i = 0; i < item->files.size(); ++i) card << (i ? ", " : "") << item->files[i];
    card << "\n\nContext: " << item->card.context << "\n\nContract:\n" << item->card.contract << "\n";
    if (!item->card.acceptance.empty()) { card << "\nAcceptance:\n"; for (const auto& x : item->card.acceptance) card << "- " << x << "\n"; }
    long testCount = 0;
    for (const auto& inc : item->card.include) {
        std::string content;
        for (const auto& dep : closure(*plan, itemId)) { const fs::path f = dir / "items" / dep / "accepted" / inc; if (fs::exists(f)) { content = slurp(f); break; } }
        if (content.empty()) continue;
        const std::string allNames = testNames(content);
        testCount += std::count(allNames.begin(), allNames.end(), '\n');
        const std::string names = treatment == "index" ? allNames : std::string{};
        if (!names.empty()) card << "\nThe provided tests in " << inc << " are named as follows (the file itself is not shown; implement the contract so tests with these names pass):\n" << names;
        else card << "\nProvided file " << inc << ":\n=== FILE: " << inc << " ===\n" << clip(content, 7000) << "\n=== END FILE ===\n";
    }
    if (!item->card.notes.empty()) { card << "\nNotes:\n"; for (const auto& x : item->card.notes) card << "- " << x << "\n"; }
    std::string feedback;
    const fs::path disputeFile = dir / "items" / itemId / "dispute.txt";
    if (fs::exists(disputeFile)) {  // the tests of this item were disputed by a failing developer: this is the feedback, consumed once
        feedback = clip(slurp(disputeFile), 1500);
        std::error_code mv;
        fs::rename(disputeFile, dir / "items" / itemId / ("dispute-used-" + std::to_string(attempt) + ".txt"), mv);
    } else if (attempt > 1 && fs::exists(dir / "items" / itemId / ("attempt-" + std::to_string(attempt - 1)))) {
        // Start the retry from the BEST earlier attempt (fewest failing tests, the latest on a tie), not from nothing: a rewrite from scratch
        // regressed a 64 of 67 pass to 59 of 67 in the first real run. The model gets that attempt's files and its failures.
        int best = attempt - 1, bestFail = 1 << 30;
        for (int k = 1; k < attempt; ++k) {
            const std::string gk = slurp(dir / "items" / itemId / ("attempt-" + std::to_string(k)) / "gate.txt");
            int f = 1 << 29;
            if (gk.rfind("GATE FAIL: ", 0) == 0 && gk.find(" of ") != std::string::npos) f = std::atoi(gk.c_str() + 11);
            if (f <= bestFail) { bestFail = f; best = k; }
        }
        const fs::path bestDir = dir / "items" / itemId / ("attempt-" + std::to_string(best));
        const std::string g = slurp(bestDir / "gate.txt");
        std::string code;
        if (!g.empty() && bestFail < (1 << 29))
            for (const auto& f : item->files) { const std::string c = slurp(bestDir / "out" / f); if (!c.empty()) code += "=== FILE: " + f + " ===\n" + clip(c, 7000) + "\n=== END FILE ===\n"; }
        if (!g.empty() && !code.empty()) feedback = "Your best earlier version (attempt " + std::to_string(best) + ") is below. It was rejected:\n" + clip(g, 1200) + "\n" + code + "Change only what these failures need, keep everything else, and write the complete files again.\n";
        else if (!g.empty()) feedback = "Your previous attempt was rejected:\n" + clip(g, 1200) + "\nFix exactly this and write the complete files again.\n";
        else feedback = "Your previous attempt produced no usable files. Reply in the required file format.\n";
    }
    // ---- plan phase: size the work before writing any of it. Calibrated on the first real run: about 350 tokens plus 30 per test
    // (69 tests came to about 2,400 tokens). Too big for the budget and still divisible: exit 4 hands the item back to be divided.
    const long estimate = 350 + 30 * testCount;
    events::metric("size.estimate", static_cast<double>(estimate));
    if (sizeBudget > 0 && attempt == 1 && feedback.empty() && testCount > 0 && estimate > sizeBudget && std::count(itemId.begin(), itemId.end(), '.') < 2) {
        writeFile(dir / "items" / itemId / "size.json", "{\"estimateTokens\":" + std::to_string(estimate) + ",\"budget\":" + std::to_string(sizeBudget) + ",\"tests\":" + std::to_string(testCount) + ",\"verdict\":\"split\"}\n");
        writeFile(dir / "items" / itemId / "attempt-1" / "gate.txt", "PLAN PHASE: not attempted. Estimated " + std::to_string(estimate) + " completion tokens for " + std::to_string(testCount) + " tests, over the budget of " + std::to_string(sizeBudget) + ". Divide the item so each part is small.\n");
        events::note("plan phase: estimated " + std::to_string(estimate) + " tokens against a budget of " + std::to_string(sizeBudget) + ": split before implementing");
        std::printf("size verdict: split (estimate %ld tokens for %ld tests, budget %ld)\n", estimate, testCount, sizeBudget);
        return 4;
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
    double totIn = static_cast<double>(extraIn), totOut = static_cast<double>(extraOut), lastWork = 0, decodeTps = 0, prefillTps = 0;
    int turns = 0;
    bool verified = false, haveFiles = false;
    std::string lastFinish, lastGate, previousFiles;
    bool stagnated = false;
    std::size_t lastFiles = 0, lastRejected = 0;
    for (int turn = 0; turn <= repairTurns; ++turn) {
        events::progress(turn, repairTurns + 1, turn == 0 ? "generating" : "repairing");
        EngineProfile turnEngine = *engine;
        if (engine->seed >= 0) {  // a reproducible run: one derived seed per (item, attempt, turn), so retries differ from each other but a rerun replays them
            std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(engine->seed);
            for (const char c : itemId + "#" + std::to_string(attempt) + "#" + std::to_string(turn)) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
            turnEngine.seed = static_cast<long long>(h & 0x7fffffffull);
            std::printf("seed for turn %d: %lld (run seed %lld)\n", turn, turnEngine.seed, engine->seed);
        }
        auto reply = chatCompleteMessages(turnEngine, convo, maxTokens, timeoutSec);
        if (!reply) {
            std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str());
            if (!haveFiles) return 2;
            break;  // keep the files of the earlier turn
        }
        ++turns;
        writeFile(attemptDir / ("reply-" + std::to_string(turn) + ".txt"), reply->content);
        writeFile(attemptDir / "reply.txt", reply->content);
        const Parsed parsed = parseReply(reply->content, allowed);
        totIn += static_cast<double>(reply->promptTokens);
        totOut += static_cast<double>(reply->completionTokens);
        const double totalChars = std::max<double>(1.0, static_cast<double>(reply->content.size() + reply->reasoningChars));  // thinking is overhead, not deliverable
        lastWork = static_cast<double>(reply->completionTokens) * static_cast<double>(parsed.fileChars) / totalChars;
        if (reply->predictedMs > 0) decodeTps = static_cast<double>(reply->completionTokens) / (reply->predictedMs / 1000.0);
        if (reply->promptMs > 0) prefillTps = static_cast<double>(reply->promptTokens) / (reply->promptMs / 1000.0);
        lastFinish = reply->finishReason; lastFiles = parsed.files.size(); lastRejected = parsed.rejectedChars;
        std::printf("turn %d, engine %s: %ld prompt tokens (system %ld, card %ld, feedback %ld), %ld completion tokens, finish=%s, %zu file(s), %zu rejected chars\n",
                    turn, engine->name.c_str(), reply->promptTokens, tSystem, tCard, tFeedback, reply->completionTokens, reply->finishReason.c_str(), parsed.files.size(), parsed.rejectedChars);
        if (parsed.files.empty()) {
            if (turn == repairTurns) break;
            convo.push_back({"assistant", reply->content});
            convo.push_back({"user", "Your reply contained none of the requested files. Reply with the complete content of every file in the required format: === FILE: <path> === ... === END FILE ==="});
            continue;
        }
        std::string filesKey;
        for (const auto& [path, text] : parsed.files) filesKey += path + "\n" + text + "\n";
        if (turn > 0 && filesKey == previousFiles) {  // the same files again: another turn would repeat the same mistake, hand the item back to the ladder
            stagnated = true;
            events::note("stagnated: the repair turn returned identical files");
            std::printf("turn %d returned files identical to the previous turn: stopping the self-check loop (stagnated)\n", turn);
            break;
        }
        previousFiles = filesKey;
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, text] : parsed.files) if (!writeFile(out / path, text)) { std::fprintf(stderr, "llm-agent: cannot write %s\n", path.c_str()); return 1; }
        haveFiles = true;
        if (plan->harness != "web-static") break;
        const int rc = runHarnessGate({"--workflow-dir", dir.string(), "--item", itemId, "--attempt", std::to_string(attempt), "--no-publish", "1"});
        lastGate = slurp(attemptDir / "gate.txt");
        events::note(rc == 0 ? "self-check passed" : "self-check failed");
        if (rc == 0) { verified = true; break; }
        if (turn == repairTurns) break;
        convo.push_back({"assistant", reply->content});
        convo.push_back({"user", "Your files were checked by running the tests and failed:\n" + clip(lastGate, 1500) + "\nFix exactly these failures and reply with the complete files again, in the same format."});
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
    if (plan->harness == "web-static") events::metric("harness.bytes", static_cast<double>(webScaffoldBytes()));
    std::printf("done after %d turn(s): %sself-check %s, finish=%s, %zu file(s), %zu rejected chars\n", turns, stagnated ? "stagnated, " : "", verified ? "passed" : "not passed", lastFinish.c_str(), lastFiles, lastRejected);
    if (!haveFiles) { std::fprintf(stderr, "llm-agent: no turn produced the requested files\n"); return 3; }
    events::progress(repairTurns + 1, repairTurns + 1, "written");
    {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::ostringstream m;
        m << "{\"treatment\":\"" << treatment << "\",\"engine\":\"" << engine->name << "\",\"tokensIn\":" << totIn << ",\"tokensOut\":" << totOut
          << ",\"tokensWork\":" << static_cast<long>(lastWork) << ",\"turns\":" << turns << ",\"selfcheck\":" << (verified ? "true" : "false") << ",\"seconds\":" << secs << "}";
        writeFile(attemptDir / "agent.json", m.str());
    }
    return 0;
}

}  // namespace sub0squad
