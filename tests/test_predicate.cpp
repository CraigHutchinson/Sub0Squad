// The predicate language of a process (docs/PROCESS_DESIGN.md section 3, predicate.hpp). Pinned here: every name of the vocabulary and what it reads; the
// operators and their precedence; that `all` over no items is false; that everything wrong is refused when the text is PARSED, with the column (so evaluating
// never fails); that a stage can read only the stages it was given (the earlier ones); and `firstFalse`, the atom that explains why a stage waits.
#include "check.hpp"
#include "sub0squad/predicate.hpp"

using namespace sub0squad;

static const Vocabulary kVocabulary{{"plan", "build"}, {"planner", "integrator"}};

static bool eval(const char* text, const Snapshot& s)
{
    auto p = Predicate::parse(text, kVocabulary);
    if (!p) { std::fprintf(stderr, "does not parse: %s: %s\n", text, p.error().c_str()); ++gFailures; return false; }
    return p->eval(s);
}
static std::string errorOf(const char* text) { auto p = Predicate::parse(text, kVocabulary); return p ? std::string{"(parsed)"} : p.error(); }
static std::string why(const char* text, const Snapshot& s) { auto p = Predicate::parse(text, kVocabulary); return p ? p->firstFalse(s) : p.error(); }
static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

int main()
{
    Snapshot empty;
    // Literals, and the names that read one fact each.
    CHECK(eval("always", empty) && eval("true", empty) && !eval("false", empty));
    {
        Snapshot s;
        CHECK(!eval("plan.present", s) && !eval("requests.openBlocking", s));
        s.planPresent = true; s.openBlocking = true;
        CHECK(eval("plan.present", s) && eval("requests.openBlocking", s));
    }
    // Stages: done, active, failed; a stage the snapshot has no facts for reads as false (total).
    {
        Snapshot s;
        CHECK(!eval("stage.plan.done", s) && !eval("stage.plan.active", s) && !eval("stage.plan.failed", s));
        s.stages["plan"] = {true, false, false};
        s.stages["build"] = {false, true, true};
        CHECK(eval("stage.plan.done", s) && !eval("stage.plan.active", s));
        CHECK(!eval("stage.build.done", s) && eval("stage.build.active", s) && eval("stage.build.failed", s));
    }
    // Items: all, any, count; `all` over NO items is false (a stage whose items do not exist yet is not done).
    {
        Snapshot s;
        CHECK(!eval("items.all(accepted)", s) && !eval("items.any(accepted)", s) && eval("items.count() == 0", s) && eval("items.count(accepted) == 0", s));
        s.accepted = 2; s.pending = 1;
        CHECK(!eval("items.all(accepted)", s) && eval("items.any(accepted)", s) && eval("items.any(pending)", s) && !eval("items.any(failed)", s) && !eval("items.any(running)", s));
        CHECK(eval("items.count() == 3", s) && eval("items.count(accepted) >= 2", s) && eval("items.count(accepted) > 1", s) && !eval("items.count(accepted) > 2", s));
        CHECK(eval("items.count(pending) < 2", s) && eval("items.count(pending) <= 1", s) && eval("items.count(failed) != 1", s));
        s.pending = 0;
        CHECK(eval("items.all(accepted)", s));
        s.running = 1; s.failed = 1;
        CHECK(!eval("items.all(accepted)", s) && eval("items.any(running)", s) && eval("items.any(failed)", s));
    }
    // Jobs of a step: the latest one's outcome and how many there were; a step with no job is all false.
    {
        Snapshot s;
        CHECK(!eval("job(planner).succeeded", s) && !eval("job(planner).failed", s) && !eval("job(planner).active", s) && eval("job(planner).tries == 0", s));
        s.jobs["planner"] = {2, false, true, false};
        s.jobs["integrator"] = {1, true, false, false};
        CHECK(eval("job(planner).failed", s) && eval("job(planner).tries == 2", s) && !eval("job(planner).succeeded", s));
        CHECK(eval("job(integrator).succeeded", s) && !eval("job(integrator).active", s));
    }
    // Operators: ! binds tighter than &&, && tighter than ||; parentheses; == and != on conditions.
    {
        Snapshot s;
        s.planPresent = true;
        CHECK(eval("plan.present || false && false", s));               // && first: true || (false && false)
        CHECK(!eval("(plan.present || false) && false", s));
        CHECK(eval("!requests.openBlocking && plan.present", s));       // ! applies to the one atom
        CHECK(!eval("!(requests.openBlocking || plan.present)", s));
        CHECK(eval("!!plan.present", s));
        CHECK(eval("plan.present == true", s) && eval("plan.present != requests.openBlocking", s) && eval("1 < 2", s) && !eval("2 <= 1", s));
        CHECK(eval("  plan.present\t&&\n always ", s));                 // white space is free
    }
    // The standard process's own predicates, on the snapshots it meets.
    {
        Snapshot s;
        const char* integrateEntry = "stage.build.done && !requests.openBlocking";
        CHECK(!eval(integrateEntry, s));
        s.stages["build"] = {true, false, false};
        CHECK(eval(integrateEntry, s));
        s.openBlocking = true;
        CHECK(!eval(integrateEntry, s));
    }
    // Everything wrong is refused at parse time, and the message says where and what.
    {
        CHECK(contains(errorOf(""), "column 1: expected a condition"));
        CHECK(contains(errorOf("plan.present &&"), "expected a condition"));
        CHECK(contains(errorOf("nonsense"), "unknown name 'nonsense'"));
        CHECK(contains(errorOf("plan.absent"), "'plan' has only '.present'"));
        CHECK(contains(errorOf("requests.open"), "'requests' has only '.openBlocking'"));
        CHECK(contains(errorOf("stage.integrate.done"), "'integrate' is not an earlier stage") && contains(errorOf("stage.integrate.done"), "plan, build"));   // no cycles: only what came before
        CHECK(contains(errorOf("stage.plan.finished"), "a stage has '.done', '.active' and '.failed'"));
        CHECK(contains(errorOf("items.all(done)"), "'done' is not an item state"));
        CHECK(contains(errorOf("items.all()"), "expected a name inside '()'"));
        CHECK(contains(errorOf("items.most(accepted)"), "'items' has '.all(STATE)'"));
        CHECK(contains(errorOf("job(reviewer).succeeded"), "'reviewer' is not the step of a job stage") && contains(errorOf("job(reviewer).succeeded"), "planner, integrator"));
        CHECK(contains(errorOf("job(planner).exitCode"), "a job has '.succeeded'"));
        CHECK(contains(errorOf("items.count(accepted)"), "a predicate is a condition, not a number"));
        CHECK(contains(errorOf("items.count(accepted) && plan.present"), "'&&' joins two conditions"));
        CHECK(contains(errorOf("plan.present || 3"), "'||' joins two conditions"));
        CHECK(contains(errorOf("!3"), "'!' negates a condition"));
        CHECK(contains(errorOf("plan.present < 3"), "compares two numbers or two conditions"));
        CHECK(contains(errorOf("plan.present < always"), "conditions are compared with == or !="));
        CHECK(contains(errorOf("(plan.present"), "expected ')'"));
        CHECK(contains(errorOf("plan.present always"), "unexpected 'a'"));
        CHECK(contains(errorOf("plan.present; rm -rf"), "unexpected ';'"));
        CHECK(contains(errorOf("99999999999999999999 > 1"), "too large"));
        CHECK(contains(errorOf(std::string(401, ' ').c_str()), "at most 400 characters"));
        CHECK(contains(errorOf("stage.plan.done && job(planner).tries >"), "column"));
        CHECK(contains(Predicate::parse("stage.plan.done", Vocabulary{}).error(), "none"));                // the first stage has nothing to read
        CHECK(contains(Predicate::parse("job(planner).succeeded", Vocabulary{}).error(), "it has none"));
    }
    // firstFalse: the atom that keeps the predicate from holding, in reading order; "" when it holds.
    {
        Snapshot s;
        CHECK_EQ(why("always", s), std::string(""));
        CHECK_EQ(why("stage.build.done && !requests.openBlocking", s), std::string("stage.build.done"));
        s.stages["build"] = {true, false, false};
        s.openBlocking = true;
        CHECK_EQ(why("stage.build.done && !requests.openBlocking", s), std::string("!requests.openBlocking"));
        s.openBlocking = false;
        CHECK_EQ(why("stage.build.done && !requests.openBlocking", s), std::string(""));
        CHECK_EQ(why("items.all(accepted) || job(integrator).succeeded", s), std::string("items.all(accepted)"));
        CHECK_EQ(why("plan.present && items.count(accepted) >= 2", s), std::string("plan.present"));
        s.planPresent = true;
        CHECK_EQ(why("plan.present && items.count(accepted) >= 2", s), std::string("items.count(accepted) >= 2"));
        CHECK_EQ(why("!(items.count() == 0)", s), std::string("!(items.count() == 0)"));
    }
    // A parsed predicate keeps its text and can be copied and evaluated again and again with the same answer (no state, no clock).
    {
        auto p = Predicate::parse("plan.present", kVocabulary);
        CHECK(p.has_value());
        if (p) {
            const Predicate copy = *p;
            Snapshot s;
            s.planPresent = true;
            CHECK(copy.text() == "plan.present" && copy.eval(s) && copy.eval(s) && p->eval(s));
        }
    }
    return finish();
}
