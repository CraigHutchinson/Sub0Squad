#include "sub0squad/plan_lint.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace sub0squad {

double estimateItemTokens(const WorkItem& item)
{
    if (item.persona == "integrator") return 100.0;  // runs tests only
    return item.persona == "tester" ? 1000.0 + 250.0 * item.complexity : 400.0 + 500.0 * item.complexity;
}

PlanLint lintPlan(const Plan& plan, double tps)
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
    for (const auto& it : plan.items)
        if (it.persona == "developer" && it.files.size() > 2) r.warnings.push_back("'" + it.id + "' writes " + std::to_string(it.files.size()) + " files: a smaller package per file runs in parallel and fits one attempt");
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
    for (const auto& w : warnings) o += "  warning: " + w + "\n";
    return o;
}

}  // namespace sub0squad
