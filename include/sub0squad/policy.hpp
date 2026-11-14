#pragma once
/// @file policy.hpp
/// The decision seam (docs/DESIGN.md section 16): given what is known about an item that just failed, answer retry, escalate or
/// fail, with reasons and the inputs used so the decision can be audited and replayed offline against a different policy.

#include <set>
#include <string>
#include <string_view>
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
    // The escalation ladder (docs/OPTIMIZATION.md): what has been tried for this item, and where it sits in the plan.
    int depth = 0;                 ///< division depth of the item (dots in its id)
    int totalAttempts = 0;         ///< every attempt-like job so far: agent, decompose and rearchitect all count
    int maxTotalAttempts = 10;   // 8 plus the near-pass repair allowance, so repairs never starve the final escalation
    bool splittable = false;       ///< more than one file, or complexity of at least 2
    bool hasTesterDep = false;     ///< the item is graded by tests another item produced
    bool repeatedFailure = false;  ///< the same tests failed with the same messages as the previous attempt (gate exit code 5)
    bool oversize = false;         ///< the attempt did not run: the agent measured the item as too big for one attempt (agent exit code 4)
    bool frontierHasReference = true;  ///< the frontier stub can produce this item (a reference exists); false = escalation would only repeat a failure
    bool interfaceGap = false;     ///< the failed gate found a name the tests need that the module does not provide (gate exit code 6): the contract is the next scope up, not another leaf attempt
    bool nearPass = false;         ///< the failed gate had at least 10 tests and at most 15 % of them failing (gate exit code 3)
    int maxNearPassRepairs = 2;    ///< extra local repair attempts granted to a near pass before the ladder moves on
    bool decomposeTried = false, reviseTried = false, rearchitectTried = false;
    int maxDepth = 2;
    /// Whose work it is. Only a DEVELOPER's item is ever divided: a tester writes one test file, and an architect asked to divide it invents
    /// implementation work (docs/INTERVENTIONS.md row 44).
    /// The rungs of the ladder this workflow's PROCESS has (docs/PROCESS_DESIGN.md): a rung it lacks is skipped, and the ladder goes on to the next one. The
    /// recorded decision lists the rungs the process lacks (`processLacks`, only when there are any), so it can be explained from its own inputs.
    bool allowDecompose = true, allowSections = true, allowReviseTests = true, allowRearchitect = true, allowEscalate = true;
    std::string persona = "developer";
    /// The project's harness divides work by SECTIONS of a file, never into new modules (row 45: in a compiled language a new module needs a header that
    /// no item owns). Where the ladder would divide, it writes the item as forked sections instead, once.
    bool divideBySections = false, sectionsTried = false;
    /// The attempt that just failed already ran as forked sections (catalogue row 73): the `sections` rung would repeat the failing attempt's own executor,
    /// so it is skipped. A rung never repeats what just failed.
    bool forked = false;
};

/// retry: another local attempt. decompose: ask the model to divide the item. reviseTests: send back to the producer (the tester that wrote
/// the tests this item is graded by). rearchitect: a dedicated job rewrites the work card. escalate: the frontier-stub tier. fail.
/// sections: the next attempt writes the item as forked sections on the canvas (a skeleton, then one function per session).
enum class Action { retry, escalate, fail, decompose, reviseTests, rearchitect, sections };

struct Decision
{
    Action action = Action::fail;
    std::string tier;                  ///< tier for the next attempt ("local" or "frontier-stub"), empty on fail
    std::vector<std::string> reasons;  ///< ordered, human readable
    double confidence = 1.0;
};

[[nodiscard]] const char* toString(Action a);
[[nodiscard]] std::string toJson(const DecisionContext& ctx, const Decision& d);

/// Divide an item before it is attempted? A division is not free: an architect job writes about 2,700 tokens (10 to 15 minutes at 3 to 5 tok/s on the
/// 9B) before any part starts, plus the parts' own tests and the integration. So: always when the estimate cannot fit one attempt (`ceiling`);
/// never at or under the soft budget; in between only when the time saved on the critical path, `estimate x (1 - 1/parallelism)` with the parallelism the
/// engine's idle slots allow (at most 4), is more than the division costs. On a slow engine this is nearly the ceiling alone; on a fast one it divides
/// more. An unknown engine state counts as no spare capacity.
/// Capacity classes of the daemon's job slots (catalogue row 19). A workflow's STRUCTURAL jobs (everything that is not an agent's attempt: gates, the planner,
/// integration, the architect) decide what the workers do next, and most of them are short and need no engine. Queued behind workers that hold every job
/// slot for an hour each, a ten-second gate kept an accepted item from being accepted and its dependants from starting.
///   * a structural job that holds no engine lease may use one of `kStructuralReserve` slots of its own, beyond `--max-jobs`;
///   * among the jobs that may start, structural ones go first (so the architect gets the next free engine slot), then the oldest.
/// It is a reserve and an order, not strict priority: a running worker is never stopped for a structural job.
/// Decided (lead, 2026-11-14): the reserve stays two slots, and a job that needs the engine (the architect) gets NO reserved engine slot: for it the class
/// is an order only.
inline constexpr int kStructuralReserve = 2;
/// Whether a job is structural: it belongs to a workflow and its step is not an agent's attempt.
[[nodiscard]] bool isStructuralJob(std::string_view workflow, std::string_view role);
/// Whether a queued job may start now. `structuralUnleased`: the candidate is structural and holds no lease. `activeNormal`: running jobs that count against
/// `maxJobs` in the ordinary way; `activeReserved`: running structural jobs without a lease.
[[nodiscard]] bool admitsJob(bool structuralUnleased, int activeNormal, int activeReserved, int maxJobs);

[[nodiscard]] bool shouldDivide(long estimate, long softBudget, long ceiling, int idleSlots, int totalSlots, long divisionCost = 2700);

/// Which slot of a lease a starting job takes: the one its node used last time when it is free (the server keeps that slot's cache, so a retry
/// skips the prefill of the card and tests it shares with the earlier attempt: measured 19 s cold against 4.5 s warm for a 3,000 token prompt on the 9B),
/// otherwise the lowest free one. `used` holds the slots in use; the result is below `cap` whenever one is free.
[[nodiscard]] int chooseLeaseSlot(int cap, const std::set<int>& used, int preferred);

class IDecisionPolicy
{
public:
    virtual ~IDecisionPolicy() = default;
    [[nodiscard]] virtual Decision decide(const DecisionContext& ctx) const = 0;
    [[nodiscard]] virtual const char* name() const = 0;
};

/// The default, transparent policy. After N local attempts the ladder is: decompose (the model decides the division), reviseTests (send
/// back to the producer), rearchitect (a dedicated job rewrites the card), escalate to frontier-stub once if allowed, fail. Each rung is tried
/// once, every rung counts as an attempt, and `maxTotalAttempts` bounds the whole ladder.
class RulePolicy final : public IDecisionPolicy
{
public:
    [[nodiscard]] Decision decide(const DecisionContext& ctx) const override;
    [[nodiscard]] const char* name() const override { return "rules-1"; }
};

}  // namespace sub0squad
