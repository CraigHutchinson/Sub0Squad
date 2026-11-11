#include "sub0squad/edits.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/policy.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/sandbox.hpp"

#include <algorithm>
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

namespace {
/// The fixed-system-block description of a project harness (empty when the plan has none).
std::string harnessBrief(const std::string& name) { const Harness* h = findHarness(name); return h && h->brief ? h->brief() : std::string{}; }
}  // namespace

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
    // Persona-neutral text first (the same for every item, so the server's prompt cache shares it), the persona sentence last.
    std::string s = "Reply with the complete content of every file you are asked for, in exactly this format and nothing else (no explanations, no markdown fences):\n"
                    "=== FILE: <path> ===\n<file content>\n=== END FILE ===\n";
    if (!harnessBrief.empty()) s += harnessBrief + "\n";
    s += "You are the " + persona + " on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done, "
         "keep to your contract and the exact interfaces you are given so the other pieces fit, and write only the files you are asked to write. ";
    s += persona == "tester" ? "Write tests for the contract; do not write the implementation.\n" : "Implement the contract so that the provided tests pass.\n";
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

const char* kEditSyntax =
    "Reply with EDITS, not the whole files, in this form (one block per change; SEARCH must match the current text exactly and occur once):\n"
    "=== EDIT: <path> ===\n<<<<<<< SEARCH\n<exact lines to replace>\n=======\n<new lines>\n>>>>>>> REPLACE\n=== END EDIT ===\n"
    "If most of a file changes, reply with the complete file in the usual === FILE: <path> === form instead.";

// The work card as the worker reads it: the task, the files, the context, the contract, the acceptance list, the provided tests of accepted dependencies
// (whole, or by name with the `index` treatment) and the notes. Counts the tests it showed.
std::string buildCard(const fs::path& dir, const Plan& plan, const WorkItem& itemRef, const std::string& itemId, const std::string& treatment, long& testCount)
{
    const WorkItem* item = &itemRef;
    std::ostringstream card;
    card << "Task: " << item->goal << "\nFiles to write: ";
    for (std::size_t i = 0; i < item->files.size(); ++i) card << (i ? ", " : "") << item->files[i];
    card << "\n\nContext: " << item->card.context << "\n\nContract:\n" << item->card.contract << "\n";
    if (!item->card.acceptance.empty()) { card << "\nAcceptance:\n"; for (const auto& x : item->card.acceptance) card << "- " << x << "\n"; }
    for (const auto& inc : item->card.include) {
        std::string content;
        for (const auto& dep : closure(plan, itemId)) { const fs::path f = dir / "items" / dep / "accepted" / inc; if (fs::exists(f)) { content = slurp(f); break; } }
        if (content.empty()) continue;
        const std::string allNames = testNames(content);
        testCount += std::count(allNames.begin(), allNames.end(), '\n');
        const std::string names = treatment == "index" ? allNames : std::string{};
        if (!names.empty()) card << "\nThe provided tests in " << inc << " are named as follows (the file itself is not shown; implement the contract so tests with these names pass):\n" << names;
        else card << "\nProvided file " << inc << ":\n=== FILE: " << inc << " ===\n" << clip(content, 7000) << "\n=== END FILE ===\n";
    }
    if (!item->card.notes.empty()) { card << "\nNotes:\n"; for (const auto& x : item->card.notes) card << "- " << x << "\n"; }
    return card.str();
}

// ---- forked sessions: write the skeleton first, then delegate each function to a session of its own --------------------------------------------------------
// One primer session writes every file as a skeleton (imports, constants, every function declared with its exact signature, each body only
// `throw new Error('not implemented: <name>');`). The harness finds the stubs; each becomes a fork: the same primed conversation (system block, card, the skeleton as the
// assistant's own reply) plus "you are working on <name>, the others are written separately". A fork answers with the body only, which the harness puts in place of the stub.
// Instead of one ever-growing context there are small sessions that share the primed prefix; a failure goes back to the sessions it points at, each continuing its own.
struct ForkUnit
{
    std::string name, file, stubLine;
    std::string body;
    std::vector<ChatMessage> convo;
    std::string reply, finish, error;
    long promptTokens = 0, completionTokens = 0, cachedTokens = 0;
};

std::string tidyBody(std::string t, const std::string& name)
{
    std::string out;
    std::istringstream in(t);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("=== FILE:", 0) == 0 || line.rfind("=== END FILE", 0) == 0 || line.rfind("=== EDIT:", 0) == 0 || line.rfind("=== END EDIT", 0) == 0) continue;
        out += line + "\n";
    }
    if (out.rfind("```", 0) == 0) { const auto nl = out.find('\n'); out = nl == std::string::npos ? "" : out.substr(nl + 1); }
    for (const std::string tail : {"\n```\n", "```\n"}) if (out.size() >= tail.size() && out.compare(out.size() - tail.size(), tail.size(), tail) == 0) { out.erase(out.size() - tail.size()); out.push_back('\n'); break; }
    // a model that returns the whole function: keep what is inside its braces
    const std::string last = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
    const auto firstNl = out.find_first_not_of(" \t\n");
    if (firstNl != std::string::npos) {
        const std::string head = out.substr(firstNl, 160);
        if ((head.rfind("function " + last, 0) == 0 || head.rfind("export function " + last, 0) == 0 || head.rfind("async function " + last, 0) == 0) && out.find('{') != std::string::npos && out.rfind('}') != std::string::npos && out.rfind('}') > out.find('{')) {
            out = out.substr(out.find('{') + 1, out.rfind('}') - out.find('{') - 1);
            while (!out.empty() && (out.front() == '\n' || out.front() == '\r')) out.erase(out.begin());
            if (out.empty() || out.back() != '\n') out.push_back('\n');
        }
    }
    return out;
}

}  // namespace

int runLlmFork(const std::vector<std::string>& args)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const fs::path dir = arg(args, "workflow-dir");
    const std::string itemId = arg(args, "item");
    const int attempt = std::atoi(arg(args, "attempt", "1").c_str());
    const int maxTokens = std::atoi(arg(args, "max-tokens", "3500").c_str());
    const int timeoutSec = std::atoi(arg(args, "timeout", "3600").c_str());
    const int rounds = std::max(0, std::atoi(arg(args, "repair-turns", "2").c_str()));
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-fork: %s\n", engine.error().c_str()); return 64; }
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan || !plan->find(itemId)) { std::fprintf(stderr, "llm-fork: cannot load the plan or item\n"); return 1; }
    WorkItem item = *plan->find(itemId);
    (void)readCardOverride(dir / "items" / itemId / "card-override.json", item.card);
    const std::string system = systemBlock(item.persona, harnessBrief(plan->harness));
    long testCount = 0;
    const std::string card = buildCard(dir, *plan, item, itemId, "baseline", testCount);
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt)), out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    const std::set<std::string> allowed(item.files.begin(), item.files.end());
    const auto seedFor = [&](const std::string& label) { EngineProfile e = *engine; if (engine->seed >= 0) { std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(engine->seed); for (const char c : label) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; } e.seed = static_cast<long long>(h & 0x7fffffffull); } return e; };

    // 1. the primer session: the skeleton
    const std::string primer = card + "\nWrite the skeleton first, with no implementation yet. For every file to write give the complete file: its imports, its constants, and every function or class declared with its exact signature and a one-line comment saying what it does. "
                               "Each function body is only `throw new Error('not implemented: <name>');` where <name> is the function's name (Class.method for a method). Reply in the file format.";
    std::vector<ChatMessage> primerConvo = {{"system", system}, {"user", primer}};
    traceBegin(attemptDir / "conversation-skeleton.md", "Skeleton session of " + itemId, *engine, maxTokens);
    traceSent(attemptDir / "conversation-skeleton.md", 0, primerConvo, 0, -1);
    auto skel = chatCompleteMessages(seedFor("skeleton"), primerConvo, maxTokens, timeoutSec);
    if (!skel) { std::fprintf(stderr, "llm-fork: the skeleton call failed: %s\n", skel.error().c_str()); return 2; }
    traceReply(attemptDir / "conversation-skeleton.md", 0, *skel);
    std::printf("fork skeleton: %ld prompt tokens (%ld cached), %ld completion tokens, finish=%s\n", skel->promptTokens, skel->cachedTokens, skel->completionTokens, skel->finishReason.c_str());
    Parsed parsed = parseReply(skel->content, allowed);
    if (parsed.files.empty()) { std::printf("the skeleton reply held none of the files\n"); return 2; }
    std::map<std::string, std::string> skeleton = parsed.files;
    for (const auto& f : item.files) if (!skeleton.count(f)) std::printf("note: the skeleton has no %s\n", f.c_str());

    // 2. find the stubs: each becomes a fork
    std::vector<ForkUnit> units;
    for (const auto& [file, text] : skeleton) {
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const auto m = line.find("not implemented:");
            if (m == std::string::npos) continue;
            std::string name = line.substr(m + 16);
            name.erase(0, name.find_first_not_of(" \t"));
            name = name.substr(0, name.find_first_of("'\"` )"));
            if (name.empty() || std::any_of(units.begin(), units.end(), [&](const ForkUnit& u) { return u.name == name && u.file == file; })) continue;
            ForkUnit u; u.name = name; u.file = file; u.stubLine = line;
            units.push_back(std::move(u));
        }
    }
    std::printf("skeleton: %zu file(s), %zu function(s) to delegate\n", skeleton.size(), units.size());
    std::string list;
    for (const auto& u : units) list += "- " + u.name + " (" + u.file + ")\n";
    const auto render = [&] {
        std::map<std::string, std::string> files = skeleton;
        for (auto& [file, text] : files) {
            std::istringstream in(text);
            std::string rebuilt;
            for (std::string line; std::getline(in, line);) {
                const ForkUnit* hit = nullptr;
                for (const auto& u : units) if (u.file == file && u.stubLine == line && !u.body.empty()) hit = &u;
                if (!hit) { rebuilt += line + "\n"; continue; }
                const std::string indent = line.substr(0, line.find_first_not_of(" \t"));
                std::istringstream body(hit->body);
                for (std::string b; std::getline(body, b);) rebuilt += (b.empty() ? std::string{} : indent + b) + "\n";
            }
            text = rebuilt;
        }
        return files;
    };
    const auto runGate = [&](const std::map<std::string, std::string>& files, std::string& gateText) {
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        for (const auto& [path, text] : files) (void)writeFile(out / path, text);
        const Harness* hs = findHarness(plan->harness);
        const int rc = hs && hs->gate ? hs->gate({"--workflow-dir", dir.string(), "--item", itemId, "--attempt", std::to_string(attempt), "--no-publish", "1"}) : 1;
        gateText = slurp(attemptDir / "gate.txt");
        return rc == 0;
    };
    std::string gate;
    if (units.empty()) { std::printf("no stubs in the skeleton: nothing to delegate\n"); std::printf("RESULT: %s\n", runGate(skeleton, gate) ? "PASS" : "FAIL"); return 1; }

    // 3. one fork per function, side by side: the primed conversation plus "you are working on X, the others are written separately"
    const auto askFork = [&](ForkUnit& u, const std::string& tag) {
        const auto trace = attemptDir / ("conversation-" + [&] { std::string n = u.name; for (char& c : n) if (c == '.' || c == '/') c = '_'; return n; }() + ".md");
        traceSent(trace, tag == "fork" ? 0 : 1, u.convo, tag == "fork" ? 0 : u.convo.size() - 2, -1);
        auto r = chatCompleteMessages(seedFor(u.name + tag), u.convo, maxTokens, timeoutSec);
        if (!r) { u.error = r.error(); traceVerdict(trace, 0, "the engine call failed: " + r.error()); return; }
        traceReply(trace, 0, *r);
        u.reply = r->content; u.finish = r->finishReason;
        u.promptTokens += r->promptTokens; u.completionTokens += r->completionTokens; u.cachedTokens += r->cachedTokens;
        u.body = tidyBody(r->content, u.name);
    };
    {
        std::vector<std::thread> pool;
        for (auto& u : units) {
            u.convo = {{"system", system}, {"user", primer}, {"assistant", skel->content},
                       {"user", "You are working on `" + u.name + "` in " + u.file + ". The other functions are written separately by others, at the same time; use them only through the signatures in the skeleton above. "
                                "Reply with only the body of `" + u.name + "`: the lines that replace its `throw new Error('not implemented: " + u.name + "');` line, nothing else (no signature, no braces around it, no explanation). Functions in this item:\n" + list}};
            pool.emplace_back([&askFork, &u] { askFork(u, "fork"); });
        }
        for (auto& t : pool) t.join();
    }
    for (const auto& u : units) std::printf("fork %s: %ld prompt tokens (%ld cached), %ld completion tokens, finish=%s%s\n", u.name.c_str(), u.promptTokens, u.cachedTokens, u.completionTokens, u.finish.c_str(), u.error.empty() ? "" : (" ERROR " + u.error).c_str());

    // 4. put the bodies in, run the item's tests; a failure goes back to the sessions it points at, each continuing its own
    bool ok = runGate(render(), gate);
    int round = 0;
    while (!ok && round < rounds) {
        ++round;
        std::vector<ForkUnit*> implicated;
        for (auto& u : units) { const std::string last = u.name.substr(u.name.rfind('.') == std::string::npos ? 0 : u.name.rfind('.') + 1); if (gate.find(last) != std::string::npos) implicated.push_back(&u); }
        if (implicated.empty()) for (auto& u : units) implicated.push_back(&u);
        std::printf("round %d: the tests failed; %zu function(s) go back to their sessions\n", round, implicated.size());
        std::vector<std::thread> pool;
        for (ForkUnit* u : implicated) {
            u->convo.push_back({"assistant", u->reply});
            u->convo.push_back({"user", "The combined result was checked by running the tests and failed:\n" + clip(gate, 1500) + "\nThe body you wrote for `" + u->name + "` may be at fault. Reply with the corrected body of `" + u->name + "` only (the complete body, same form as before)."});
            pool.emplace_back([&askFork, u] { askFork(*u, "round"); });
        }
        for (auto& t : pool) t.join();
        for (const ForkUnit* u : implicated) std::printf("fork %s (round %d): %ld completion tokens so far, finish=%s\n", u->name.c_str(), round, u->completionTokens, u->finish.c_str());
        ok = runGate(render(), gate);
    }
    std::printf("RESULT: %s after %d round(s)\n", ok ? "PASS" : "FAIL", round);
    return ok ? 0 : 1;
}

namespace {

}  // namespace

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
    {
        std::string drafts, draftGate, origin;
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
            drafts += "=== FILE: " + f + " ===\n" + clip(text, 7000) + "\n=== END FILE ===\n";
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
    int continuations = 0;
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
        auto reply = chatCompleteMessages(turnEngine, convo, maxTokens, timeoutSec);
        if (!reply) {
            traceVerdict(tracePath, turn, "the engine call failed: " + reply.error());
            std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str());
            if (!haveFiles) return 2;
            break;  // keep the files of the earlier turn
        }
        ++turns;
        traceReply(tracePath, turn, *reply);
        writeFile(attemptDir / ("reply-" + std::to_string(turn) + ".txt"), reply->content);
        writeFile(attemptDir / "reply.txt", reply->content);
        Parsed parsed;
        std::string cutPath = partialPath;
        if (!partialPath.empty()) {   // this reply continues the file that was cut off: append it, never rewrite what was written
            std::string body;
            std::istringstream in(reply->content);
            for (std::string line; std::getline(in, line);) {
                if (line.rfind("=== FILE:", 0) == 0 || line.rfind("=== END FILE", 0) == 0) continue;
                body += line + "\n";
            }
            if (body.rfind("```", 0) == 0) { const auto nl = body.find('\n'); body = nl == std::string::npos ? "" : body.substr(nl + 1); }
            for (const std::string tail : {"\n```\n", "```\n"}) if (body.size() >= tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0) { body.erase(body.size() - tail.size()); body.push_back('\n'); break; }
            parsed.files = carryFiles;
            parsed.files[partialPath] = partialText + body;
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
            if (auto merged = applyEdits(reply->content, currentFiles, allowed, editError)) {
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
        continueWith(reply->content, reply->finishReason == "length", std::string{reply->finishReason == "length" ? "Your reply was cut off at the token limit, so the file is incomplete. " : ""} + "Your files were checked by running the tests and failed:\n" + clip(lastGate, 1500) + "\nFix exactly these failures. " + std::string{kEditSyntax});
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
    if (!haveFiles) { std::fprintf(stderr, "llm-agent: no turn produced the requested files\n"); return 3; }
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
        return 4;
    }
    return 0;
}

}  // namespace sub0squad
