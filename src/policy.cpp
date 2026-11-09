#include "sub0squad/policy.hpp"

#include "json_util.hpp"

namespace sub0squad {

const char* toString(Action a)
{
    switch (a) { case Action::retry: return "retry"; case Action::escalate: return "escalate"; case Action::fail: return "fail"; }
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
        .kv("maxLocalAttempts", c.maxLocalAttempts).kv("maxFrontierAttempts", c.maxFrontierAttempts).endObject().endObject();
    return w.take();
}

Decision RulePolicy::decide(const DecisionContext& c) const
{
    Decision d;
    const std::string what = "item '" + c.item + "' attempt " + std::to_string(c.attempt) + " failed in its " + c.failedRole;
    if (c.localAttempts < c.maxLocalAttempts) {
        d.action = Action::retry;
        d.tier = "local";
        d.reasons = {what, "local attempts " + std::to_string(c.localAttempts) + " of " + std::to_string(c.maxLocalAttempts) + " used: retry locally"};
        return d;
    }
    if (c.frontier == FrontierMode::off) {
        d.action = Action::fail;
        d.reasons = {what, "local attempts exhausted", "frontier mode is off: no escalation allowed"};
        return d;
    }
    if (c.frontierAttempts >= c.maxFrontierAttempts) {
        d.action = Action::fail;
        d.reasons = {what, "local attempts exhausted", "frontier attempts exhausted (" + std::to_string(c.maxFrontierAttempts) + ")"};
        return d;
    }
    d.action = Action::escalate;
    d.tier = "frontier-stub";
    d.reasons = {what, "local attempts exhausted (" + std::to_string(c.localAttempts) + "): escalate once to the frontier tier (stub: nothing leaves the machine)"};
    return d;
}

}  // namespace sub0squad
