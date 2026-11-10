#include "sub0squad/replay.hpp"

#include "sub0squad/plan.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

extern char** environ;

namespace fs = std::filesystem;

namespace sub0squad {

namespace {

std::string slurp(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

std::set<std::string> closureOf(const Plan& plan, const std::string& id)
{
    std::set<std::string> seen;
    std::vector<std::string> stack{id};
    while (!stack.empty()) {
        const std::string cur = stack.back();
        stack.pop_back();
        const WorkItem* it = plan.find(cur);
        if (!it) continue;
        for (const auto& d : it->deps) if (seen.insert(d).second) stack.push_back(d);
    }
    return seen;
}

// The profile with its seed replaced (and thinking on when asked).
std::string withSeed(const std::string& profile, long seed, bool think)
{
    std::string out;
    std::stringstream ss(profile);
    bool first = true;
    for (std::string part; std::getline(ss, part, ',');) {
        if (!first && (part.rfind("seed=", 0) == 0 || (think && part.rfind("thinking=", 0) == 0))) continue;
        out += (first ? "" : ",") + part;
        first = false;
    }
    out += ",seed=" + std::to_string(seed);
    if (think) out += ",thinking=1";
    return out;
}

struct Variant
{
    long seed = 0;
    fs::path dir;
    pid_t pid = -1;
    std::chrono::steady_clock::time_point began;
    double seconds = 0;
    int exitCode = -1;
};

}  // namespace

int runReplay(const ReplayOptions& o)
{
    auto plan = Plan::fromJson(slurp(o.sandbox / "plan.json"));
    if (!plan) { std::fprintf(stderr, "replay: cannot read the workflow's plan: %s\n", plan.error().c_str()); return 2; }
    const WorkItem* node = plan->find(o.node);
    if (!node) { std::fprintf(stderr, "replay: the plan has no node '%s'\n", o.node.c_str()); return 2; }
    if (o.engineProfile.empty()) { std::fprintf(stderr, "replay: no engine profile (give --engine NAME=URL,model=..)\n"); return 2; }
    const auto stamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const fs::path base = o.sandbox / "replays" / (o.node + "-" + stamp);
    std::error_code ec;
    std::vector<Variant> vs;
    for (const long seed : o.seeds) {
        Variant v;
        v.seed = seed;
        v.dir = base / ("s" + std::to_string(seed));
        fs::create_directories(v.dir / "items" / o.node, ec);
        fs::copy_file(o.sandbox / "plan.json", v.dir / "plan.json", fs::copy_options::overwrite_existing, ec);
        for (const auto& dep : closureOf(*plan, o.node)) {   // what the node is given: the accepted outputs of everything it depends on
            const fs::path acc = o.sandbox / "items" / dep / "accepted";
            if (fs::exists(acc, ec)) fs::copy(acc, v.dir / "items" / dep / "accepted", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        if (fs::exists(o.sandbox / "items" / o.node / "card-override.json", ec)) fs::copy_file(o.sandbox / "items" / o.node / "card-override.json", v.dir / "items" / o.node / "card-override.json", fs::copy_options::overwrite_existing, ec);
        for (const auto& e : fs::directory_iterator(o.sandbox / "items" / o.node, ec)) {   // earlier failures: a division is asked with them in view
            const std::string n = e.path().filename().string();
            if (n.rfind("attempt-", 0) == 0 && fs::exists(e.path() / "gate.txt", ec)) {
                fs::create_directories(v.dir / "items" / o.node / n, ec);
                fs::copy_file(e.path() / "gate.txt", v.dir / "items" / o.node / n / "gate.txt", fs::copy_options::overwrite_existing, ec);
            }
        }
        std::vector<std::string> argv{o.exe.string()};
        const std::string profile = withSeed(o.engineProfile, seed, o.think);
        if (o.fork) argv.insert(argv.end(), {"llm-fork", "--workflow-dir", v.dir.string(), "--item", o.node, "--attempt", "1", "--engine", profile, "--max-tokens", std::to_string(o.maxTokens), "--timeout", "3600", "--repair-turns", std::to_string(o.repairTurns)});
        else if (o.decompose) argv.insert(argv.end(), {"llm-architect", "--mode", "decompose", "--workflow-dir", v.dir.string(), "--item", o.node, "--attempt", "9", "--engine", profile, "--timeout", "3600"});
        else argv.insert(argv.end(), {"llm-agent", "--workflow-dir", v.dir.string(), "--item", o.node, "--tier", "local", "--attempt", "1", "--engine", profile, "--max-tokens", std::to_string(o.think ? std::max(o.maxTokens, 6000) : o.maxTokens),
                                      "--timeout", "3600", "--treatment", o.treatment, "--repair-turns", std::to_string(o.repairTurns), "--size-budget", "0"});
        std::vector<char*> cargv;
        for (auto& s : argv) cargv.push_back(s.data());
        cargv.push_back(nullptr);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        const std::string log = (v.dir / "replay.log").string();
        posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        v.began = std::chrono::steady_clock::now();
        if (posix_spawn(&v.pid, argv[0].c_str(), &fa, nullptr, cargv.data(), environ) != 0) { std::fprintf(stderr, "replay: cannot start a variant\n"); return 2; }
        posix_spawn_file_actions_destroy(&fa);
        vs.push_back(std::move(v));
    }
    std::printf("replaying '%s' (%s) with %zu variant(s) in %s\n", o.node.c_str(), o.decompose ? "the division" : "the worker", vs.size(), base.string().c_str());
    std::fflush(stdout);
    for (auto& v : vs) {
        int status = 0;
        waitpid(v.pid, &status, 0);
        v.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - v.began).count();
        v.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    int passed = 0;
    std::printf("%-6s %-8s %5s %12s %7s  %s\n", "seed", "result", "turns", "completion", "wall s", "first failure / note");
    for (const auto& v : vs) {
        const fs::path attempt = v.dir / "items" / o.node / (o.decompose ? "attempt-9" : "attempt-1");
        const std::string log = slurp(v.dir / "replay.log");
        int turns = 0;
        for (const auto& e : fs::directory_iterator(attempt, ec)) if (e.path().filename().string().rfind("reply-", 0) == 0) ++turns;
        long completion = 0;
        for (std::size_t at = 0; (at = log.find(" completion tokens", at)) != std::string::npos; at += 18) {
            std::size_t b = at;
            while (b > 0 && std::isdigit(static_cast<unsigned char>(log[b - 1]))) --b;
            completion += std::atol(log.c_str() + b);
        }
        std::string result, note;
        if (o.decompose) {
            const bool ok = fs::exists(attempt / "fragment.json", ec);
            result = ok ? "PASS" : "FAIL";
            const auto d = log.find("DECLINED:");
            note = ok ? "a valid division was proposed" : d != std::string::npos ? log.substr(d, log.find('\n', d) - d).substr(0, 140) : "no division";
        } else {
            const std::string gate = slurp(attempt / "gate.txt");
            const bool ok = gate.rfind("GATE PASS", 0) == 0;
            result = ok ? "PASS" : (v.exitCode == 0 || v.exitCode == 3 || v.exitCode == 5 ? "FAIL" : "ERROR");
            note = ok ? "" : gate.substr(0, gate.find('\n')).substr(0, 140);
            if (note.empty()) note = "exit " + std::to_string(v.exitCode);
        }
        if (result == "PASS") ++passed;
        std::printf("%-6ld %-8s %5d %12ld %7.0f  %s\n", v.seed, result.c_str(), turns, completion, v.seconds, note.c_str());
        std::printf("       trace: %s\n", (attempt / "conversation.md").string().c_str());
    }
    std::printf("%d of %zu variant(s) passed\n", passed, vs.size());
    return passed > 0 ? 0 : 1;
}

}  // namespace sub0squad
