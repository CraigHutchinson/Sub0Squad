#pragma once
/// @file policy.hpp
/// The decision seam (docs/DESIGN.md section 16): given what is known about an item that just failed, answer retry, escalate or
/// fail, with reasons and the inputs used so the decision can be audited and replayed offline against a different policy.

#include <string>
#include <vector>

namespace sub0squad {

/// "off": no frontier tier at all. "stub": the full escalation path runs, nothing is sent (the development default).
/// There is no "live" in this build; it exists only as an owner switch in a later milestone.
enum class FrontierMode { off, stub };

struct DecisionContext
{
    std::string workflow, item;
    int attempt = 0;               ///< the attempt that just failed (1-based)
    int localAttempts = 0;         ///< attempts made on the local tier so far
    int frontierAttempts = 0;      ///< attempts made on the frontier-stub tier so far
    std::string failedRole;        ///< "agent" or "gate"
    std::string failure;           ///< short description (exit code, gate output tail)
    int complexity = 1;            ///< planner estimate 1..5
    FrontierMode frontier = FrontierMode::stub;
    int maxLocalAttempts = 2;
    int maxFrontierAttempts = 1;
};

enum class Action { retry, escalate, fail };

struct Decision
{
    Action action = Action::fail;
    std::string tier;                  ///< tier for the next attempt ("local" or "frontier-stub"), empty on fail
    std::vector<std::string> reasons;  ///< ordered, human readable
    double confidence = 1.0;
};

[[nodiscard]] const char* toString(Action a);
[[nodiscard]] std::string toJson(const DecisionContext& ctx, const Decision& d);

class IDecisionPolicy
{
public:
    virtual ~IDecisionPolicy() = default;
    [[nodiscard]] virtual Decision decide(const DecisionContext& ctx) const = 0;
    [[nodiscard]] virtual const char* name() const = 0;
};

/// The default, transparent policy: retry locally up to a limit, then escalate to the frontier-stub tier once if allowed, else fail.
class RulePolicy final : public IDecisionPolicy
{
public:
    [[nodiscard]] Decision decide(const DecisionContext& ctx) const override;
    [[nodiscard]] const char* name() const override { return "rules-1"; }
};

}  // namespace sub0squad
