#include "sub0squad/policy.hpp"

#include "json_util.hpp"

namespace sub0squad {

const char* toString(Action a)
{
    switch (a) { case Action::retry: return "retry"; case Action::escalate: return "escalate"; case Action::fail: return "fail"; case Action::decompose: return "decompose"; case Action::reviseTests: return "reviseTests"; case Action::rearchitect: return "rearchitect"; }
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
        .kvBool("splittable", c.splittable).kvBool("hasTesterDep", c.hasTesterDep).kvBool("decomposeTried", c.decomposeTried).kvBool("reviseTried", c.reviseTried)
        .kvBool("rearchitectTried", c.rearchitectTried).endObject().endObject();
    return w.take();
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
    if (!c.decomposeTried && c.splittable && c.depth < c.maxDepth) {
        d.action = Action::decompose;
        d.tier = "local";
        d.reasons.push_back("first rung: let the model decide how to divide the item into smaller work packages");
        return d;
    }
    if (!c.reviseTried && c.hasTesterDep) {
        d.action = Action::reviseTests;
        d.tier = "local";
        d.reasons.push_back("send back to the producer: the tests this item is graded by may be wrong, so the tester revises them from this failure");
        return d;
    }
    if (!c.rearchitectTried) {
        d.action = Action::rearchitect;
        d.tier = "local";
        d.reasons.push_back("a dedicated job rewrites the work card, shorter and focused on the failures");
        return d;
    }
    if (c.frontier != FrontierMode::off && c.frontierAttempts < c.maxFrontierAttempts) {
        d.action = Action::escalate;
        d.tier = "frontier-stub";
        d.reasons.push_back("every local rung was tried: escalate once to the frontier tier (stub: nothing leaves the machine)");
        return d;
    }
    d.action = Action::fail;
    d.reasons.push_back(c.frontier == FrontierMode::off ? "frontier mode is off: no escalation allowed" : "frontier attempts exhausted");
    return d;
}

}  // namespace sub0squad
