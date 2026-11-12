#include "agent_impl.hpp"

#include "sub0squad/edits.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/policy.hpp"
#include "sub0squad/trace.hpp"
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

namespace {

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

}  // namespace sub0squad
