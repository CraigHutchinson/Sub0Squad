#include "sub0squad/plan_lint.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace sub0squad {

double estimateItemTokens(const WorkItem& item)
{
    if (item.persona == "integrator") return 100.0;  // runs tests only
    return item.persona == "tester" ? 1000.0 + 250.0 * item.complexity : 400.0 + 500.0 * item.complexity;
}

namespace {

/// A tiny deterministic generator (splitmix64) and a normal draw from it (Box-Muller): the standard library's distributions are not specified to give the
/// same numbers on every platform, and a lint figure that changes with the toolchain could not be a test.
struct Rng
{
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint64_t next() { std::uint64_t z = (state += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double uniform() { return (static_cast<double>(next() >> 11) + 0.5) / 9007199254740992.0; }
    double normal() { return std::sqrt(-2.0 * std::log(uniform())) * std::cos(6.283185307179586 * uniform()); }
};

struct SlotRun { double makespan = 0, work = 0; int freed = 0, backfilled = 0; };

/// One list schedule: `size[i]` is the work of item i, ready items start in plan order on the first free slot.
SlotRun scheduleOnce(const Plan& plan, const std::vector<double>& size, int slots)
{
    const std::size_t n = plan.items.size();
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) index[plan.items[i].id] = i;
    std::vector<int> waitingOn(n, 0);
    std::vector<std::vector<std::size_t>> dependents(n);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& d : plan.items[i].deps) { ++waitingOn[i]; dependents[index[d]].push_back(i); }
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i) if (waitingOn[i] == 0) ready.push_back(i);
    std::vector<std::pair<double, std::size_t>> running;   // finish time, item
    SlotRun r;
    double now = 0;
    std::size_t done = 0;
    for (double w : size) r.work += w;
    while (done < n) {
        std::sort(ready.begin(), ready.end());
        while (static_cast<int>(running.size()) < slots && !ready.empty()) {
            running.push_back({now + size[ready.front()], ready.front()});
            ready.erase(ready.begin());
        }
        if (running.empty()) break;   // cannot happen in a validated (acyclic) plan
        const auto first = std::min_element(running.begin(), running.end());
        now = first->first;
        const std::size_t item = first->second;
        running.erase(first);
        ++done;
        for (std::size_t d : dependents[item]) if (--waitingOn[d] == 0) ready.push_back(d);
        if (done < n) { ++r.freed; if (ready.size() >= static_cast<std::size_t>(slots) - running.size()) ++r.backfilled; }   // ready work for EVERY idle slot
    }
    r.makespan = now;
    return r;
}

}  // namespace

PlanLint lintPlan(const Plan& plan, double tps, int slots, double sigma)
{
    PlanLint r;
    std::map<std::string, int> level;
    std::map<std::string, double> best;          // most tokens on any chain ending at the item (including it)
    std::map<std::string, std::string> via;      // predecessor on that chain
    // Items are validated acyclic, so repeated relaxation terminates.
    for (std::size_t pass = 0; pass <= plan.items.size(); ++pass)
        for (const auto& it : plan.items) {
            int lv = 0;
            double b = 0;
            std::string from;
            for (const auto& d : it.deps) {
                lv = std::max(lv, level[d] + 1);
                if (best[d] > b) { b = best[d]; from = d; }
            }
            level[it.id] = lv;
            best[it.id] = b + estimateItemTokens(it);
            via[it.id] = from;
        }
    int maxLevel = 0;
    for (const auto& [id, lv] : level) maxLevel = std::max(maxLevel, lv);
    r.levels.resize(static_cast<std::size_t>(maxLevel) + 1);
    std::string tail;
    for (const auto& it : plan.items) {
        r.levels[static_cast<std::size_t>(level[it.id])].push_back(it.id);
        r.totalTokens += estimateItemTokens(it);
        if (best[it.id] > r.criticalTokens) { r.criticalTokens = best[it.id]; tail = it.id; }
    }
    for (std::string id = tail; !id.empty(); id = via[id]) r.criticalPath.insert(r.criticalPath.begin(), id);
    for (const auto& l : r.levels) r.maxWidth = std::max<int>(r.maxWidth, static_cast<int>(l.size()));
    r.serialFraction = r.totalTokens > 0 ? r.criticalTokens / r.totalTokens : 1.0;
    r.minutesSerial = r.totalTokens / tps / 60.0;
    r.minutesCritical = r.criticalTokens / tps / 60.0;

    if (r.levels.size() > 1 && r.levels.back().size() == 1) {
        const auto* last = plan.find(r.levels.back().front());
        if (last && estimateItemTokens(*last) >= 0.2 * r.totalTokens)
            r.warnings.push_back("the last stage is one large item ('" + last->id + "', about " + std::to_string(static_cast<int>(100 * estimateItemTokens(*last) / r.totalTokens)) +
                                 " % of the work): nothing runs beside it. Can part of it start earlier, for example by depending on an interface instead of the implementation (inject the dependency), or by splitting it?");
    }
    if (r.maxWidth < 3 && plan.items.size() >= 6)
        r.warnings.push_back("at most " + std::to_string(r.maxWidth) + " items can run at once in a plan of " + std::to_string(plan.items.size()) + ": widen the early stages (small independent work packages first)");
    if (r.serialFraction > 0.6 && plan.items.size() >= 4)
        r.warnings.push_back("the critical path holds " + std::to_string(static_cast<int>(100 * r.serialFraction)) + " % of the estimated work: concurrency can speed this plan up by at most " +
                             std::to_string(1.0 / r.serialFraction).substr(0, 4) + "x");
    if (slots > 0 && !plan.items.empty()) {
        r.slots = slots;
        r.boundSpeedup = r.criticalTokens > 0 ? std::min<double>(slots, r.totalTokens / r.criticalTokens) : slots;
        r.widthRatio = static_cast<double>(r.maxWidth) / slots;
        Rng rng;
        constexpr int kTrials = 400;
        std::vector<double> fills;
        double speedup = 0;
        int freed = 0, backfilled = 0;
        for (int t = 0; t < kTrials; ++t) {
            std::vector<double> size;
            for (const auto& it : plan.items) size.push_back(estimateItemTokens(it) * std::exp(sigma * rng.normal() - 0.5 * sigma * sigma));   // mean-preserving
            const SlotRun run = scheduleOnce(plan, size, slots);
            fills.push_back(run.makespan > 0 ? run.work / (slots * run.makespan) : 1.0);
            speedup += run.makespan > 0 ? run.work / run.makespan : 1.0;
            freed += run.freed; backfilled += run.backfilled;
        }
        std::sort(fills.begin(), fills.end());
        for (double f : fills) r.fill += f;
        r.fill /= kTrials;
        r.fillP10 = fills[fills.size() / 10];
        r.expectedSpeedup = speedup / kTrials;
        r.backfillRate = freed > 0 ? static_cast<double>(backfilled) / freed : 1.0;
        char buf[320];
        if (slots >= 2 && r.fill < 0.7) {
            std::snprintf(buf, sizeof buf, "on %d slots this plan is expected to keep them %.0f %% busy (a bad draw of sizes: %.0f %%; no schedule can beat %.1fx of the %d): only %d item(s) can be ready at once, so a slot that finishes early finds nothing to take. Make more independent items ready at the start (smaller, with no shared files), aiming at about %.0f.",
                          slots, 100 * r.fill, 100 * r.fillP10, r.boundSpeedup, slots, r.maxWidth, 1.5 * slots);
            r.warnings.push_back(buf);
        }
    }
    for (const auto& it : plan.items)
        if (it.persona == "developer" && it.files.size() > 2) r.warnings.push_back("'" + it.id + "' writes " + std::to_string(it.files.size()) + " files: a smaller package per file runs in parallel and fits one attempt");
    for (const auto& it : plan.items) {   // the naming convention: role and area, so a node is recognisable by its id (the part after the last '.' for a divided item)
        const std::string leaf = it.id.substr(it.id.rfind('.') == std::string::npos ? 0 : it.id.rfind('.') + 1);
        const char* want = it.persona == "tester" ? "test-" : it.persona == "developer" ? "dev-" : "int-";
        const bool dividedDeveloper = it.persona == "integrator" && leaf.rfind("dev-", 0) == 0;   // a divided developer item keeps its id as the coordinator
        if (leaf.rfind(want, 0) != 0 && !dividedDeveloper)
            r.warnings.push_back("item id '" + it.id + "' does not start with '" + want + "' (ids say role and area: test-, dev-, int-)");
    }
    return r;
}

std::string PlanLint::toText() const
{
    std::string o;
    char buf[256];
    std::snprintf(buf, sizeof buf, "levels %zu, widest %d, critical path %.0f of %.0f estimated tokens (%.0f %%)\n", levels.size(), maxWidth, criticalTokens, totalTokens, 100 * serialFraction);
    o += buf;
    std::snprintf(buf, sizeof buf, "rough time at one request: %.0f min if run one after another, %.0f min for the critical path alone\n", minutesSerial, minutesCritical);
    o += buf;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        o += "  level " + std::to_string(i) + ":";
        for (const auto& id : levels[i]) o += " " + id;
        o += "\n";
    }
    o += "  critical path:";
    for (std::size_t i = 0; i < criticalPath.size(); ++i) o += (i ? " -> " : " ") + criticalPath[i];
    o += "\n";
    if (slots > 0) {
        std::snprintf(buf, sizeof buf, "slot fill on %d slots with unknown item sizes: %.0f %% busy on average (%.0f %% on a bad draw), speedup %.2fx of at most %.2fx, backfill rate %.0f %%, width %.1fx the slots\n",
                      slots, 100 * fill, 100 * fillP10, expectedSpeedup, boundSpeedup, 100 * backfillRate, widthRatio);
        o += buf;
    }
    for (const auto& w : warnings) o += "  warning: " + w + "\n";
    return o;
}

}  // namespace sub0squad
