// The model-free proving grounds of docs/BLOCK_PROVING_GROUNDS.md, as tests.
//   block 1 (reply formats): every recorded-reply case under tests/data/replies gives its intended result, and each turn-control rule is pinned on text;
//   block 4 (folded views): the outline of one function, the section view, the path-only read;
//   block 5 (canvas transactions): 1,000 random operation sequences equal an independent model after every step;
//   block 6 (parallel sections): the width rule, the slot-model arithmetic, and the fork harness on a slot-model engine (parity, conflicts, lost edits).
// argv[2] is the fixture directory.
#include "check.hpp"
#include "sub0squad/blocks.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/throughput.hpp"
#include "sub0squad/turn_control.hpp"

#include <cmath>

using namespace sub0squad;

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: test_blocks <sub0squad> <fixture dir>\n"); return 2; }

    // ---- block 1: recorded replies ----
    {
        const FixtureReport r = runReplyFixtures(argv[2]);
        CHECK(r.cases >= 24);                       // every reply form and every refusal has a case
        for (const auto& p : r.problems) { std::fprintf(stderr, "reply case: %s\n", p.c_str()); CHECK(false); }
        ReplyCase c;
        std::string err;
        CHECK(!parseReplyCase("# name: x\n--- reply\nhello\n", c, err) && err.find("states no outcome") != std::string::npos);   // a case must say what it expects
        // The runner itself can fail: a case whose intended text is wrong is reported, with the line.
        CHECK(parseReplyCase("# name: x\n--- base a.py\nx = 1\n--- reply\n=== EDIT: a.py LINES 1 ===\nx = 2\n=== END EDIT ===\n--- want a.py\nx = 3\n", c, err));
        CHECK(runReplyCase(c).find("differs from the intended text at line 1") != std::string::npos);
    }
    // ---- block 1: turn control on text (rows 34 to 36) ----
    {
        // trimOverlap: the longest suffix that is a prefix, at least 8 characters, and never indentation alone.
        CHECK_EQ(trimOverlap("throw Error(\"sqrt requires", "sqrt requires 1 argument\");"), std::string(" 1 argument\");"));
        CHECK_EQ(trimOverlap("abc", "abcdef"), std::string("abcdef"));                              // 3 characters: too short to be evidence
        CHECK_EQ(trimOverlap("    x = 1\n        ", "        y = 2\n"), std::string("        y = 2\n"));   // eight spaces are not a repeat
        CHECK_EQ(trimOverlap("return value;", "nothing in common"), std::string("nothing in common"));
        CHECK_EQ(trimOverlap("", "text"), std::string("text"));
        // continuationText: a fence and a repeated header are not content.
        CHECK_EQ(continuationText("int a = 1;\nint b", "```\n = 2;\nint c = 3;\n```\n"), std::string(" = 2;\nint c = 3;\n"));
        CHECK_EQ(continuationText("=== FILE: a.cpp ===\nint a;\n", "=== FILE: a.cpp ===\nint b;\n"), std::string("int b;\n"));
        CHECK(cutOffInsideEdit("=== EDIT: a.cpp LINES 3 ===\nint x"));
        CHECK(!cutOffInsideEdit("=== EDIT: a.cpp LINES 3 ===\nint x;\n=== END EDIT ===\n"));
        CHECK(!cutOffInsideEdit("=== FILE: a.cpp ===\nint x"));

        // detectRunaway: a long SEARCH block, a repeated line, a restated draft, an echoed view; a normal reply is left alone.
        std::string draft, search = "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n";
        for (int i = 1; i <= 80; ++i) draft += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        for (int i = 1; i <= 40; ++i) search += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        CHECK(!detectRunaway(search, {{"a.cpp", draft}}));                                         // 40 lines: at the limit
        CHECK_EQ(detectRunaway(search + "int v41 = 41;\n", {{"a.cpp", draft}}).kind, std::string("long-search"));
        CHECK(!detectRunaway(search + "int v41 = 41", {{"a.cpp", draft}}));                        // the 41st line is not complete yet: no verdict on a partial line
        CHECK(detectRunaway(search + "int v41 = 41;\n", {{"a.cpp", draft}}).instruction.find("LINES a-b") != std::string::npos);
        CHECK(!detectRunaway(search + "=======\nint v1 = 0;\n>>>>>>> REPLACE\n" + search, {{"a.cpp", draft}}));   // two blocks of 40 are two blocks
        std::string repeat = "=== EDIT: a.cpp AFTER 2 ===\n";
        for (int i = 0; i < 9; ++i) repeat += "    total += value;\n";
        CHECK(!detectRunaway(repeat, {}));
        CHECK_EQ(detectRunaway(repeat + "    total += value;\n", {}).kind, std::string("repeat"));
        std::string braces = "=== FILE: a.cpp ===\n";
        for (int i = 0; i < 40; ++i) braces += "}\n";
        CHECK(!detectRunaway(braces, {}));                                                          // short lines (a brace) repeat legitimately
        std::string cycle = "=== FILE: a.cpp ===\n";
        for (int i = 0; i < 6; ++i) cycle += "if (x) {\n    doTheThing();\n}\n";
        CHECK_EQ(detectRunaway(cycle, {}).kind, std::string("repeat"));                             // a 3-line block over and over
        CHECK_EQ(detectRunaway("=== FILE: a.cpp ===\n" + draft.substr(0, draft.find("int v31")), {{"a.cpp", draft}}).kind, std::string("restate"));
        CHECK(!detectRunaway("=== FILE: a.cpp ===\n" + draft.substr(0, draft.find("int v31")), {}));                    // no draft: a first whole file is not a restatement
        CHECK(!detectRunaway("=== FILE: b.cpp ===\n" + draft.substr(0, draft.find("int v31")), {{"a.cpp", draft}}));    // another file
        std::string rewritten = "=== FILE: a.cpp ===\n";
        for (int i = 1; i <= 40; ++i) rewritten += "long w" + std::to_string(i) + " = 0;\n";
        CHECK(!detectRunaway(rewritten, {{"a.cpp", draft}}));                                       // a genuine rewrite is allowed
        std::string echo = "Here is the file:\n";
        for (int i = 1; i <= 30; ++i) echo += "  " + std::to_string(i) + "| int v" + std::to_string(i) + ";\n";
        CHECK_EQ(detectRunaway(echo, {}).kind, std::string("view-echo"));
        CHECK(!detectRunaway("=== EDIT: a.cpp LINES 5 ===\nint v5 = 50;\n=== END EDIT ===\n", {{"a.cpp", draft}}));
        RunawayLimits off;
        off.maxSearchLines = 0;
        CHECK(!detectRunaway(search + "int v41 = 41;\nint v42 = 42;\n", {}, off));                  // a limit of 0 turns its rule off
    }
    // ---- block 4: outline, section view, path-only read ----
    {
        const std::string py = "import math\n\n\ndef area(w, h):\n    a = w * h\n    return a\n\n\ndef one(x):\n    return x\n\n\nclass P:\n    def long(self):\n        a = 1\n        b = 2\n        return a + b\n";
        CHECK_EQ(foldRegions(py, true).size(), static_cast<std::size_t>(2));          // worth folding: area and long
        CHECK_EQ(foldRegions(py, true, true).size(), static_cast<std::size_t>(3));    // by name: the one-line function too
        const std::string view = sectionView(py, true, {"area"});
        CHECK(view.find("a = w * h") != std::string::npos);                            // the owner's function is open
        CHECK(view.find("b = 2") == std::string::npos && view.find("lines folded: 15-17; long") != std::string::npos);
        const std::string outline = answerRead(ReadRequest{"m.py", 0, 0, {}}, py, true);
        CHECK(outline.find("outline (17 lines") != std::string::npos && outline.find("a = w * h") == std::string::npos && outline.find("def area(w, h):") != std::string::npos);
        const auto reads = parseReadRequests("=== READ: m.py ===\n=== READ: m.py SYMBOL one ===\n=== READ: m.py WHATEVER ===\n");
        CHECK_EQ(reads.size(), static_cast<std::size_t>(2));
        CHECK(answerRead(reads[1], py, true).find("return x") != std::string::npos);   // a one-line function can be read by name
        const std::string cpp = "int tiny() { return 1; }\n\nint two()\n{\n    return 2;\n}\n";
        Canvas cv;
        cv.addFile("a.cpp", cpp);
        CHECK(cv.apply({"a.cpp", 1, "t", {{CanvasOp::Kind::replaceSymbol, 0, 0, "tiny", "int tiny() { return 10; }"}}}).ok);
        CHECK(cv.apply({"a.cpp", 2, "t", {{CanvasOp::Kind::replaceSymbol, 0, 0, "two", "int two() { return 20; }"}}}).ok);
        CHECK_EQ(cv.text("a.cpp"), std::string("int tiny() { return 10; }\n\nint two() { return 20; }\n"));
        CHECK(cv.logJson().find("\"a.cpp\":3") != std::string::npos);
    }
    // ---- the skeleton is the harness's job (row 46): `@@ signature` lines become stubbed functions, per language ----
    {
        const Skeleton py = expandSkeleton("import math\n\nclass P:\n    def __init__(self):\n        self.n = 0\n\n    @@ def term(self, x)\n\n@@ def evaluate(expr):\n", "calc/engine.py");
        CHECK(py.error.empty() && py.sections.size() == 2 && py.sections[0].name == "term" && py.sections[1].name == "evaluate");
        CHECK(py.text.find("    def term(self, x):\n        raise NotImplementedError(\"not implemented: term\")\n") != std::string::npos);
        CHECK(py.text.find("def evaluate(expr):\n    raise NotImplementedError(\"not implemented: evaluate\")\n") != std::string::npos && py.text.find("@@") == std::string::npos);
        CHECK_EQ(foldRegions(py.text, true, true).size(), static_cast<std::size_t>(3));                // every stub is a function that can be replaced by name
        const Skeleton cpp = expandSkeleton("#include <cmath>\n\nnamespace n {\n\nclass P\n{\n    @@ double term();\n};\n\n@@ static double sq(double x)\n@@ double area(double r) {\n\n}\n", "src/a.cpp");
        CHECK(cpp.error.empty() && cpp.sections.size() == 3);
        CHECK(cpp.text.find("#include <cmath>\n#include <stdexcept>\n") != std::string::npos);     // what the placeholders need
        CHECK(cpp.text.find("static double sq(double x);\ndouble area(double r);\n\nstatic double sq(double x)\n{\n    throw std::logic_error(\"not implemented: sq\");\n}\n") != std::string::npos);   // prototypes first: sections may call each other in any order
        CHECK(cpp.text.find("    double term()\n    {\n        throw std::logic_error(\"not implemented: term\");\n    }\n") != std::string::npos);
        CHECK(cpp.text.find("double term();") == std::string::npos);                                 // a method needs no prototype
        const Skeleton js = expandSkeleton("@@ export function evaluate(expr) {\n", "src/calc.js");
        CHECK_EQ(js.text, std::string("export function evaluate(expr) {\n    throw new Error('not implemented: evaluate');\n}\n"));
        CHECK(expandSkeleton("x = 1\n@@ compute the total\n", "a.py").error.find("line 2 is not a function signature") != std::string::npos);
        CHECK(expandSkeleton("@@ def f(a)\n@@ def f(b)\n", "a.py").error.find("declared twice") != std::string::npos);
        CHECK(expandSkeleton("x = 1\n", "a.py").sections.empty() && expandSkeleton("x = 1\n", "a.py").text == "x = 1\n");
    }
    // ---- block 5: the canvas against a model ----
    {
        const ModelCheck m = canvasModelCheck(1000, 20261112u);
        if (m.failures) std::fprintf(stderr, "canvas model: %s\n", m.firstFailure.c_str());
        CHECK_EQ(m.cases, 1000);
        CHECK_EQ(m.failures, 0);
        CHECK(m.steps > 3000);
        CHECK_EQ(canvasModelCheck(200, 7u).failures, 0);   // another seed
        // Two insertions after the same line keep the order they were given in (the applier's sort is stable).
        Canvas cv;
        cv.addFile("f", "a\nb\n");
        CHECK(cv.apply({"f", 1, "t", {{CanvasOp::Kind::insertAfter, 1, 0, "", "first"}, {CanvasOp::Kind::insertAfter, 1, 0, "", "second"}}}).ok);
        CHECK_EQ(cv.text("f"), std::string("a\nfirst\nsecond\nb\n"));
        CHECK(cv.replaceText("f", "z\n", "t").ok && cv.revision("f") == 3 && cv.textAt("f", 2) == "a\nfirst\nsecond\nb\n");
    }
    // ---- block 6: throughput by default ----
    {
        // The rule: as wide as the idle slots and the sections allow; one at a time needs a stated reason; memory pressure sheds forks first.
        CHECK_EQ(planParallel(9, 4, 0, "", -1, 0).parallel, 4);
        CHECK_EQ(planParallel(3, 4, 0, "", -1, 0).parallel, 3);
        CHECK_EQ(planParallel(9, 0, 0, "", -1, 0).parallel, 1);                         // no idle slot: still one, never zero
        CHECK_EQ(planParallel(9, 4, 2, "", -1, 0).parallel, 2);                         // an experiment fixes the width
        const ParallelPlan seq = planParallel(9, 4, 0, "each section needs the one before", -1, 0);
        CHECK(seq.parallel == 1 && seq.reason.find("each section needs the one before") != std::string::npos);
        const ParallelPlan shed = planParallel(9, 4, 0, "", 900, 2000);
        CHECK(shed.parallel == 1 && shed.shed && shed.reason.find("forks are shed first") != std::string::npos);
        CHECK(!planParallel(9, 4, 0, "", 4000, 2000).shed && planParallel(9, 4, 0, "", 4000, 2000).parallel == 4);

        // The slot model: four equal sections of 100 tokens at 10 tokens/s alone, aggregate 2.8x at four sequences, no prefill.
        SlotModel model;
        model.prefillTps = 0;
        const std::vector<SectionJob> four(4, SectionJob{"s", 0, 0, 100});
        CHECK(near(model.perSequenceTps(1), 10.0) && near(model.perSequenceTps(4), 7.0) && near(model.perSequenceTps(9), 2.8 * 10 / 9));
        CHECK(near(simulate(model, four, 1).wallSec, 40.0));
        const ThroughputRun par = simulate(model, four, 4);
        CHECK(near(par.wallSec, 100.0 / 7.0) && near(par.idleShare, 0.0) && near(par.aggregateTps, 28.0));
        CHECK(near(simulate(model, four, 99).wallSec, par.wallSec));                    // never wider than the engine's slots
        // Unequal sections: when the short ones finish, the long one speeds up (fewer sequences share the step).
        const ThroughputRun mixed = simulate(model, {{"long", 0, 0, 170}, {"short", 0, 0, 85}}, 2);
        CHECK(near(mixed.spans[1].end, 10.0) && near(mixed.spans[0].end, 10.0 + 85.0 / 10.0));
        // The speed-up counts the coordination: the skeleton and the integration are sequential either way.
        const ParallelMetrics m4 = parallelMetrics(model, {"skeleton", 0, 0, 100}, four, {"integration", 0, 0, 0}, 4);
        CHECK(near(m4.speedUp, (10.0 + 40.0) / (10.0 + 100.0 / 7.0)) && near(m4.coordinationShare, 0.2));
        CHECK(near(parallelMetrics(model, {"skeleton", 0, 0, 100}, four, {"integration", 0, 0, 0}, 1).speedUp, 1.0));
        // Idle accounting over measured spans: two slots, three jobs, the second slot idle from 4 to 6 while the third had not started.
        const ThroughputRun idle = accountSpans({{"a", 0, 10}, {"b", 0, 4}, {"c", 6, 9}}, 2, 300);
        CHECK(near(idle.idleSlotSec, 2.0) && near(idle.idleShare, 2.0 / 12.0) && near(idle.wallSec, 10.0) && near(idle.aggregateTps, 30.0));

        // The harness itself, model-free: the same file from the same skeleton one section at a time and four at once, through the real gate.
        const ParallelProof p = parallelSectionsProof(makeTempDir("block6"), 4);
        if (!p.error.empty()) std::fprintf(stderr, "block 6: %s\n", p.error.c_str());
        CHECK(p.ran && p.sections >= 8 && p.width == 4);
        CHECK(p.sequentialPassed && p.parallelPassed);
        CHECK(p.identical);                                                             // parity: same output
        CHECK_EQ(p.conflicts, 0);
        CHECK_EQ(p.lostEdits, 0);
        CHECK(p.model.speedUp > 1.0 && p.model.speedUp < 2.8);                          // arithmetic on the placeholder curve, bounded by its aggregate scale
        CHECK(p.proven());
    }
    // ---- the report ----
    {
        BlockOptions o;
        o.fixtures = argv[2];
        o.modelCases = 100;
        const auto states = runBlocks(o);
        CHECK_EQ(states.size(), static_cast<std::size_t>(13));
        for (const auto& s : states) {
            if (s.id == 1) CHECK_EQ(s.state, std::string("proven"));
            if (s.id == 5) CHECK_EQ(s.state, std::string("partly proven"));           // the model-based half; the repair scenarios as transactions need a model
            if (s.id == 2 || s.id == 6) CHECK_EQ(s.state, std::string("not run"));     // nothing is claimed for a block that was not run
            if (s.id == 3 || s.id == 11) CHECK_EQ(s.state, std::string("needs model"));
            CHECK(!s.command.empty());
        }
    }
    return finish();
}
