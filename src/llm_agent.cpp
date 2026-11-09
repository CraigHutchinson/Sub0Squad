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
    std::string s = "You are the " + persona + " on a small software squad. ";
    if (persona == "tester") s += "Write tests for the contract you are given. Do not write the implementation. ";
    else s += "Implement the contract so that the provided tests pass. ";
    s += "Write only the files you are asked to write.\n"
         "Reply with the complete content of every file in exactly this format and nothing else (no explanations, no markdown fences):\n"
         "=== FILE: <path> ===\n<file content>\n=== END FILE ===\n";
    if (!harnessBrief.empty()) s += harnessBrief + "\n";
    return s;
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

    if (tier == "frontier-stub") {  // no frontier provider exists: the stub tier is the reference implementation, recorded as a stub
        std::vector<std::string> a = {"--workflow-dir", dir.string(), "--item", itemId, "--tier", tier, "--attempt", std::to_string(attempt), "--delay-ms", "50"};
        return runMockAgent(a);
    }
    auto engine = EngineProfile::parse(arg(args, "engine"));
    if (!engine) { std::fprintf(stderr, "llm-agent: %s\n", engine.error().c_str()); return 64; }
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::fprintf(stderr, "llm-agent: %s\n", plan.error().c_str()); return 1; }
    const WorkItem* item = plan->find(itemId);
    if (!item) { std::fprintf(stderr, "llm-agent: no item '%s'\n", itemId.c_str()); return 1; }

    // ---- the prompt: a fixed system block, the work card, included files from accepted dependencies, and the previous failure only ----
    const std::string system = systemBlock(item->persona, plan->harness == "web-static" ? webHarnessBrief() : std::string{});
    std::ostringstream card;
    card << "Task: " << item->goal << "\nFiles to write: ";
    for (std::size_t i = 0; i < item->files.size(); ++i) card << (i ? ", " : "") << item->files[i];
    card << "\n\nContext: " << item->card.context << "\n\nContract:\n" << item->card.contract << "\n";
    if (!item->card.acceptance.empty()) { card << "\nAcceptance:\n"; for (const auto& x : item->card.acceptance) card << "- " << x << "\n"; }
    for (const auto& inc : item->card.include) {
        std::string content;
        for (const auto& dep : closure(*plan, itemId)) { const fs::path f = dir / "items" / dep / "accepted" / inc; if (fs::exists(f)) { content = slurp(f); break; } }
        if (!content.empty()) card << "\nProvided file " << inc << ":\n=== FILE: " << inc << " ===\n" << clip(content, 7000) << "\n=== END FILE ===\n";
    }
    if (!item->card.notes.empty()) { card << "\nNotes:\n"; for (const auto& x : item->card.notes) card << "- " << x << "\n"; }
    std::string feedback;
    if (attempt > 1) {
        const std::string g = slurp(dir / "items" / itemId / ("attempt-" + std::to_string(attempt - 1)) / "gate.txt");
        if (!g.empty()) feedback = "Your previous attempt was rejected:\n" + clip(g, 1200) + "\nFix exactly this and write the complete files again.\n";
        else feedback = "Your previous attempt produced no usable files. Reply in the required file format.\n";
    }
    const std::string user = card.str() + (feedback.empty() ? "" : "\n" + feedback);

    bool est1 = false, est2 = false, est3 = false;
    const long tSystem = countTokens(*engine, system, est1), tCard = countTokens(*engine, card.str(), est2), tFeedback = feedback.empty() ? 0 : countTokens(*engine, feedback, est3);
    events::progress(0, 2, "waiting for the engine");
    events::metric("tokens.system", static_cast<double>(tSystem));
    events::metric("tokens.card", static_cast<double>(tCard));
    events::metric("tokens.feedback", static_cast<double>(tFeedback));

    auto reply = chatComplete(*engine, system, user, maxTokens, 0.2, timeoutSec);
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    const fs::path out = attemptDir / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    writeFile(attemptDir / "prompt.txt", "SYSTEM:\n" + system + "\nUSER:\n" + user);
    if (!reply) { std::fprintf(stderr, "llm-agent: engine call failed: %s\n", reply.error().c_str()); return 2; }
    writeFile(attemptDir / "reply.txt", reply->content);
    events::progress(1, 2, "parsing");

    std::set<std::string> allowed(item->files.begin(), item->files.end());
    const Parsed parsed = parseReply(reply->content, allowed);
    const double totalChars = std::max<double>(1.0, static_cast<double>(reply->content.size()));
    const double work = static_cast<double>(reply->completionTokens) * static_cast<double>(parsed.fileChars) / totalChars;
    events::metric("tokens.in", static_cast<double>(reply->promptTokens));
    events::metric("tokens.out", static_cast<double>(reply->completionTokens));
    events::metric("tokens.work", work);
    events::metric("tokens.waste", static_cast<double>(reply->completionTokens) - work);
    if (reply->predictedMs > 0) events::metric("engine.decode_tps", static_cast<double>(reply->completionTokens) / (reply->predictedMs / 1000.0));
    if (reply->promptMs > 0) events::metric("engine.prefill_tps", static_cast<double>(reply->promptTokens) / (reply->promptMs / 1000.0));
    if (plan->harness == "web-static") events::metric("harness.bytes", static_cast<double>(webScaffoldBytes()));
    std::printf("engine %s: %ld prompt tokens (system %ld, card %ld, feedback %ld), %ld completion tokens, finish=%s, %zu file(s) accepted, %zu rejected chars\n",
                engine->name.c_str(), reply->promptTokens, tSystem, tCard, tFeedback, reply->completionTokens, reply->finishReason.c_str(), parsed.files.size(), parsed.rejectedChars);
    if (parsed.files.empty()) { std::fprintf(stderr, "llm-agent: the reply contained none of the requested files\n"); return 3; }
    for (const auto& [path, text] : parsed.files) if (!writeFile(out / path, text)) { std::fprintf(stderr, "llm-agent: cannot write %s\n", path.c_str()); return 1; }
    events::progress(2, 2, "written");
    return 0;
}

}  // namespace sub0squad
