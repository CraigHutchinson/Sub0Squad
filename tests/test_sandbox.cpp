// The sandbox commands without a daemon: the templates really compile, every injected defect really fails its own gate, the gate
// enforces the editable-file allowlist, and the accepted items integrate into a working calculator.
#include "check.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/sandbox.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

static int agent(const fs::path& d, const std::string& item, int attempt, const std::string& tier, std::vector<std::string> inject = {})
{
    std::vector<std::string> a = {"--workflow-dir", d.string(), "--item", item, "--tier", tier, "--attempt", std::to_string(attempt), "--delay-ms", "0"};
    for (auto& i : inject) { a.push_back("--inject"); a.push_back(i); }
    return runMockAgent(a);
}
static int gate(const fs::path& d, const std::string& item, int attempt)
{
    return runSandboxGate({"--workflow-dir", d.string(), "--item", item, "--attempt", std::to_string(attempt)});
}

int main()
{
    // Every defect still matches its template (calculatorFile returns empty when it does not).
    for (const char* f : {"tokenizer.hpp", "parser.hpp", "functions.hpp", "evaluator.hpp", "cli.hpp", "calc_tests.cpp"}) {
        CHECK(!calculatorFile(f, true).empty());
        CHECK(calculatorFile(f, true) != calculatorFile(f, false));
    }

    const fs::path d = makeTempDir("sandbox");
    CHECK_EQ(runMockPlanner({"--task", "nonsense", "--out", (d / "x.json").string()}), 3);
    CHECK_EQ(runMockPlanner({"--task", "calculator-scientific", "--out", (d / "plan.json").string()}), 0);
    auto plan = Plan::fromJson(slurp(d / "plan.json"));
    CHECK(plan.has_value());
    CHECK(plan && plan->items.size() == 6);
    const fs::path one = makeTempDir("sandbox1");
    CHECK_EQ(runMockPlanner({"--task", "calculator-basic", "--out", (one / "plan.json").string()}), 0);
    auto basic = Plan::fromJson(slurp(one / "plan.json"));
    CHECK(basic && basic->items.size() == 1);

    // Good path in dependency order (items are listed with dependencies first apart from functions/tokenizer which are independent).
    for (const char* id : {"tokenizer", "functions", "parser", "evaluator", "cli", "tests"}) {
        CHECK_EQ(agent(d, id, 1, "local"), 0);
        CHECK_EQ(gate(d, id, 1), 0);
    }
    CHECK_EQ(runSandboxIntegrate({"--workflow-dir", d.string()}), 0);
    CHECK(fs::exists(d / "project" / "calc"));

    // Each injected defect fails its own gate (a real compiler or test run says so), and the frontier-stub tier is never broken.
    for (const char* id : {"tokenizer", "parser", "functions", "evaluator", "cli", "tests"}) {
        CHECK_EQ(agent(d, id, 2, "local", {id}), 0);
        CHECK(gate(d, id, 2) != 0);
        CHECK_EQ(agent(d, id, 3, "frontier-stub", {id}), 0);
        CHECK_EQ(gate(d, id, 3), 0);
    }
    // Attempt-limited injection: "tokenizer:1" breaks attempt 1 only.
    CHECK_EQ(agent(d, "tokenizer", 4, "local", {"tokenizer:1"}), 0);
    CHECK_EQ(gate(d, "tokenizer", 4), 0);

    // The allowlist: a file outside the item's editable set fails the gate even if everything compiles.
    CHECK_EQ(agent(d, "tokenizer", 5, "local"), 0);
    { std::ofstream o(d / "items" / "tokenizer" / "attempt-5" / "out" / "sneaky.hpp"); o << "#pragma once\n"; }
    CHECK(gate(d, "tokenizer", 5) != 0);

    // The single-item plan: everything in one item, defect in the parser.
    CHECK_EQ(agent(one, "calculator", 1, "local", {"calculator"}), 0);
    CHECK(gate(one, "calculator", 1) != 0);
    CHECK_EQ(agent(one, "calculator", 2, "frontier-stub", {"calculator"}), 0);
    CHECK_EQ(gate(one, "calculator", 2), 0);
    CHECK_EQ(runSandboxIntegrate({"--workflow-dir", one.string()}), 0);

    // Integration refuses an item that was never accepted.
    fs::remove_all(d / "items" / "cli" / "accepted");
    CHECK(runSandboxIntegrate({"--workflow-dir", d.string()}) != 0);
    fs::remove_all(d);
    fs::remove_all(one);
    return finish();
}
