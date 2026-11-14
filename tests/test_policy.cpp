// The decision rules as pure functions: the ladder (near pass, testers, sections, oversize, interface gap, repeated failure, the stub), capacity classes,
// when a division pays, slot affinity.
#include "workflow_sim.hpp"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/briefing.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/division.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/overseer.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/workflow.hpp"

int main()
{
    { RulePolicy policy; // Near pass: a gate failure with few failing tests is repaired (extra local attempts), not divided; a bad failure still climbs the ladder.
        DecisionContext c; c.workflow = "w"; c.item = "engine"; c.attempt = 2; c.localAttempts = 2; c.failedRole = "gate"; c.splittable = true; c.nearPass = true;
        c.totalAttempts = 2;
        CHECK(policy.decide(c).action == Action::retry);
        c.localAttempts = 4;                       // the repair allowance is used up
        CHECK(policy.decide(c).action == Action::decompose);
        c.localAttempts = 2; c.nearPass = false;   // not a near pass: the ladder starts at decompose
        CHECK(policy.decide(c).action == Action::decompose);
    }
    { RulePolicy policy; // Row 44: only a developer's item is ever divided. A tester that fails goes retry, then the card is rewritten, then escalation or fail.
        DecisionContext c; c.persona = "tester"; c.localAttempts = 2; c.maxLocalAttempts = 2; c.totalAttempts = 2; c.splittable = true; c.frontier = FrontierMode::off;
        CHECK(policy.decide(c).action == Action::rearchitect);
        c.oversize = true;
        CHECK(policy.decide(c).action == Action::rearchitect);
        c.rearchitectTried = true;
        CHECK(policy.decide(c).action == Action::fail);
        c.persona = "developer"; c.oversize = false; c.rearchitectTried = false;
        CHECK(policy.decide(c).action == Action::decompose);                                   // the same context for a developer divides
        // The validator says the same of a fragment: a tester's item is not divided, least of all into implementation work.
        Plan base = Plan::fromJson(blueprintJson("calculator-web")).value();
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem d1; d1.id = "dev-calc"; d1.persona = "developer"; d1.files = {"src/x.js"};
        WorkItem d2; d2.id = "dev-cli"; d2.persona = "developer"; d2.files = {"src/y.js"};
        frag.items = {d1, d2};
        const auto refused = Plan::amended(base, "test-engine", frag);
        CHECK(!refused.has_value() && refused.error().find("is a tester's item and cannot be divided into implementation work (part 'dev-calc' is a developer's)") != std::string::npos);
    }
    { RulePolicy policy; // Row 45: where the platform divides by sections of a file, the ladder never asks for a division into new modules; it writes the item as forked sections, once.
        DecisionContext c; c.divideBySections = true; c.localAttempts = 2; c.maxLocalAttempts = 2; c.totalAttempts = 2; c.splittable = true;
        CHECK(policy.decide(c).action == Action::sections);
        c.sectionsTried = true; c.hasTesterDep = true;
        CHECK(policy.decide(c).action == Action::reviseTests);                                 // then the ladder goes on, never to decompose
        DecisionContext o; o.divideBySections = true; o.oversize = true; o.localAttempts = 1; o.totalAttempts = 1;
        CHECK(policy.decide(o).action == Action::sections);                                    // too big for one reply: sections at once, even for one file
        o.persona = "tester";
        CHECK(policy.decide(o).action == Action::retry);
        CHECK(std::string(toString(Action::sections)) == "sections");
    }
    { RulePolicy policy; // Plan phase: an oversize verdict divides at once, before any local retry; at the depth limit it does not.
        DecisionContext c; c.workflow = "w"; c.item = "engine"; c.attempt = 1; c.localAttempts = 1; c.failedRole = "agent"; c.splittable = true; c.oversize = true; c.totalAttempts = 1;
        CHECK(policy.decide(c).action == Action::decompose);
        c.depth = c.maxDepth;
        CHECK(policy.decide(c).action == Action::retry);
        c.depth = 0; c.decomposeTried = true;
        CHECK(policy.decide(c).action == Action::retry);
    }
    { RulePolicy policy; // Interface mismatch (gate exit 6): go up a scope at once (the card is rewritten to name every export), not another leaf attempt; once only.
        DecisionContext c; c.workflow = "w"; c.item = "functions"; c.attempt = 1; c.localAttempts = 1; c.failedRole = "gate"; c.hasTesterDep = true; c.interfaceGap = true; c.totalAttempts = 1;
        CHECK(policy.decide(c).action == Action::rearchitect);
        c.rearchitectTried = true;
        CHECK(policy.decide(c).action == Action::retry);
    }
    { RulePolicy policy; // Repeated identical failure: the tests are sent back at once, once; afterwards the ordinary ladder applies.
        DecisionContext c; c.workflow = "w"; c.item = "lexer"; c.attempt = 2; c.localAttempts = 2; c.failedRole = "gate"; c.hasTesterDep = true; c.repeatedFailure = true; c.nearPass = true; c.totalAttempts = 2;
        CHECK(policy.decide(c).action == Action::reviseTests);
        c.reviseTried = true;
        CHECK(policy.decide(c).action != Action::reviseTests);
    }
    { RulePolicy policy; // The frontier stub is only used when it has a reference; a repeated failure disputes the tests only when it is a near pass.
        DecisionContext c; c.workflow = "w"; c.item = "lexer"; c.attempt = 6; c.localAttempts = 2; c.failedRole = "gate"; c.totalAttempts = 6;
        c.decomposeTried = c.reviseTried = c.rearchitectTried = true; c.splittable = true;
        CHECK(policy.decide(c).action == Action::escalate);
        c.frontierHasReference = false;
        CHECK(policy.decide(c).action == Action::fail);
        DecisionContext r; r.workflow = "w"; r.item = "ui-js"; r.attempt = 2; r.localAttempts = 2; r.failedRole = "gate"; r.hasTesterDep = true; r.splittable = true; r.totalAttempts = 2;
        r.repeatedFailure = false;  // 3 of 7 failing twice is not a near pass, so the gate does not flag it: the ordinary ladder applies
        CHECK(policy.decide(r).action == Action::decompose);
    }
    {   // Row 19: capacity classes. A structural job without a lease has a reserve of its own; everything else shares --max-jobs; overflow of the reserve
        // takes ordinary slots.
        CHECK(isStructuralJob("w-000001", "gate") && isStructuralJob("w-000001", "planner") && isStructuralJob("w-000001", "decompose") && isStructuralJob("w-000001", "integrator"));
        CHECK(!isStructuralJob("w-000001", "agent") && !isStructuralJob("", "gate") && !isStructuralJob("w-000001", ""));   // an attempt is a worker; a plain job is nobody's structure
        CHECK(!admitsJob(false, 2, 0, 2));                     // workers hold both slots: another worker waits
        CHECK(admitsJob(true, 2, 0, 2) && admitsJob(true, 2, 1, 2));   // ... a gate does not: it has the reserve
        CHECK(!admitsJob(true, 2, kStructuralReserve, 2));     // the reserve is full and so are the ordinary slots
        CHECK(admitsJob(true, 1, kStructuralReserve, 2));      // the reserve is full, an ordinary slot is free: it takes that
        CHECK(!admitsJob(false, 1, kStructuralReserve + 1, 2)); // a structural job beyond the reserve occupies an ordinary slot
        CHECK(admitsJob(false, 1, kStructuralReserve, 2) && admitsJob(false, 0, 0, 1));
    }
    { // Divide only when it pays: over the ceiling always, under the soft budget never, in between only when the saving beats the division's cost.
        CHECK(shouldDivide(3000, 1800, 2800, 7, 8));                 // cannot fit one attempt: divide whatever it costs
        CHECK(!shouldDivide(1500, 1800, 2800, 7, 8));                // under the soft budget: never
        CHECK(!shouldDivide(2400, 1800, 2800, 7, 8));                // 2400 x (1 - 1/4) = 1800 saved, less than the 2700 the division costs
        CHECK(shouldDivide(2400, 1800, 2800, 7, 8, 1000));           // a fast engine (a cheap division): worth it
        CHECK(!shouldDivide(2400, 1800, 2800, 0, 8, 1000));          // no spare slots: nothing runs beside it, so nothing is saved
        CHECK(!shouldDivide(2400, 1800, 2800, -1, 0, 1000));         // unknown engine state counts as no spare capacity
        CHECK(!shouldDivide(9000, 0, 2800, 8, 8));                   // size splitting off (budget 0) stays off
    }
    { // Slot affinity: a node's retry returns to its earlier slot when it is free; otherwise the lowest free slot.
        CHECK_EQ(chooseLeaseSlot(8, {}, -1), 0);
        CHECK_EQ(chooseLeaseSlot(8, {0, 1}, -1), 2);
        CHECK_EQ(chooseLeaseSlot(8, {0, 1, 5}, 5), 2);   // preferred is taken: lowest free
        CHECK_EQ(chooseLeaseSlot(8, {0, 1}, 5), 5);      // preferred is free: kept, so the prompt cache hits
        CHECK_EQ(chooseLeaseSlot(4, {0}, 9), 1);         // preferred beyond the cap is ignored
    }
    return finish();
}
