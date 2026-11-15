#include "sub0squad/plan_checks.hpp"

#include "sub0squad/harness.hpp"

#include <algorithm>
#include <set>

namespace sub0squad {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

}  // namespace

std::vector<std::string> checkPlan(const Plan& plan)
{
    std::vector<std::string> problems;
    const Harness* harness = findHarness(plan.harness);
    // Small models write what they are asked and no more: a plan that is large is a plan whose reply is cut off, and whose pieces nobody can hold together
    // (the first real planner wrote 20 KB and a lexer, a parser and a "misc" item for one calculator).
    if (plan.items.size() > kMaxPlanItems)
        problems.push_back("the plan has " + std::to_string(plan.items.size()) + " items; use at most " + std::to_string(kMaxPlanItems) + ": one tester item and one developer item per independent deliverable (a library, a command-line program). Do NOT make items of a file's internal parts (a lexer, a parser): the developer divides its own file into sections");
    if (plan.toJson().size() > kMaxPlanBytes)
        problems.push_back("the plan is " + std::to_string(plan.toJson().size()) + " bytes; keep it under " + std::to_string(kMaxPlanBytes) + " (short cards, one-line examples, no filler)");
    for (const auto& it : plan.items) {
        const std::string suffix = harness ? harness->testSuffix : "";
        if (it.persona == "tester" && !suffix.empty())
            for (const auto& f : it.files)
                if (!endsWith(f, suffix) || f.rfind("tests/", 0) != 0)
                    problems.push_back("tester item '" + it.id + "': the test file '" + f + "' must be named tests/NAME" + suffix + " (the build compiles exactly those)");
        for (const auto* list : {&it.card.acceptance, &it.card.examples})
            for (const auto& line : *list)
                if (line.find("handled correctly") != std::string::npos || line.find("Other special") != std::string::npos)
                    problems.push_back("item '" + it.id + "': '" + line.substr(0, 60) + "' is filler, not a check or an example; state something observable or leave it out");
    }
    std::set<std::string> testerIds;
    for (const auto& it : plan.items) if (it.persona == "tester") testerIds.insert(it.id);
    if (testerIds.empty()) problems.push_back("the plan has no tester item: the work is test-first, so a tester item (persona \"tester\", no dependencies) must write the tests the developers are graded by");

    for (const auto& it : plan.items) {
        if (it.card.acceptance.empty()) problems.push_back("item '" + it.id + "' has no acceptance: list the observable checks that say it is done");
        if (it.card.contract.empty()) problems.push_back("item '" + it.id + "' has an empty contract: state the interface exactly (names, parameters, results, errors)");
        if (it.persona == "tester") {
            if (!it.gate.mustFailOnStub) problems.push_back("tester item '" + it.id + "': set gate.mustFailOnStub to true (tests that pass against a stub test nothing)");
            if (it.gate.minTests < 5) problems.push_back("tester item '" + it.id + "': gate.minTests is " + std::to_string(it.gate.minTests) + "; ask for at least 5 tests");
            for (const auto& t : it.gate.tests)
                if (std::find(it.files.begin(), it.files.end(), t) == it.files.end()) problems.push_back("tester item '" + it.id + "': the gate runs '" + t + "' but the item does not list it in files");
            if (static_cast<int>(it.card.examples.size()) < kMinWorkedExamples)
                problems.push_back("tester item '" + it.id + "': give at least " + std::to_string(kMinWorkedExamples) + " worked examples in card.examples (one per line, input and exact result, for example \"2 + 3 * 4 = 14\"); the tests are checked against them");
        }
        if (it.persona == "developer") {
            const bool hasTester = std::any_of(it.deps.begin(), it.deps.end(), [&](const std::string& d) { return testerIds.count(d) > 0; });
            if (!hasTester) problems.push_back("developer item '" + it.id + "' depends on no tester item: add the tester item's id to deps so the developer is graded by tests it did not write");
            if (it.gate.tests.empty()) problems.push_back("developer item '" + it.id + "' has no gate.tests: name the tester's test files it must pass");
            if (static_cast<int>(it.card.examples.size()) < kMinWorkedExamples)
                problems.push_back("developer item '" + it.id + "': give at least " + std::to_string(kMinWorkedExamples) + " worked examples in card.examples (the same facts the tester is given)");
            for (const auto& t : it.gate.tests)
                if (std::find(it.card.include.begin(), it.card.include.end(), t) == it.card.include.end())
                    problems.push_back("developer item '" + it.id + "': put '" + t + "' in card.include so the developer reads the tests it is graded by");
        }
    }

    // The project's own interface (a harness whose scaffold is the plan's): every file a developer writes needs a stub the tests can link against, and
    // some header must say what the stubs implement. Only the C++ harness splits the instruments from the project's interface today.
    if (harness && plan.harness == "cpp-cmake") {
        const auto has = [&](const std::string& path) { return std::any_of(plan.scaffold.begin(), plan.scaffold.end(), [&](const PlanFile& f) { return f.path == path; }); };
        const bool header = std::any_of(plan.scaffold.begin(), plan.scaffold.end(), [](const PlanFile& f) { return f.path.rfind("include/", 0) == 0 && (endsWith(f.path, ".hpp") || endsWith(f.path, ".h")); });
        if (!header) problems.push_back("the plan's scaffold has no header under include/: write the project's interface as a header (scaffold: [{\"path\": \"include/NAME/NAME.hpp\", \"content\": \"...\"}]) that declares what the developers implement");
        for (const auto& f : plan.scaffold)
            if (endsWith(f.path, ".cpp") && std::count(f.content.begin(), f.content.end(), '\n') > 30)
                problems.push_back("the stub '" + f.path + "' is longer than 30 lines: a stub only defines each declared function with a neutral body (return 0; or an empty main). The real code is the developer's work, and a stub that already works makes the tests pass against it");
        for (const auto& it : plan.items) {
            if (it.persona != "developer") continue;
            for (const auto& f : it.files)
                if (endsWith(f, ".cpp") && f.rfind("src/", 0) == 0 && !has(f))
                    problems.push_back("developer file '" + f + "' has no stub in the scaffold: add {\"path\": \"" + f + "\", \"content\": \"...\"} defining every declared function with a body that returns a neutral value and never throws, so the tester's tests fail against it instead of crashing");
        }
    }
    return problems;
}

}  // namespace sub0squad
