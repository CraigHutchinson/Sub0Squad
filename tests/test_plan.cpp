// The plan and its divisions: validation, plan-2 (personas, cards, gates), tolerant reading, amendment by a division, the completion of a division.
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
    {   // Every plan names a registered harness: one that names none, or an unknown one, is not a plan.
        Plan none = makePlan();
        none.harness.clear();
        CHECK(!Plan::fromJson(none.toJson()).has_value());
        Plan odd = makePlan();
        odd.harness = "mystery";
        CHECK(!Plan::fromJson(odd.toJson()).has_value());
        CHECK(Plan::fromJson(makePlan().toJson()).has_value());
    }
    { // plan validation
        CHECK(Plan::fromJson(makePlan().toJson()).has_value());
        Plan dup = makePlan(); dup.items[2].files = {"src/calc.js"};
        CHECK(!Plan::fromJson(dup.toJson()).has_value());                       // overlapping editable files
        Plan cyc = makePlan(); cyc.items[0].deps = {"b"};
        CHECK(!Plan::fromJson(cyc.toJson()).has_value());                       // cycle
        Plan unk = makePlan(); unk.items[1].deps = {"zzz"};
        CHECK(!Plan::fromJson(unk.toJson()).has_value());                       // unknown dependency
        Plan esc = makePlan(); esc.items[0].files = {"../evil.hpp"};
        CHECK(!Plan::fromJson(esc.toJson()).has_value());                       // path escape
        Plan abs = makePlan(); abs.items[0].files = {"/etc/passwd"};
        CHECK(!Plan::fromJson(abs.toJson()).has_value());
        CHECK(!Plan::fromJson("{\"schema\":\"plan-1\",\"items\":[]}").has_value());
    }
    { // plan-2: the blueprint is valid; personas, cards and gates are enforced
        const std::string bp = blueprintJson("calculator-web");
        CHECK(Plan::fromJson(bp).has_value());
        CHECK(!Plan::fromJson(bp, 50).has_value());                                  // cards over the token budget are rejected
        auto swap = [&](const std::string& from, const std::string& to) { std::string t = bp; auto p = t.find(from); if (p != std::string::npos) t.replace(p, from.size(), to); return t; };
        CHECK(!Plan::fromJson(swap("\"files\": [\"tests/eval.test.mjs\"]", "\"files\": [\"src/x.js\"]")).has_value());          // a tester writes only tests/
        CHECK(!Plan::fromJson(swap("\"files\": [\"src/calc.js\"]", "\"files\": [\"tests/y.test.mjs\"]")).has_value());          // a developer never writes tests
        CHECK(!Plan::fromJson(swap("\"persona\": \"developer\"", "\"persona\": \"wizard\"")).has_value());
        CHECK(!Plan::fromJson(swap("\"kind\": \"node-test\"", "\"kind\": \"shell\"")).has_value());                           // a plan cannot carry commands
        CHECK(!Plan::fromJson(swap("\"harness\": \"web-static\"", "\"harness\": \"mystery\"")).has_value());
        auto plan = Plan::fromJson(bp);
        CHECK(plan && plan->items.size() == 4 && plan->find("dev-engine")->card.include.size() == 1);
        // Triage: the first call.
        const TriageResult web = triage("write a scientific calculator as a web page");
        CHECK(web.blueprint == "calculator-web" && web.architect == "not needed" && web.split && web.projectType == "web-static");
        const TriageResult basic = triage("make a calculator");
        CHECK(basic.blueprint == "calculator-cpp" && basic.split && basic.projectType == "cpp-cmake");   // no platform named: the native one
        CHECK(isKnownTask("calculator-py") && isKnownTask("calculator-cpp") && !isKnownTask("calculator-basic"));   // a blueprint is what a registered harness offers
        const TriageResult unknown = triage("build a unit converter page with length and mass");
        CHECK(unknown.blueprint.empty() && unknown.architect == "deferred" && unknown.projectType == "web-static");
        CHECK(needsPlanPacket("x", unknown, "w-1").find("plan-2 syntax") != std::string::npos);
        // Engine profiles: parse, round trip, loopback only.
        auto e = EngineProfile::parse("q=http://127.0.0.1:8342/v1,model=m,ctx=8192,slots=2");
        CHECK(e && e->model == "m" && e->contextTokens == 8192 && e->maxConcurrent == 2);
        CHECK(e && EngineProfile::parse(e->toArg()).has_value());
        auto seeded = EngineProfile::parse("q=http://127.0.0.1:8342/v1,model=m,slots=1,seed=42,cache=0");  // a deterministic profile
        CHECK(seeded && seeded->seed == 42 && !seeded->cachePrompt && e->seed == -1 && e->cachePrompt);
        auto again = seeded ? EngineProfile::parse(seeded->toArg()) : std::unexpected(std::string{"x"});
        CHECK(again && again->seed == 42 && !again->cachePrompt);
    }
    { // A division on a web project keeps the parent as a coordinator, and refuses parts graded by the parent's own tests.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 1; g.mustFailOnStub = stub; return g; };
        WorkItem tt; tt.id = "tt"; tt.persona = "tester"; tt.files = {"tests/e.test.mjs"}; tt.gate = gate("tests/e.test.mjs", true);
        WorkItem eng; eng.id = "eng"; eng.persona = "developer"; eng.files = {"src/e.js", "src/f.js"}; eng.deps = {"tt"}; eng.complexity = 3; eng.gate = gate("tests/e.test.mjs", false);
        WorkItem after; after.id = "after"; after.persona = "developer"; after.files = {"src/g.js"}; after.deps = {"eng"}; after.gate = gate("tests/g.test.mjs", false);
        WorkItem gt; gt.id = "gt"; gt.persona = "tester"; gt.files = {"tests/g.test.mjs"}; gt.gate = gate("tests/g.test.mjs", true);
        after.deps.push_back("gt");
        base.items = {tt, eng, gt, after};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem ft; ft.id = "t-f"; ft.persona = "tester"; ft.files = {"tests/f.test.mjs"}; ft.gate = gate("tests/f.test.mjs", true);
        WorkItem fe; fe.id = "d-e"; fe.persona = "developer"; fe.files = {"src/e.js"}; fe.gate = gate("tests/e.test.mjs", false);   // graded by the parent's tests
        WorkItem ff; ff.id = "d-f"; ff.persona = "developer"; ff.files = {"src/f.js"}; ff.deps = {"t-f"}; ff.gate = gate("tests/f.test.mjs", false);
        frag.items = {ft, fe, ff};
        CHECK(!Plan::amended(base, "eng", frag).has_value());                                              // d-e is graded by tests/e.test.mjs, the parent's
        frag.items[1].deps = {"t-f"}; frag.items[1].gate = gate("tests/f.test.mjs", false);
        auto merged = Plan::amended(base, "eng", frag);
        CHECK(merged.has_value());
        const WorkItem* coord = merged ? merged->find("eng") : nullptr;
        CHECK(coord && coord->persona == "integrator" && coord->files.empty() && coord->deps.size() == 2);   // the parent stays, waiting for both parts
        const WorkItem* dependent = merged ? merged->find("after") : nullptr;
        CHECK(dependent && std::find(dependent->deps.begin(), dependent->deps.end(), std::string{"eng"}) != dependent->deps.end());   // dependents still wait for it
    }
    { // The division the 9B wrote three times: tests for the lexer, parts for the functions and the parser, no part for src/lexer.js. The harness completes it.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 3; g.mustFailOnStub = stub; return g; };
        WorkItem et; et.id = "eval-tests"; et.persona = "tester"; et.files = {"tests/eval.test.mjs"}; et.gate = gate("tests/eval.test.mjs", true);
        WorkItem eng; eng.id = "engine"; eng.persona = "developer"; eng.files = {"src/calc.js", "src/lexer.js", "src/functions.js"}; eng.deps = {"eval-tests"}; eng.complexity = 3; eng.gate = gate("tests/eval.test.mjs", false);
        base.items = {et, eng};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem lx; lx.id = "lexer"; lx.persona = "tester"; lx.files = {"tests/lexer.test.mjs"}; lx.gate = gate("tests/lexer.test.mjs", true);
        WorkItem fn; fn.id = "functions"; fn.persona = "developer"; fn.files = {"src/functions.js"}; fn.deps = {"lexer"}; fn.gate = gate("tests/lexer.test.mjs", false);
        WorkItem ps; ps.id = "parser"; ps.persona = "developer"; ps.files = {"src/calc.js"}; ps.deps = {"lexer", "functions"}; ps.gate = gate("tests/lexer.test.mjs", false);
        frag.items = {lx, fn, ps};
        CHECK(!Plan::amended(base, "engine", frag).has_value());                       // as written: src/lexer.js is delivered by no part
        const auto added = completeDivision(frag, *base.find("engine"));
        CHECK(added.size() == 1 && added[0].find("src/lexer.js") != std::string::npos);
        const WorkItem* impl = frag.find("dev-lexer");
        CHECK(impl && impl->persona == "developer" && impl->files == std::vector<std::string>{"src/lexer.js"} && impl->deps == std::vector<std::string>{"lexer"} && impl->gate.tests == std::vector<std::string>{"tests/lexer.test.mjs"});
        CHECK(std::find(frag.find("parser")->deps.begin(), frag.find("parser")->deps.end(), "dev-lexer") != frag.find("parser")->deps.end());   // the parser waits for the lexer's implementation
        CHECK(Plan::fromJson(frag.toJson()).has_value());
        CHECK(completeDivision(frag, *base.find("engine")).empty());                  // nothing left to add
    }
    { // The 'ui-js' division of the first real 9B run: a developer part graded by tests/ui-dom.test.mjs, which no item writes. It gets a tester.
        Plan base; base.task = "x"; base.harness = "web-static";
        auto gate = [](const char* t, bool stub) { GateSpec g; g.kind = "node-test"; g.tests = {t}; g.minTests = 3; g.mustFailOnStub = stub; return g; };
        WorkItem ut; ut.id = "ui-tests"; ut.persona = "tester"; ut.files = {"tests/ui.test.mjs"}; ut.gate = gate("tests/ui.test.mjs", true);
        WorkItem js; js.id = "ui-js"; js.persona = "developer"; js.files = {"src/ui.js"}; js.deps = {"ui-tests"}; js.gate = gate("tests/ui.test.mjs", false);
        base.items = {ut, js};
        Plan frag; frag.task = "x"; frag.harness = "web-static";
        WorkItem ev; ev.id = "ui-eval"; ev.persona = "tester"; ev.files = {"tests/ui-eval.test.mjs"}; ev.gate = gate("tests/ui-eval.test.mjs", true);
        WorkItem dom; dom.id = "ui-dom"; dom.persona = "developer"; dom.files = {"src/ui.js"}; dom.deps = {"ui-eval"}; dom.gate = gate("tests/ui-dom.test.mjs", false);
        frag.items = {ev, dom};
        const auto added = completeDivision(frag, *base.find("ui-js"));
        CHECK(added.size() == 1 && added[0].find("tests/ui-dom.test.mjs") != std::string::npos);
        const WorkItem* t = frag.find("test-ui-dom");
        CHECK(t && t->persona == "tester" && t->files == std::vector<std::string>{"tests/ui-dom.test.mjs"} && t->gate.mustFailOnStub);
        CHECK(std::find(frag.find("ui-dom")->deps.begin(), frag.find("ui-dom")->deps.end(), "test-ui-dom") != frag.find("ui-dom")->deps.end());
        CHECK(completeDivision(frag, *base.find("ui-js")).empty());
    }
    { // Tolerant plan reading (what the 9B wrote: notes as one string) and an error that says where a type is wrong.
        const std::string base = "{\"schema\":\"plan-2\",\"task\":\"x\",\"harness\":\"web-static\",\"items\":[{\"id\":\"a\",\"title\":\"A\",\"persona\":\"developer\",\"goal\":\"g\",\"files\":%FILES%,\"deps\":[],\"complexity\":%CX%,"
                                  "\"card\":{\"context\":\"c\",\"contract\":\"k\",\"acceptance\":[\"ok\"],\"notes\":%NOTES%},\"gate\":{\"kind\":\"node-test\",\"tests\":%TESTS%,\"minTests\":%MIN%,\"mustFailOnStub\":%STUB%}}]}";
        const auto make = [&](const char* files, const char* cx, const char* notes, const char* tests, const char* mn, const char* stub) {
            std::string t = base;
            for (const auto& [k2, v] : std::vector<std::pair<std::string, std::string>>{{"%FILES%", files}, {"%CX%", cx}, {"%NOTES%", notes}, {"%TESTS%", tests}, {"%MIN%", mn}, {"%STUB%", stub}})
                t.replace(t.find(k2), k2.size(), v);
            return t;
        };
        CHECK(Plan::fromJson(make("[\"src/a.js\"]", "2", "[\"n\"]", "[\"tests/a.test.mjs\"]", "3", "false")).has_value());                       // the proper form
        const auto loose = Plan::fromJson(make("\"src/a.js\"", "\"2\"", "\"Output only src/a.js\"", "\"tests/a.test.mjs\"", "\"3\"", "\"false\""));   // strings where lists, numbers and booleans are wanted
        CHECK(loose.has_value() && loose->items[0].card.notes.size() == 1 && loose->items[0].files.size() == 1 && loose->items[0].complexity == 2 && loose->items[0].gate.minTests == 3);
        const auto bad = Plan::fromJson(make("{\"x\":1}", "2", "[\"n\"]", "[\"t\"]", "3", "false"));                                      // an object where a list is wanted: not repairable, but it names the field
        CHECK(!bad.has_value() && bad.error().find("item #1") != std::string::npos && bad.error().find("files") != std::string::npos);
    }
    return finish();
}
