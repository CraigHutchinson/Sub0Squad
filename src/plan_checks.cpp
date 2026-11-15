#include "sub0squad/plan_checks.hpp"

#include "sub0squad/harness.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace sub0squad {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

// A stub ignores its parameters, and -Werror turns that into a failure the planner has made in every probe. Name only what the body reads: in every function
// definition `name(params) ... { body }`, a parameter whose last identifier the body never mentions loses that identifier.
std::string unnameUnusedParams(const std::string& src)
{
    std::string out;
    std::size_t at = 0;
    const auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (at < src.size()) {
        const std::size_t open = src.find('(', at);
        if (open == std::string::npos) break;
        int depth = 0;
        std::size_t close = open;
        for (; close < src.size(); ++close) { if (src[close] == '(') ++depth; else if (src[close] == ')' && --depth == 0) break; }
        if (close >= src.size()) break;
        std::size_t brace = close + 1;
        while (brace < src.size() && (std::isspace(static_cast<unsigned char>(src[brace])) || src.compare(brace, 5, "const") == 0 || src.compare(brace, 8, "noexcept") == 0)) brace += src.compare(brace, 5, "const") == 0 ? 5 : src.compare(brace, 8, "noexcept") == 0 ? 8 : 1;
        if (brace >= src.size() || src[brace] != '{') { out += src.substr(at, close + 1 - at); at = close + 1; continue; }   // a call or a declaration
        int bd = 0;
        std::size_t end = brace;
        for (; end < src.size(); ++end) { if (src[end] == '{') ++bd; else if (src[end] == '}' && --bd == 0) break; }
        const std::string body = src.substr(brace, end - brace);
        std::string params = src.substr(open + 1, close - open - 1), rebuilt;
        std::size_t start = 0;
        while (start <= params.size()) {
            std::size_t comma = params.find(',', start);
            if (comma == std::string::npos) comma = params.size();
            std::string one = params.substr(start, comma - start);
            if (one.find('(') == std::string::npos && one.find('=') == std::string::npos) {
                std::size_t e = one.size();
                while (e > 0 && std::isspace(static_cast<unsigned char>(one[e - 1]))) --e;
                std::size_t b = e;
                while (b > 0 && isId(one[b - 1])) --b;
                const std::string name = one.substr(b, e - b);
                const bool hasType = one.substr(0, b).find_first_not_of(" \t\n") != std::string::npos;
                bool used = false;
                for (std::size_t f = body.find(name); !name.empty() && f != std::string::npos; f = body.find(name, f + 1))
                    if ((f == 0 || !isId(body[f - 1])) && (f + name.size() >= body.size() || !isId(body[f + name.size()]))) { used = true; break; }
                const bool keyword = name == "int" || name == "double" || name == "float" || name == "char" || name == "bool" || name == "long" || name == "void" || name == "string" || name == "unsigned";
                if (!name.empty() && hasType && !used && !keyword) { one = one.substr(0, b); while (!one.empty() && std::isspace(static_cast<unsigned char>(one.back()))) one.pop_back(); }
            }
            rebuilt += (start ? "," : "") + (start && !one.empty() && one[0] != ' ' ? std::string(" ") : std::string{}) + one;
            if (comma == params.size()) break;
            start = comma + 1;
        }
        out += src.substr(at, open + 1 - at) + rebuilt + ")";
        at = close + 1;
    }
    out += src.substr(at);
    return out;
}

}  // namespace

std::vector<std::string> mendPlan(Plan& plan)
{
    std::vector<std::string> mends;
    for (auto& it : plan.items) {
        if (it.persona != "tester" && it.persona != "developer") continue;
        const int reachable = std::max(5, static_cast<int>(it.card.examples.size()) + 2);
        if (it.persona == "tester") {
            if (!it.gate.mustFailOnStub) { it.gate.mustFailOnStub = true; mends.push_back("item '" + it.id + "': gate.mustFailOnStub set to true"); }
            if (it.gate.minTests < 5) { it.gate.minTests = 5; mends.push_back("item '" + it.id + "': gate.minTests raised to 5"); }
        }
        if (it.gate.minTests > reachable) { it.gate.minTests = reachable; mends.push_back("item '" + it.id + "': gate.minTests lowered to " + std::to_string(reachable)); }
    }
    for (auto& dev : plan.items) {
        if (dev.persona != "developer") continue;
        const bool tied = std::any_of(dev.deps.begin(), dev.deps.end(), [&](const std::string& d) {
            return std::any_of(plan.items.begin(), plan.items.end(), [&](const WorkItem& o) { return o.id == d && o.persona == "tester"; }); });
        if (tied || dev.gate.tests.empty()) continue;
        std::vector<const WorkItem*> owners;
        for (const auto& o : plan.items)
            if (o.persona == "tester" && !o.files.empty() && std::all_of(dev.gate.tests.begin(), dev.gate.tests.end(), [&](const std::string& t) { return std::find(o.files.begin(), o.files.end(), t) != o.files.end(); })) owners.push_back(&o);
        if (owners.size() == 1) { dev.deps.push_back(owners[0]->id); mends.push_back("item '" + dev.id + "': depends on '" + owners[0]->id + "' (the tester that owns its tests)"); }
    }
    // A card's `include` names files the worker is shown. A path no item writes and the scaffold does not hold shows nothing (the first end-to-end run: a
    // card listed "src/engine.h" while the header was include/calc/engine.h, and the tester invented "calc/calc.hpp"). Drop those, and show every project
    // header to every tester and developer, so the include line the code needs is on the card.
    {
        std::set<std::string> known;
        for (const auto& f : plan.scaffold) known.insert(f.path);
        for (const auto& it : plan.items) for (const auto& f : it.files) known.insert(f);
        std::vector<std::string> headers;
        for (const auto& f : plan.scaffold) if (f.path.rfind("include/", 0) == 0 && (endsWith(f.path, ".hpp") || endsWith(f.path, ".h"))) headers.push_back(f.path);
        for (auto& it : plan.items) {
            if (it.persona != "tester" && it.persona != "developer") continue;
            for (auto at = it.card.include.begin(); at != it.card.include.end();) {
                if (known.count(*at) == 0) { mends.push_back("item '" + it.id + "': card.include '" + *at + "' names no file; dropped"); at = it.card.include.erase(at); }
                else ++at;
            }
            for (const auto& h : headers)
                if (std::find(it.card.include.begin(), it.card.include.end(), h) == it.card.include.end()) {
                    it.card.include.push_back(h);
                    mends.push_back("item '" + it.id + "': card.include gains the project header '" + h + "'");
                }
        }
    }
    if (plan.harness == "cpp-cmake")
        for (auto& f : plan.scaffold)
            if (endsWith(f.path, ".cpp"))
                if (const std::string fixed = unnameUnusedParams(f.content); fixed != f.content) { f.content = fixed; mends.push_back("scaffold: '" + f.path + "' stub parameters the body does not read were left unnamed"); }
    if (plan.harness == "cpp-cmake") {
        const bool hasMain = std::any_of(plan.scaffold.begin(), plan.scaffold.end(), [](const PlanFile& f) { return f.path == "src/main.cpp"; });
        const bool wantsMain = std::any_of(plan.items.begin(), plan.items.end(), [](const WorkItem& it) { return it.persona == "developer" && std::find(it.files.begin(), it.files.end(), "src/main.cpp") != it.files.end(); });
        if (wantsMain && !hasMain) { plan.scaffold.push_back({"src/main.cpp", "int main() { return 0; }\n"}); mends.push_back("scaffold: stub src/main.cpp added (an empty main)"); }
    }
    return mends;
}

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
        const int reachable = std::max(5, static_cast<int>(it.card.examples.size()) + 2);
        if ((it.persona == "tester" || it.persona == "developer") && it.gate.minTests > reachable)
            problems.push_back("item '" + it.id + "': gate.minTests is " + std::to_string(it.gate.minTests) + " but only " + std::to_string(reachable) + " tests can be expected from " + std::to_string(it.card.examples.size()) + " worked examples; lower it to " + std::to_string(reachable) + " or add examples");
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
