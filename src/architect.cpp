// Architect jobs: the dedicated roles of the escalation ladder (docs/OPTIMIZATION.md). `decompose` asks the model to divide a failing work
// item into smaller work packages (a plan-2 fragment); `rearchitect` rewrites the item's work card, shorter and focused on the failures.
// Each is an ordinary job and counts as an attempt. The mock versions are deterministic stand-ins for tests.
#include "json_util.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/sandbox.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// The most recent failure text for an item: the gate output of its highest attempt that has one.
std::string latestFailure(const fs::path& dir, const std::string& item, int beforeAttempt)
{
    for (int n = beforeAttempt - 1; n >= 1; --n) {
        const std::string g = slurp(dir / "items" / item / ("attempt-" + std::to_string(n)) / "gate.txt");
        if (!g.empty() && g.rfind("GATE PASS", 0) != 0) return g.size() > 1500 ? g.substr(0, 1500) : g;
    }
    return {};
}

std::string cardText(const WorkItem& it)
{
    std::ostringstream o;
    o << "Item '" << it.id << "' (" << it.persona << "): " << it.goal << "\nFiles: ";
    for (std::size_t i = 0; i < it.files.size(); ++i) o << (i ? ", " : "") << it.files[i];
    o << "\nContext: " << it.card.context << "\nContract:\n" << it.card.contract << "\nAcceptance:\n";
    for (const auto& a : it.card.acceptance) o << "- " << a << "\n";
    return o.str();
}

// First balanced-looking JSON object in a reply (the model may add prose or fences around it).
std::string extractJson(const std::string& reply)
{
    const auto a = reply.find('{');
    const auto b = reply.rfind('}');
    return a == std::string::npos || b == std::string::npos || b < a ? std::string{} : reply.substr(a, b - a + 1);
}

bool parseCard(const std::string& text, WorkCard& card)
{
    try {
        bool any = false;
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "context") { card.context = json::toString(v); any = true; }
                else if (k == "contract") { card.contract = json::toString(v); any = true; }
                else if (k == "acceptance") { for (auto x : v.get_array()) card.acceptance.push_back(json::toString(x.value())); }
                else if (k == "notes") { for (auto x : v.get_array()) card.notes.push_back(json::toString(x.value())); }
            }
        });
        return any && !card.contract.empty();
    } catch (const simdjson::simdjson_error&) { return false; }
}

std::string cardJson(const WorkCard& c)
{
    json::Writer w;
    w.beginObject().kv("context", c.context).kv("contract", c.contract).key("acceptance").beginArray();
    for (const auto& a : c.acceptance) w.str(a);
    w.endArray().key("notes").beginArray();
    for (const auto& a : c.notes) w.str(a);
    w.endArray().endObject();
    return w.take();
}

}  // namespace

bool readCardOverride(const fs::path& file, WorkCard& card)
{
    const std::string text = slurp(file);
    if (text.empty()) return false;
    WorkCard parsed;
    if (!parseCard(text, parsed)) return false;
    card.context = parsed.context; card.contract = parsed.contract; card.acceptance = parsed.acceptance; card.notes = parsed.notes;
    return true;
}

int runMockArchitect(const std::vector<std::string>& args)
{
    const std::string mode = arg(args, "mode"), itemId = arg(args, "item");
    const fs::path dir = arg(args, "workflow-dir");
    const int attempt = std::atoi(arg(args, "attempt", "1").c_str());
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan || !plan->find(itemId)) { std::fprintf(stderr, "mock-architect: cannot load the plan or item\n"); return 1; }
    const WorkItem& it = *plan->find(itemId);
    events::progress(0, 1, mode);
    if (mode == "decompose") {  // deterministic stand-in: it cannot divide (a real division needs tests per part), so the ladder moves on
        std::printf("mock architect declines to divide '%s'\n", itemId.c_str());
        return 3;
    }
    if (mode == "rearchitect") {
        WorkCard c = it.card;
        const std::string failure = latestFailure(dir, itemId, attempt);
        c.notes.push_back("Rewritten after failed attempts" + std::string{failure.empty() ? "" : ": " + failure.substr(0, 300)});
        if (!writeFile(dir / "items" / itemId / "card-override.json", cardJson(c))) return 1;
        events::progress(1, 1, "rewritten");
        std::printf("rewrote the card of '%s'\n", itemId.c_str());
        return 0;
    }
    std::fprintf(stderr, "mock-architect: unknown mode '%s'\n", mode.c_str());
    return 64;
}

int runLlmArchitect(const std::vector<std::string>& args)
{
    const std::string mode = arg(args, "mode"), itemId = arg(args, "item");
    const fs::path dir = arg(args, "workflow-dir");
    const int attempt = std::atoi(arg(args, "attempt", "1").c_str());
    const int timeoutSec = std::atoi(arg(args, "timeout", "1700").c_str());
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-architect: %s\n", engine.error().c_str()); return 64; }
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan || !plan->find(itemId)) { std::fprintf(stderr, "llm-architect: cannot load the plan or item\n"); return 1; }
    const WorkItem& it = *plan->find(itemId);
    const std::string failure = latestFailure(dir, itemId, attempt);
    std::string system, user;
    if (mode == "decompose") {
        system = "You are the architect of a small software squad. Workers are small models: a work item that fails repeatedly is too big. "
                 "Your job is the separation: clean boundaries, exact interfaces and how the parts interact (the whole is checked afterwards by the original tests). "
                 "Divide the failing item into 2 to 4 smaller work packages, test-first, and reply with ONE JSON object and nothing else.\n"
                 "Syntax: {\"schema\":\"plan-2\",\"task\":\"x\",\"harness\":\"web-static\",\"project\":\"x\",\"reasons\":[\"why this division\"],\"items\":[{\"id\":\"short\",\"title\":\"..\",\"persona\":\"tester|developer\",\"goal\":\"one sentence\","
                 "\"files\":[\"paths\"],\"deps\":[\"ids of items in THIS reply only\"],\"complexity\":1,\"card\":{\"context\":\"1-2 sentences\",\"contract\":\"exact interface\",\"acceptance\":[\"checks\"],\"include\":[],\"notes\":[]},"
                 "\"gate\":{\"kind\":\"node-test\",\"tests\":[\"tests/x.test.mjs\"],\"minTests\":3,\"mustFailOnStub\":false}}]}\n"
                 "Rules: testers write only under tests/ (their gate has mustFailOnStub true); developers never write tests; every file the original item delivered must be delivered by some new item; "
                 "items must not share files; a developer item depends on the tester item whose test file it includes (list it in card.include); keep each card under 300 words. "
                 "Every developer part gets its OWN tester item with a NEW test file (never the original item's test file, which needs all the parts together: it is run on the whole afterwards). "
                 "Because the parts are built independently, each card must state the exact exports and signatures of the other parts it uses.";
        user = cardText(it) + (failure.empty() ? "" : "\nFailures so far:\n" + failure + "\n") + "\nDivide this item.";
    } else if (mode == "rearchitect") {
        system = "You rewrite work cards for a small model that keeps failing a task. Make the card shorter, concrete and focused on what failed: exact interface, worked input/output examples, the specific pitfalls. "
                 "Reply with ONE JSON object and nothing else: {\"context\":\"1-2 sentences\",\"contract\":\"exact interface and examples\",\"acceptance\":[\"checks\"],\"notes\":[\"pitfalls\"]}";
        user = cardText(it) + (failure.empty() ? "" : "\nFailures so far:\n" + failure + "\n") + "\nRewrite the card.";
    } else {
        std::fprintf(stderr, "llm-architect: unknown mode '%s'\n", mode.c_str());
        return 64;
    }
    if (const char* slot = std::getenv("SUB0SQUAD_LEASE_INDEX"); slot && *slot) events::metric("engine.slot", static_cast<double>(std::atoi(slot)));  // so the portal can show this on the node that raised it
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    writeFile(attemptDir / "prompt.txt", "SYSTEM:\n" + system + "\nUSER:\n" + user);
    // Iterate: a division with one defect (a part without test files, a part graded by the parent's tests) is sent back with the reason, up to
    // two more times, instead of being thrown away: the same repair loop the workers have, applied to the architect.
    EngineProfile ep = *engine;
    ep.temperature = 0.2;
    std::vector<ChatMessage> convo = {{"system", system}, {"user", user}};
    constexpr int kTurns = 3;
    double tokensIn = 0, tokensOut = 0;
    std::string lastError = "no reply";
    for (int turn = 0; turn < kTurns; ++turn) {
        events::progress(turn, kTurns + 1, turn == 0 ? "waiting for the engine" : "repairing");
        auto reply = chatCompleteMessages(ep, convo, 2500, timeoutSec);
        if (!reply) { std::fprintf(stderr, "llm-architect: engine call failed: %s\n", reply.error().c_str()); return 2; }
        writeFile(attemptDir / ("reply-" + std::to_string(turn) + ".txt"), reply->content);
        writeFile(attemptDir / "reply.txt", reply->content);
        tokensIn += reply->promptTokens; tokensOut += reply->completionTokens;
        events::metric("tokens.in", tokensIn);
        events::metric("tokens.out", tokensOut);
        if (reply->predictedMs > 0) events::metric("engine.decode_tps", static_cast<double>(reply->completionTokens) / (reply->predictedMs / 1000.0));
        const std::string json = extractJson(reply->content);
        std::string error;
        if (json.empty()) error = "the reply contained no JSON object";
        else if (mode == "decompose") {
            auto fragment = Plan::fromJson(json);
            if (!fragment) error = "the proposed division is invalid: " + fragment.error();
            else if (auto merged = Plan::amended(*plan, itemId, *fragment); !merged) error = merged.error();  // would it merge now, so the daemon's later merge is no surprise
            else {
                if (!writeFile(attemptDir / "fragment.json", fragment->toJson())) return 1;
                std::printf("proposed a division of '%s' into %zu items%s\n", itemId.c_str(), fragment->items.size(), turn ? (" (after " + std::to_string(turn) + " correction" + (turn > 1 ? "s" : "") + ")").c_str() : "");
                return 0;
            }
        } else {
            WorkCard card;
            if (!parseCard(json, card)) error = "the rewritten card is not valid";
            else if (card.estimatedTokens() > 1500) error = "the rewritten card is over the token budget";
            else {
                if (!writeFile(dir / "items" / itemId / "card-override.json", cardJson(card))) return 1;
                events::progress(kTurns, kTurns, "rewritten");
                std::printf("rewrote the card of '%s' (about %d tokens)\n", itemId.c_str(), card.estimatedTokens());
                return 0;
            }
        }
        lastError = error;
        std::printf("turn %d rejected: %s\n", turn, error.c_str());
        convo.push_back({"assistant", reply->content});
        convo.push_back({"user", "Your reply was rejected: " + error + ". Reply again with the corrected JSON object only, changing just what the error names."});
    }
    std::printf("DECLINED: %s\n", lastError.c_str());
    return 3;
}

}  // namespace sub0squad
