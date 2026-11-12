// The calculator on three platforms (docs/PROVING_GROUND.md): web, native C++ and Python share ONE contract. This test pins that down (the plans
// have the same shape), then runs the real toolchains without a model: correct files pass every gate and integrate, each injected defect fails its
// own gate with feedback that names the failing test and the values, and a file that does not compile or parse is reported with its location.
#include "check.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_cpp.hpp"
#include "sub0squad/harness_py.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/sandbox.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static void put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p) << t; }

struct Platform { const char* task; const char* harness; const char* gateCmd; const char* integrateCmd; };

static int agent(const fs::path& d, const std::string& item, int attempt, std::vector<std::string> inject = {})
{
    std::vector<std::string> a = {"--workflow-dir", d.string(), "--item", item, "--tier", "local", "--attempt", std::to_string(attempt), "--delay-ms", "0"};
    for (auto& i : inject) { a.push_back("--inject"); a.push_back(i); }
    return runMockAgent(a);
}
static int gate(const Platform& p, const fs::path& d, const std::string& item, int attempt)
{
    const std::vector<std::string> a = {"--workflow-dir", d.string(), "--item", item, "--attempt", std::to_string(attempt)};
    return std::string(p.harness) == "cpp-cmake" ? runCppGate(a) : runPyGate(a);
}

int main()
{
    const Platform platforms[] = {{"calculator-cpp", "cpp-cmake", "cpp-gate", "cpp-integrate"}, {"calculator-py", "py-unittest", "py-gate", "py-integrate"}};

    // The abstraction: both blueprints are valid and have the same items, personas, dependencies and thresholds.
    std::vector<Plan> plans;
    for (const auto& p : platforms) {
        const Harness* h = harnessWithBlueprint(p.task);
        CHECK(h != nullptr);
        auto plan = h ? Plan::fromJson(h->blueprint(p.task)) : std::unexpected(std::string{"no harness"});
        CHECK(plan.has_value());
        if (plan) plans.push_back(*plan);
    }
    CHECK_EQ(plans.size(), static_cast<std::size_t>(2));
    if (plans.size() == 2) {
        CHECK_EQ(plans[0].items.size(), plans[1].items.size());
        for (std::size_t i = 0; i < plans[0].items.size(); ++i) {
            CHECK_EQ(plans[0].items[i].id, plans[1].items[i].id);
            CHECK_EQ(plans[0].items[i].persona, plans[1].items[i].persona);
            CHECK(plans[0].items[i].deps == plans[1].items[i].deps);
            CHECK_EQ(plans[0].items[i].gate.minTests, plans[1].items[i].gate.minTests);
            CHECK_EQ(plans[0].items[i].gate.mustFailOnStub, plans[1].items[i].gate.mustFailOnStub);
        }
    }
    // Triage routes by wording.
    CHECK(cppClaimsTask("write a native c++ calculator"));
    CHECK(pyClaimsTask("write a python calculator"));
    CHECK(!cppClaimsTask("write a scientific calculator web app"));

    for (const auto& p : platforms) {
        const fs::path d = makeTempDir(p.harness);
        CHECK_EQ(runMockPlanner({"--task", p.task, "--out", (d / "plan.json").string()}), 0);

        // Every defect still matches its template.
        for (const char* item : {"dev-engine", "dev-app"}) {
            const Harness* h = findHarness(p.harness);
            const std::string target = h->mockBugTarget(item);
            CHECK(!h->mockFile(target, true).empty());
            CHECK(h->mockFile(target, true) != h->mockFile(target, false));
        }

        // Testers pass against the stubs-fail rule; the engine with the defect fails its gate and the feedback names the test and the values.
        CHECK_EQ(agent(d, "test-engine", 1), 0);
        CHECK_EQ(gate(p, d, "test-engine", 1), 0);
        {   // The stub must not help a test pass: the reference tests all FAIL against it (a throwing stub let every "must throw" test pass for free: catalogue rows 2 and 33).
            const std::string g = slurp(d / "items" / "test-engine" / "attempt-1" / "gate.txt");
            int tests = -1, failing = -2;
            CHECK(std::sscanf(g.c_str(), "GATE PASS: test-engine attempt 1 (%d tests, %d failing", &tests, &failing) == 2);
            CHECK(tests >= 10 && failing == tests);
        }
        CHECK_EQ(agent(d, "test-app", 1), 0);
        CHECK_EQ(gate(p, d, "test-app", 1), 0);
        CHECK_EQ(agent(d, "dev-engine", 1, {"dev-engine:1"}), 0);
        CHECK(gate(p, d, "dev-engine", 1) != 0);
        const std::string g1 = slurp(d / "items" / "dev-engine" / "attempt-1" / "gate.txt");
        CHECK(g1.find("GATE FAIL") != std::string::npos);
        CHECK(g1.find("modulo") != std::string::npos);
        CHECK(g1.find("expected") != std::string::npos);
        // The same item passes on the next attempt (the correct file).
        CHECK_EQ(agent(d, "dev-engine", 2), 0);
        CHECK_EQ(gate(p, d, "dev-engine", 2), 0);
        CHECK(fs::exists(d / "items" / "dev-engine" / "accepted"));

        // The app with the defect fails; a file that does not compile or parse is named with its location.
        CHECK_EQ(agent(d, "dev-app", 1, {"dev-app:1"}), 0);
        CHECK(gate(p, d, "dev-app", 1) != 0);
        const Harness* h = findHarness(p.harness);
        const std::string appFile = h->mockBugTarget("dev-app");
        put(d / "items" / "dev-app" / "attempt-2" / "out" / appFile, std::string(p.harness) == "cpp-cmake" ? "int main() { return ; }\n" : "def broken(:\n");
        CHECK(gate(p, d, "dev-app", 2) != 0);
        const std::string g2 = slurp(d / "items" / "dev-app" / "attempt-2" / "gate.txt");
        CHECK(g2.find(appFile) != std::string::npos);
        CHECK(g2.find(std::string(p.harness) == "cpp-cmake" ? "does not compile" : "syntax error") != std::string::npos);
        // An interface mismatch (the engine does not provide the name the tests import) exits 6 and lists the names the tests need.
        {
            const std::string engineFile = h->mockBugTarget("dev-engine");
            put(d / "items" / "dev-engine" / "attempt-5" / "out" / engineFile, std::string(p.harness) == "cpp-cmake" ? "int unrelated() { return 1; }\n" : "def other(expr, degrees=False):\n    return 0\n");
            CHECK_EQ(gate(p, d, "dev-engine", 5), 6);
            const std::string g5 = slurp(d / "items" / "dev-engine" / "attempt-5" / "gate.txt");
            CHECK(g5.find("INTERFACE MISMATCH") != std::string::npos);
            if (std::string(p.harness) == "py-unittest") CHECK(g5.find("evaluate") != std::string::npos);
        }
        // A syntax error followed by undeclared-name noise is NOT an interface gap (the first error decides): an ordinary failure, exit 1.
        if (std::string(p.harness) == "cpp-cmake") {
            put(d / "items" / "dev-engine" / "attempt-6" / "out" / h->mockBugTarget("dev-engine"), "double broken() { throw std::runtime_error(\"unterminated); }\nint x = nothing_declared;\n");
            CHECK_EQ(gate(p, d, "dev-engine", 6), 1);
        }
        // An edit outside the allowlist is refused.
        put(d / "items" / "dev-app" / "attempt-3" / "out" / appFile, h->mockFile(appFile, false));
        put(d / "items" / "dev-app" / "attempt-3" / "out" / "stray.txt", "x");
        CHECK(gate(p, d, "dev-app", 3) != 0);
        CHECK(slurp(d / "items" / "dev-app" / "attempt-3" / "gate.txt").find("outside the item's editable files") != std::string::npos);

        // The correct app passes, and the accepted items integrate into a working program with a capture.
        CHECK_EQ(agent(d, "dev-app", 4), 0);
        CHECK_EQ(gate(p, d, "dev-app", 4), 0);
        const int integ = std::string(p.harness) == "cpp-cmake" ? runCppIntegrate({"--workflow-dir", d.string()}) : runPyIntegrate({"--workflow-dir", d.string()});
        CHECK_EQ(integ, 0);
        CHECK(slurp(d / "capture.txt").find("= 512") != std::string::npos);
        CHECK(fs::exists(d / "report.md"));
        CHECK(slurp(d / "deliverables.json").find("screen-capture") != std::string::npos);
    }

    // The same rule on the web platform: the real failure of run w-000002 (a test imports a name the module does not export).
    {
        const fs::path d = makeTempDir("platform-web");
        CHECK_EQ(runMockPlanner({"--task", "calculator-web", "--out", (d / "plan.json").string()}), 0);
        CHECK_EQ(agent(d, "test-engine", 1), 0);
        CHECK_EQ(runHarnessGate({"--workflow-dir", d.string(), "--item", "test-engine", "--attempt", "1"}), 0);
        put(d / "items" / "dev-engine" / "attempt-1" / "out" / "src" / "calc.js", "export function calculate(expr) { return 0; }\n");
        CHECK_EQ(runHarnessGate({"--workflow-dir", d.string(), "--item", "dev-engine", "--attempt", "1"}), 6);
        const std::string g = slurp(d / "items" / "dev-engine" / "attempt-1" / "gate.txt");
        CHECK(g.find("INTERFACE MISMATCH") != std::string::npos);
        CHECK(g.find("evaluate") != std::string::npos);
    }
    return 0;
}
