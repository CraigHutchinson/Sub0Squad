#include "sub0squad/replay.hpp"

#include "sub0squad/plan.hpp"
#include "sub0platform/process.hpp"
#include "sub0platform/file.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <print>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace sub0squad {

namespace {

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
        if (!first && (part.starts_with("seed=") || (think && part.starts_with("thinking=")))) continue;
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
    sub0platform::Child child;
    std::chrono::steady_clock::time_point began;
    double seconds = 0;
    int exitCode = -1;
};

}  // namespace

ReplayOptions::AcceptedOverride parseAcceptedOverride(const std::string& value)
{
    const auto colon = value.find(':'), eq = value.find('=');
    if (colon == std::string::npos || eq == std::string::npos || colon == 0 || eq < colon + 2 || eq + 1 >= value.size()) return {};
    return {value.substr(0, colon), value.substr(colon + 1, eq - colon - 1), value.substr(eq + 1)};
}

int runReplay(const ReplayOptions& o)
{
    auto plan = Plan::fromJson(sub0platform::readFileText(o.sandbox / "plan.json"));
    if (!plan) { std::println(stderr, "replay: cannot read the workflow's plan: {}", plan.error()); return 2; }
    const WorkItem* node = plan->find(o.node);
    if (!node) { std::println(stderr, "replay: the plan has no node '{}'", o.node); return 2; }
    if (o.engineProfile.empty()) { std::println(stderr, "replay: no engine profile (give --engine NAME=URL,model=..)"); return 2; }
    for (const auto& ov : o.accepted) {
        std::error_code e;
        if (ov.item.empty() || !fs::is_regular_file(ov.from, e)) { std::println(stderr, "replay: --accepted needs ITEM:FILE=PATH with an existing PATH ('{}')", ov.from.string()); return 2; }
        if (!closureOf(*plan, o.node).count(ov.item)) { std::println(stderr, "replay: --accepted names '{}', which '{}' does not depend on", ov.item, o.node); return 2; }
    }
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
        for (const auto& ov : o.accepted) {   // the case under test: a dependency's accepted file replaced (validated above)
            const fs::path to = v.dir / "items" / ov.item / "accepted" / ov.file;
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(ov.from, to, fs::copy_options::overwrite_existing, ec);
        }
        if (fs::exists(o.sandbox / "items" / o.node / "card-override.json", ec)) fs::copy_file(o.sandbox / "items" / o.node / "card-override.json", v.dir / "items" / o.node / "card-override.json", fs::copy_options::overwrite_existing, ec);
        for (const auto& e : fs::directory_iterator(o.sandbox / "items" / o.node, ec)) {   // earlier failures: a division is asked with them in view
            const std::string n = e.path().filename().string();
            if (n.starts_with("attempt-") && fs::exists(e.path() / "gate.txt", ec)) {
                fs::create_directories(v.dir / "items" / o.node / n, ec);
                fs::copy_file(e.path() / "gate.txt", v.dir / "items" / o.node / n / "gate.txt", fs::copy_options::overwrite_existing, ec);
            }
        }
        std::vector<std::string> argv{o.exe.string()};
        const std::string profile = withSeed(o.engineProfile, seed, o.think);
        if (o.fork) argv.insert(argv.end(), {"llm-fork", "--workflow-dir", v.dir.string(), "--item", o.node, "--attempt", "1", "--engine", profile, "--max-tokens", std::to_string(o.maxTokens), "--timeout", "3600", "--repair-turns", std::to_string(o.repairTurns)});
        if (o.fork && o.parallel > 0) argv.insert(argv.end(), {"--parallel", std::to_string(o.parallel)});
        if (o.fork && !o.sequentialReason.empty()) argv.insert(argv.end(), {"--sequential-reason", o.sequentialReason});
        if (o.fork) argv.insert(argv.end(), {"--fresh", "1"});   // a replay compares like with like: every variant starts from its own skeleton
        if (o.fork && o.stance != "baseline") argv.insert(argv.end(), {"--stance", o.stance});
        else if (o.decompose) argv.insert(argv.end(), {"llm-architect", "--mode", "decompose", "--workflow-dir", v.dir.string(), "--item", o.node, "--attempt", "9", "--engine", profile, "--timeout", "3600"});
        else argv.insert(argv.end(), {"llm-agent", "--workflow-dir", v.dir.string(), "--item", o.node, "--tier", "local", "--attempt", "1", "--engine", profile, "--max-tokens", std::to_string(o.think ? std::max(o.maxTokens, 6000) : o.maxTokens),
                                      "--timeout", "3600", "--treatment", o.treatment, "--stance", o.stance, "--repair-turns", std::to_string(o.repairTurns), "--size-budget", "0"});
        v.began = std::chrono::steady_clock::now();
        auto child = sub0platform::spawn({.argv = std::move(argv), .outputFile = v.dir / "replay.log", .nullInput = false});
        if (!child) { std::println(stderr, "replay: cannot start a variant: {}", child.error().message); return 2; }
        v.child = std::move(*child);
        vs.push_back(std::move(v));
    }
    std::println("replaying '{}' ({}) with {} variant(s) in {}", o.node, o.decompose ? "the division" : "the worker", vs.size(), base.string());
    std::fflush(stdout);
    for (auto& v : vs) {
        const auto status = v.child.wait().value_or(sub0platform::ExitStatus{});
        v.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - v.began).count();
        v.exitCode = status.shellCode();
    }
    int passed = 0;
    std::println("{:<6} {:<8} {:>5} {:>12} {:>7} {:>8}  {}", "seed", "result", "turns", "completion", "wall s", "concerns", "first failure / note");
    for (const auto& v : vs) {
        const fs::path attempt = v.dir / "items" / o.node / (o.decompose ? "attempt-9" : "attempt-1");
        const std::string log = sub0platform::readFileText(v.dir / "replay.log");
        int turns = 0;
        for (const auto& e : fs::directory_iterator(attempt, ec)) if (e.path().filename().string().starts_with("reply-")) ++turns;
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
            const std::string gate = sub0platform::readFileText(attempt / "gate.txt");
            const bool ok = gate.starts_with("GATE PASS");
            result = ok ? "PASS" : (v.exitCode == 0 || v.exitCode == 3 || v.exitCode == 5 || v.exitCode == 7 ? "FAIL" : "ERROR");
            note = ok ? "" : gate.substr(0, gate.find('\n')).substr(0, 140);
            if (note.empty()) note = "exit " + std::to_string(v.exitCode);
        }
        if (result == "PASS") ++passed;
        int concerns = 0;   // CONCERN blocks the worker raised (stance.hpp): recorded one heading each in concerns.md
        const std::string raised = sub0platform::readFileText(attempt / "concerns.md");
        for (std::size_t at = raised.find("## CONCERN:"); at != std::string::npos; at = raised.find("## CONCERN:", at + 1)) ++concerns;
        std::println("{:<6} {:<8} {:5} {:12} {:7.0f} {:8}  {}", v.seed, result, turns, completion, v.seconds, concerns, note);
        if (concerns > 0) std::println("       concerns: {}", (attempt / "concerns.md").string());
        std::println("       trace: {}", (attempt / "conversation.md").string());
    }
    std::println("{} of {} variant(s) passed", passed, vs.size());
    return passed > 0 ? 0 : 1;
}

}  // namespace sub0squad
