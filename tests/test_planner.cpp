// The planner role (docs/CLEAN_SLATE.md): a model plans from the request alone, with no blueprint. A fake engine plays the model (recorded replies); the real
// plan validator, the mechanical plan checks, the harness's scaffold compile check and the real C++ gate judge the result. The fixture is a project the
// harness has never heard of (a word counter): the plan carries its own interface and stubs, so nothing calculator-shaped is in the workspace.
#include "check.hpp"
#include "fake_engine.hpp"
#include "sub0squad/harness_cpp.hpp"
#include "sub0squad/harness_registry.hpp"
#include "sub0squad/plan.hpp"
#include "sub0squad/plan_checks.hpp"
#include "sub0squad/plan_lint.hpp"
#include "sub0squad/planner.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static void put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
static std::string data(const char* name) { return slurp(fs::path{SUB0SQUAD_SOURCE_DIR} / "tests" / "data" / "plans" / name); }

static std::vector<std::string> plannerArgs(const fs::path& d, const fake::Engine& e, int turns = 4)
{
    return {"--workflow-dir", d.string(), "--engine", e.profile(), "--request", "write a word counting library in C++", "--max-turns", std::to_string(turns), "--timeout", "30"};
}

int main()
{
    registerBuiltinHarnesses();
    const std::string good = data("planner-wordcount.json");
    CHECK(!good.empty());

    {   // A good plan passes every check, including the compile check of its own stub.
        auto plan = Plan::fromJson(good);
        CHECK(plan.has_value());
        if (plan) {
            CHECK(checkPlan(*plan).empty());
            CHECK(cppCheckPlanScaffold(plan->toJson()).empty());
            CHECK_EQ(plan->scaffold.size(), static_cast<std::size_t>(2));
            auto again = Plan::fromJson(plan->toJson());   // the scaffold and the examples survive a round trip
            CHECK(again.has_value() && again->scaffold.size() == 2 && again->items[0].card.examples.size() == 4);
        }
    }
    {   // The checks name what to change: no examples, no stub for a developer's file, no tester dependency, a stub that does not compile.
        auto plan = Plan::fromJson(good);
        CHECK(plan.has_value());
        if (plan) {
            Plan p = *plan;
            p.items[0].card.examples.clear();
            const auto problems = checkPlan(p);
            CHECK(!problems.empty() && problems[0].find("worked examples") != std::string::npos);
            Plan q = *plan;
            q.scaffold.pop_back();   // the stub of src/wc.cpp
            const auto noStub = checkPlan(q);
            CHECK(!noStub.empty() && noStub[0].find("has no stub in the scaffold") != std::string::npos);
            Plan r = *plan;
            r.items[1].deps.clear();
            bool sawDeps = false;
            for (const auto& s : checkPlan(r)) sawDeps = sawDeps || s.find("depends on no tester item") != std::string::npos;
            CHECK(sawDeps);
            Plan broken = *plan;
            broken.scaffold[1].content = "#include \"wc/wc.hpp\"\nnamespace wc { std::size_t countWords(const std::string& t) { return 0 } }\n";
            const std::string bad = cppCheckPlanScaffold(broken.toJson());
            CHECK(bad.find("does not compile") != std::string::npos && bad.find("src/wc.cpp") != std::string::npos);
            // A "stub" that is the real program makes the tests pass against it: refused by length.
            Plan real = *plan;
            for (int i = 0; i < 40; ++i) real.scaffold[1].content += "// line\n";
            bool sawLong = false;
            for (const auto& s : checkPlan(real)) sawLong = sawLong || s.find("longer than 30 lines") != std::string::npos;
            CHECK(sawLong);
            // The plan's account of the harness names no project's files (the calculator's brief does).
            CHECK(cppOwnScaffoldBrief().find("calc") == std::string::npos && cppOwnScaffoldBrief().find("helpers/app.hpp") != std::string::npos);
            // A plan may not supply the harness's instruments or a tester's files.
            Plan hijack = *plan;
            hijack.scaffold.push_back({"CMakeLists.txt", "x"});
            CHECK(!hijack.validate().has_value());
        }
    }
    {   // Slot fill (plan-lint): the scalar for "does the plan keep the slots busy when item sizes are unknown". Deterministic (a fixed-seed Monte Carlo).
        const auto item = [](const std::string& id, std::vector<std::string> deps) {
            WorkItem w; w.id = id; w.persona = "developer"; w.complexity = 3; w.deps = std::move(deps); w.files = {"src/" + id + ".cpp"};
            return w;
        };
        Plan chain;   // six items in a line: one slot works, the others never can
        for (int i = 0; i < 6; ++i) chain.items.push_back(item("dev-c" + std::to_string(i), i ? std::vector<std::string>{"dev-c" + std::to_string(i - 1)} : std::vector<std::string>{}));
        Plan wide;    // twelve independent items: always something to take
        for (int i = 0; i < 12; ++i) wide.items.push_back(item("dev-w" + std::to_string(i), {}));
        Plan exact;   // four independent items on four slots: no spare work, so the slot that finishes first idles while the biggest runs on
        for (int i = 0; i < 4; ++i) exact.items.push_back(item("dev-e" + std::to_string(i), {}));
        const PlanLint lc = lintPlan(chain, 5.0, 4), lw = lintPlan(wide, 5.0, 4), le = lintPlan(exact, 5.0, 4), lw2 = lintPlan(wide, 5.0, 4);
        CHECK(lc.fill > 0.2 && lc.fill < 0.3);                       // 1 of 4 slots
        CHECK(lw.fill > 0.8);                                         // over-allocated: backfilled until the tail
        CHECK(le.fill < lw.fill);                                     // allocating exactly one item per slot under-uses them when sizes differ
        CHECK(lw.backfillRate > 0.6 && lc.backfillRate < 0.01);
        CHECK(lw.fill == lw2.fill && lw.fillP10 == lw2.fillP10);      // deterministic
        CHECK(lw.fillP10 <= lw.fill && lw.expectedSpeedup <= 4.0 + 1e-9 && lc.boundSpeedup < 1.01 && lw.widthRatio == 3.0);
        bool warned = false;
        for (const auto& w : lc.warnings) warned = warned || w.find("keep them") != std::string::npos;
        CHECK(warned && lw.warnings.empty());
        CHECK(lintPlan(chain, 5.0).slots == 0);                       // off unless asked
    }
    {   // The planner repairs: the first reply (fenced, with chatter) has no worked examples; the problem list reaches the model; the second is accepted.
        const fs::path d = makeTempDir("planner-repair");
        auto draft = Plan::fromJson(good);
        CHECK(draft.has_value());
        if (draft) for (auto& it : draft->items) it.card.examples.clear();
        const std::string firstDraft = "Here is the plan:\n```json\n" + (draft ? draft->toJson() : std::string{}) + "\n```\n";
        CHECK(firstDraft != good);
        fake::Engine engine({{firstDraft}, {good}});
        CHECK_EQ(runLlmPlanner(plannerArgs(d, engine)), 0);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(2));
        const auto rq = engine.requests();
        if (rq.size() == 2) {
            CHECK(rq[0].messages.front().second.find("PLANNER") != std::string::npos && rq[0].last().find("write a word counting library in C++") != std::string::npos);
            CHECK(rq[1].last().find("The plan was refused") != std::string::npos);
        }
        auto written = Plan::fromJson(slurp(d / "plan.json"));
        CHECK(written.has_value() && written->project == "wordcount");
        const std::string record = slurp(d / "planner.json");
        CHECK(record.find("\"valid\":true") != std::string::npos && record.find("\"turns\":2") != std::string::npos);
        CHECK(fs::exists(d / "planner" / "turn-1.txt") && fs::exists(d / "planner" / "conversation.md"));
    }
    {   // The scaffold as plain FILE blocks after the JSON (no escaping to get wrong): the same plan results, and the blocks fill in what the JSON leaves out.
        const fs::path d = makeTempDir("planner-blocks");
        auto base = Plan::fromJson(good);
        CHECK(base.has_value());
        std::string reply;
        if (base) {
            Plan bare = *base;
            bare.scaffold.clear();
            reply = bare.toJson() + "\n";
            for (const auto& f : base->scaffold) reply += "=== FILE: " + f.path + " ===\n" + f.content + "=== END FILE ===\n";
        }
        fake::Engine engine({{reply}});
        CHECK_EQ(runLlmPlanner(plannerArgs(d, engine)), 0);
        auto written = Plan::fromJson(slurp(d / "plan.json"));
        CHECK(written.has_value() && written->scaffold.size() == 2 && written->scaffold[0].content.find("countWords") != std::string::npos);
        std::string rest;
        const auto blocks = extractFileBlocks(reply, rest);
        CHECK(blocks.size() == 2 && rest.find("=== FILE") == std::string::npos && rest.find("\"schema\"") != std::string::npos);
        CHECK(slurp(d / "planner.json").find("slot fill") != std::string::npos);   // the plan-lint scalars are kept with the record
    }
    {   // A long refused reply is not put back into the conversation (no stand-in the model could copy: it did, in the first real probe): the request is
        // asked again with the reasons, in a conversation of a system and a user message only.
        const fs::path d = makeTempDir("planner-long");
        fake::Engine engine({{std::string(3500, 'x')}, {good}});
        CHECK_EQ(runLlmPlanner(plannerArgs(d, engine)), 0);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            bool assistant = false;
            for (const auto& m : rq[1].messages) assistant = assistant || m.first == "assistant";
            CHECK(!assistant && rq[1].messages.size() == 2);
            CHECK(rq[1].last().find("write a word counting library") != std::string::npos && rq[1].last().find("no JSON object") != std::string::npos);
        }
    }
    {   // A planner that never produces a valid plan leaves no plan behind and says so.
        const fs::path d = makeTempDir("planner-fail");
        fake::Engine engine({{"I cannot do that."}, {"{\"schema\":\"plan-2\"}"}});
        CHECK_EQ(runLlmPlanner(plannerArgs(d, engine, 2)), 1);
        CHECK(!fs::exists(d / "plan.json"));
        CHECK(slurp(d / "planner.json").find("\"valid\":false") != std::string::npos);
    }
    {   // A reply cut off at the token limit is not parsed as a plan: the planner is told to make it smaller.
        const fs::path d = makeTempDir("planner-cut");
        fake::Engine engine({{good.substr(0, good.size() / 2), "length"}, {good}});
        CHECK_EQ(runLlmPlanner(plannerArgs(d, engine)), 0);
        const auto rq = engine.requests();
        CHECK(rq.size() == 2 && rq[1].last().find("cut off at the token limit") != std::string::npos);
    }
    {   // The plan's own scaffold drives the real gate: the tester's tests fail against the plan's stub (as a tester's must), the developer's file passes them,
        // and the calculator-shaped files of the harness are not in the workspace (no src/calc.cpp, no include/calc).
        const fs::path d = makeTempDir("planner-gate");
        put(d / "plan.json", good);
        put(d / "items" / "test-wc" / "attempt-1" / "out" / "tests" / "wc.test.cpp", data("planner-wordcount.test.cpp"));
        CHECK_EQ(runCppGate({"--workflow-dir", d.string(), "--item", "test-wc", "--attempt", "1"}), 0);
        const fs::path ws = d / "items" / "test-wc" / "attempt-1" / "ws";
        CHECK(fs::exists(ws / "include" / "wc" / "wc.hpp") && !fs::exists(ws / "src" / "calc.cpp") && !fs::exists(ws / "include" / "calc"));
        CHECK(fs::exists(ws / "CMakeLists.txt") && fs::exists(ws / "tests" / "helpers" / "check.hpp"));
        put(d / "items" / "dev-wc" / "attempt-1" / "out" / "src" / "wc.cpp", data("planner-wordcount.impl.cpp"));
        CHECK_EQ(runCppGate({"--workflow-dir", d.string(), "--item", "dev-wc", "--attempt", "1"}), 0);
        // And a wrong implementation is refused with each side's own text and value (no expected/actual).
        put(d / "items" / "dev-wc" / "attempt-2" / "out" / "src" / "wc.cpp", "#include \"wc/wc.hpp\"\nnamespace wc { std::size_t countWords(const std::string& t) { return t.empty() ? 0 : 1; } }\n");
        CHECK(runCppGate({"--workflow-dir", d.string(), "--item", "dev-wc", "--attempt", "2", "--no-publish", "1"}) != 0);
        const std::string said = slurp(d / "items" / "dev-wc" / "attempt-2" / "gate.txt");
        CHECK(said.find("wc::countWords(\"hello world\") is 1 but") != std::string::npos && said.find("expected:") == std::string::npos);
    }
    return finish();
}
