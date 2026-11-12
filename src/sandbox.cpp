#include "sub0squad/sandbox.hpp"

#include "json_util.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/plan.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::map<std::string, std::vector<std::string>> parseArgs(const std::vector<std::string>& args)
{
    std::map<std::string, std::vector<std::string>> m;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i].rfind("--", 0) == 0) { m[args[i].substr(2)].push_back(args[i + 1]); ++i; }
    return m;
}
std::string one(const std::map<std::string, std::vector<std::string>>& m, const std::string& k, const std::string& dflt = {})
{
    auto it = m.find(k);
    return it == m.end() || it->second.empty() ? dflt : it->second.back();
}

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); if (!o) return false; o << text; if (!o) return false; }
    fs::rename(tmp, p, ec);
    return !ec;
}

std::optional<Plan> loadPlan(const fs::path& dir, std::string& error)
{
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { error = "cannot load plan.json: " + plan.error(); return std::nullopt; }
    return std::move(*plan);
}

}  // namespace

// ---- planner --------------------------------------------------------------------------------------------------------
int runMockPlanner(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const std::string task = one(a, "task"), out = one(a, "out");
    if (task.empty() || out.empty()) { std::fprintf(stderr, "mock-planner: --task and --out are required\n"); return 64; }
    events::progress(0, 1, "planning");
    if (const Harness* bh = harnessWithBlueprint(task)) {
        // The hand-written blueprint stands in for a frontier architect: validated like any other plan.
        const std::string text = bh->blueprint(task);
        auto parsed = Plan::fromJson(text);
        if (!parsed) { std::fprintf(stderr, "mock-planner: blueprint invalid: %s\n", parsed.error().c_str()); return 1; }
        events::metric("plan.items", static_cast<double>(parsed->items.size()));
        if (!writeFileAtomic(out, parsed->toJson())) { std::fprintf(stderr, "mock-planner: cannot write %s\n", out.c_str()); return 1; }
        events::progress(1, 1, "planned");
        std::printf("planned %zu item(s) for %s from the blueprint\n", parsed->items.size(), task.c_str());
        return 0;
    }
    std::string known;
    for (const auto& name : harnessNames()) known += (known.empty() ? "" : ", ") + name;
    std::fprintf(stderr, "mock-planner: no blueprint for task '%s' (a blueprint belongs to a registered harness: %s)\n", task.c_str(), known.c_str());
    return 3;
}

// ---- agent ----------------------------------------------------------------------------------------------------------
int runMockAgent(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    const std::string itemId = one(a, "item"), tier = one(a, "tier", "local");
    const int attempt = std::atoi(one(a, "attempt", "1").c_str());
    const int delayMs = std::atoi(one(a, "delay-ms", "120").c_str());
    std::string error;
    auto plan = loadPlan(dir, error);
    if (!plan) { std::fprintf(stderr, "mock-agent: %s\n", error.c_str()); return 1; }
    const WorkItem* item = plan->find(itemId);
    if (!item) { std::fprintf(stderr, "mock-agent: no item '%s' in the plan\n", itemId.c_str()); return 1; }

    // Failure injection: "item" fails every local attempt, "item:N" the first N attempts; the frontier-stub tier is never broken.
    bool buggy = false;
    if (tier != "frontier-stub")
        if (auto inj = a.find("inject"); inj != a.end())
            for (const auto& spec : inj->second) {
                const auto colon = spec.find(':');
                const std::string name = spec.substr(0, colon);
                const int limit = colon == std::string::npos ? 1 << 30 : std::atoi(spec.c_str() + colon + 1);
                if (name == itemId && attempt <= limit) buggy = true;
            }
    const Harness* harness = findHarness(plan->harness);
    if (!harness || !harness->mockFile) { std::fprintf(stderr, "mock-agent: the plan's harness '%s' has no mock deliverables\n", plan->harness.c_str()); return 1; }
    const std::string bugFile = harness->mockBugTarget ? harness->mockBugTarget(itemId) : std::string{};
    const fs::path out = dir / "items" / itemId / ("attempt-" + std::to_string(attempt)) / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);

    double tokensIn = 200.0 + static_cast<double>(item->goal.size()) / 4.0, tokensOut = 0;
    events::metric("tokens.in", tokensIn);
    long done = 0;
    const long total = static_cast<long>(item->files.size());
    events::progress(0, total, "writing");
    for (const auto& f : item->files) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        const bool useBug = buggy && f == bugFile;
        std::string text = harness->mockFile(f, useBug);
        if (text.empty()) { std::fprintf(stderr, "mock-agent: no template for '%s' (or its defect no longer matches)\n", f.c_str()); return 1; }
        if (!writeFileAtomic(out / f, text)) { std::fprintf(stderr, "mock-agent: cannot write %s\n", f.c_str()); return 1; }
        tokensOut += static_cast<double>(text.size()) / 4.0;
        events::metric("tokens.out", tokensOut);
        events::progress(++done, total, "writing");
        std::printf("wrote %s (%zu bytes)%s\n", f.c_str(), text.size(), useBug ? " [injected defect]" : "");
    }
    json::Writer w;
    w.beginObject().kv("item", itemId).kv("tier", tier).kv("attempt", attempt).kvBool("injectedDefect", buggy).endObject();
    writeFileAtomic(out.parent_path() / "agent.json", w.take());
    {   // a mock still reports the metrics the harness KPIs use: all of it is deliverable work, the card is the only input
        double cardTokens = 0;
        cardTokens += static_cast<double>(item->card.estimatedTokens());
        events::metric("tokens.system", 0);
        events::metric("tokens.card", cardTokens);
        events::metric("tokens.feedback", 0);
        events::metric("tokens.work", tokensOut);
        events::metric("tokens.waste", 0);
        if (harness->scaffoldBytes) events::metric("harness.bytes", static_cast<double>(harness->scaffoldBytes()));
    }
    return 0;
}

}  // namespace sub0squad
