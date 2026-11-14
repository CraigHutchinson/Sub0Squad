#include "sub0squad/policy.hpp"

#include "json_util.hpp"

namespace sub0squad {

const char* toString(Action a)
{
    switch (a) { case Action::retry: return "retry"; case Action::escalate: return "escalate"; case Action::fail: return "fail"; case Action::sections: return "sections"; case Action::decompose: return "decompose"; case Action::reviseTests: return "reviseTests"; case Action::rearchitect: return "rearchitect"; }
    return "?";
}

std::string toJson(const DecisionContext& c, const Decision& d)
{
    json::Writer w;
    w.beginObject().kv("action", toString(d.action)).kv("tier", d.tier).kv("confidence", d.confidence);
    w.key("reasons").beginArray();
    for (const auto& r : d.reasons) w.str(r);
    w.endArray().key("inputs").beginObject().kv("workflow", c.workflow).kv("item", c.item).kv("attempt", c.attempt)
        .kv("localAttempts", c.localAttempts).kv("frontierAttempts", c.frontierAttempts).kv("failedRole", c.failedRole)
        .kv("failure", c.failure).kv("complexity", c.complexity).kv("frontier", c.frontier == FrontierMode::stub ? "stub" : "off")
        .kv("maxLocalAttempts", c.maxLocalAttempts).kv("maxFrontierAttempts", c.maxFrontierAttempts).kv("depth", c.depth).kv("totalAttempts", c.totalAttempts)
        .kv("persona", c.persona).kvBool("divideBySections", c.divideBySections).kvBool("sectionsTried", c.sectionsTried).kvBool("splittable", c.splittable).kvBool("hasTesterDep", c.hasTesterDep).kvBool("decomposeTried", c.decomposeTried).kvBool("reviseTried", c.reviseTried)
        .kvBool("rearchitectTried", c.rearchitectTried);
    // The rungs this workflow's process does not have: written only when there are any, so a decision says why the ladder skipped a rung.
    const std::pair<const char*, bool> rungs[] = {{"decompose", c.allowDecompose}, {"sections", c.allowSections}, {"reviseTests", c.allowReviseTests}, {"rearchitect", c.allowRearchitect}, {"escalate", c.allowEscalate}};
    bool lacks = false;
    for (const auto& [name, allowed] : rungs) lacks = lacks || !allowed;
    if (c.forked) w.kvBool("forked", true);   // written only when it holds, like `processLacks`
    if (lacks) { w.key("processLacks").beginArray(); for (const auto& [name, allowed] : rungs) if (!allowed) w.str(name); w.endArray(); }
    w.endObject().endObject();
    return w.take();
}

int chooseLeaseSlot(int cap, const std::set<int>& used, int preferred)
{
    if (preferred >= 0 && preferred < cap && !used.count(preferred)) return preferred;
    int i = 0;
    while (used.count(i) && i < cap) ++i;
    return i;
}

bool isStructuralJob(std::string_view workflow, std::string_view role) { return !workflow.empty() && !role.empty() && role != "agent"; }

bool admitsJob(bool structuralUnleased, int activeNormal, int activeReserved, int maxJobs)
{
    const int overflow = std::max(0, activeReserved - kStructuralReserve);   // structural jobs beyond their reserve take ordinary slots
    if (structuralUnleased && activeReserved < kStructuralReserve) return true;
    return activeNormal + overflow < maxJobs;
}

bool shouldDivide(long estimate, long softBudget, long ceiling, int idleSlots, int totalSlots, long divisionCost)
{
    if (softBudget <= 0) return false;
    if (estimate > ceiling) return true;
    if (estimate <= softBudget) return false;
    const int parallelism = (totalSlots <= 1 || idleSlots < 0) ? 1 : std::clamp(idleSlots + 1, 1, 4);
    const double saved = static_cast<double>(estimate) * (1.0 - 1.0 / parallelism);
    return saved > static_cast<double>(divisionCost);
}

Decision RulePolicy::decide(const DecisionContext& c) const
{
    Decision d;
    const std::string what = "item '" + c.item + "' attempt " + std::to_string(c.attempt) + " failed in its " + c.failedRole;
    d.reasons.push_back(what);
    if (c.totalAttempts >= c.maxTotalAttempts) {
        d.action = Action::fail;
        d.reasons.push_back("attempt budget exhausted (" + std::to_string(c.maxTotalAttempts) + " attempts, divisions and rewrites included)");
        return d;
    }
    if (c.repeatedFailure && c.hasTesterDep && !c.reviseTried && c.allowReviseTests) {
        d.action = Action::reviseTests;
        d.tier = "local";
        d.reasons.push_back("the same tests failed with the same messages twice: repairing does not change the outcome, so the tests are suspect; send them back to the tester");
        return d;
    }
    if (c.interfaceGap && !c.rearchitectTried && c.allowRearchitect) {
        // Widen before retrying: a missing export or symbol is a gap between two modules' contracts, which a second attempt at one file cannot close.
        d.action = Action::rearchitect;
        d.tier = "local";
        d.reasons.push_back("interface mismatch (a name the tests need is not provided): the contract is incomplete or the tests overreach, so the next scope up rewrites the card to name every export before another attempt at the file");
        return d;
    }
    const bool developer = c.persona == "developer";   // a tester's item is never divided (row 44)
    if (c.oversize && developer && c.divideBySections && !c.sectionsTried && !c.forked && c.allowSections) {
        d.action = Action::sections;
        d.tier = "local";
        d.reasons.push_back("plan phase: the item is estimated too big for one reply; this platform divides by sections of a file, so the next attempt writes it as forked sections (a skeleton, then one function per session)");
        return d;
    }
    if (c.oversize && developer && !c.divideBySections && !c.decomposeTried && c.splittable && c.depth < c.maxDepth && c.allowDecompose) {
        d.action = Action::decompose;
        d.tier = "local";
        d.reasons.push_back("plan phase: the item is estimated too big for one attempt, so it is divided before any code is written");
        return d;
    }
    if (c.localAttempts < c.maxLocalAttempts) {
        d.action = Action::retry;
        d.tier = "local";
        d.reasons.push_back("local attempts " + std::to_string(c.localAttempts) + " of " + std::to_string(c.maxLocalAttempts) + " used: retry locally");
        return d;
    }
    if (c.nearPass && c.localAttempts < c.maxLocalAttempts + c.maxNearPassRepairs) {
        d.action = Action::retry;
        d.tier = "local";
        d.reasons.push_back("near pass (at most 15 % of the tests fail): repair the best code so far instead of dividing the item");
        return d;
    }
    d.reasons.push_back("local attempts exhausted (" + std::to_string(c.localAttempts) + ")");
    if (developer && c.divideBySections && !c.sectionsTried && c.forked && c.allowSections) d.reasons.push_back("the failed attempt already ran as forked sections: the `sections` rung would repeat it, so it is skipped");
    if (developer && c.divideBySections && !c.sectionsTried && !c.forked && c.allowSections) {
        d.action = Action::sections;
        d.tier = "local";
        d.reasons.push_back("first rung: this platform divides by sections of a file (a new module would need a header no item owns), so the next attempt writes the item as forked sections");
        return d;
    }
    if (developer && !c.divideBySections && !c.decomposeTried && c.splittable && c.depth < c.maxDepth && c.allowDecompose) {
        d.action = Action::decompose;
        d.tier = "local";
        d.reasons.push_back("first rung: let the model decide how to divide the item into smaller work packages");
        return d;
    }
    if (!c.reviseTried && c.hasTesterDep && c.allowReviseTests) {
        d.action = Action::reviseTests;
        d.tier = "local";
        d.reasons.push_back("send back to the producer: the tests this item is graded by may be wrong, so the tester revises them from this failure");
        return d;
    }
    if (!c.rearchitectTried && c.allowRearchitect) {
        d.action = Action::rearchitect;
        d.tier = "local";
        d.reasons.push_back("a dedicated job rewrites the work card, shorter and focused on the failures");
        return d;
    }
    if (c.frontier != FrontierMode::off && c.frontierAttempts < c.maxFrontierAttempts && c.frontierHasReference && c.allowEscalate) {
        d.action = Action::escalate;
        d.tier = "frontier-stub";
        d.reasons.push_back("every local rung was tried: escalate once to the frontier tier (stub: nothing leaves the machine)");
        return d;
    }
    d.action = Action::fail;
    d.reasons.push_back(c.frontier == FrontierMode::off ? "frontier mode is off: no escalation allowed" : !c.allowEscalate ? "this process has no escalation rung" : !c.frontierHasReference ? "the frontier stub has no reference for this item, so escalating would only repeat the failure" : "frontier attempts exhausted");
    return d;
}

}  // namespace sub0squad
