// Edit replies and the canvas they are applied to: each edit exactly once, every failure names the edit, other files untouched.
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
    { // Edit replies: applied to the earlier files, exactly once each; every failure names the edit; the other files are untouched.
        const std::map<std::string, std::string> files{{"src/a.js", "function add(a, b) {\n  return a - b;\n}\nexport { add };\n"}, {"src/b.js", "export const x = 1;\n"}};
        const std::set<std::string> allowed{"src/a.js", "src/b.js"};
        std::string err;
        auto ok = applyEdits("Here is the fix.\n=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  return a - b;\n=======\n  return a + b;\n>>>>>>> REPLACE\n=== END EDIT ===\n", files, allowed, err);
        CHECK(ok && ok->at("src/a.js") == "function add(a, b) {\n  return a + b;\n}\nexport { add };\n" && ok->at("src/b.js") == "export const x = 1;\n");
        auto two = applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\nfunction add(a, b)\n=======\nfunction add(a, b = 0)\n>>>>>>> REPLACE\n<<<<<<< SEARCH\n  return a - b;\n=======\n  return a + b;\n>>>>>>> REPLACE\n=== END EDIT ===\n=== EDIT: src/b.js ===\n<<<<<<< SEARCH\nconst x = 1;\n=======\nconst x = 2;\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err);
        CHECK(two && two->at("src/a.js").find("b = 0") != std::string::npos && two->at("src/a.js").find("a + b") != std::string::npos && two->at("src/b.js") == "export const x = 2;\n");
        {   // Folded views: structure stays, function bodies fold, focus regions open, READ requests are answered.
            std::string cpp = "#include <string>\nnamespace n {\nclass A {\npublic:\n  int get() const { return v; }\n  int v = 0;\n};\ndouble f(double x)\n{\n  double y = x;\n  if (y > 1) {\n    y = 2;\n  }\n  y += 1;\n  return y;\n}\nint g(int a) { return a; }\n}\n";
            const auto regions = foldRegions(cpp, false);
            CHECK(regions.size() == 1 && regions[0].name == "f" && regions[0].first == 8 && regions[0].bodyFrom == 10 && regions[0].bodyTo == 16);   // g and get are one line: nothing to fold; namespace and class stay open
            const std::string small = foldedView(cpp, false, {}, 100000);
            CHECK(small.find("folded") == std::string::npos);                                  // fits: shown whole
            std::string big = "namespace n {\nclass A {\n  int v = 0;\n};\n";   // two functions of 40 body lines each, so a budget can force folding
            for (const char* fn : {"first", "second"}) { big += std::string("double ") + fn + "(double x)\n{\n"; for (int k = 0; k < 40; ++k) big += "  x += " + std::to_string(k) + ";\n"; big += "  return x;\n}\n"; }
            big += "}\n";
            const std::string tight = foldedView(big, false, {}, 700);
            CHECK(tight.find("lines folded: 7-47") != std::string::npos && tight.find("double first(double x)") != std::string::npos && tight.find("class A {") != std::string::npos && tight.find("x += 20;") == std::string::npos);
            const std::string open = foldedView(big, false, {70}, 1300);   // a failure at line 70 (inside `second`) keeps that function open, `first` stays folded
            CHECK(open.find("lines folded: 7-47") != std::string::npos && open.find("lines folded: 51-") == std::string::npos && open.find("x += 20;") != std::string::npos);
            const auto reqs = parseReadRequests("Let me look.\n=== READ: src/a.cpp LINES 9-12 ===\n=== READ: src/a.cpp SYMBOL f ===\n");
            CHECK(reqs.size() == 2 && reqs[0].from == 9 && reqs[0].to == 12 && reqs[1].symbol == "f");
            const std::string ans = answerRead(reqs[0], cpp, false);
            CHECK(ans.find("   9| {") != std::string::npos && ans.find("  12| ") != std::string::npos && ans.find("  13| ") == std::string::npos);
            CHECK(answerRead(reqs[1], cpp, false).find("double f(double x)") != std::string::npos);
            CHECK(answerRead({"x.cpp", 99, 100, ""}, cpp, false).find("do not exist") != std::string::npos);
            const std::string py = "import math\n\ndef a(x):\n    y = x\n    return y\n\ndef b():\n    pass\n";
            const auto pr = foldRegions(py, true);
            CHECK(pr.size() == 1 && pr[0].name == "a" && pr[0].first == 3 && pr[0].bodyTo == 5);
        }
        {   // The canvas: transactions against a revision, all or nothing, rebased through earlier edits, conflicts named, any revision restorable.
            Canvas cv;
            cv.addFile("a.cpp", "l1\nl2\nl3\nl4\nl5\nl6\n");
            CHECK(cv.revision("a.cpp") == 1);
            auto ok = cv.apply({"a.cpp", 1, "w1", {{CanvasOp::Kind::replaceLines, 2, 3, "", "X\nY\nZ"}}});    // lines 2-3 become three lines: +1
            CHECK(ok.ok && ok.revision == 2 && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nl5\nl6\n");
            auto stale = cv.apply({"a.cpp", 1, "w2", {{CanvasOp::Kind::replaceLines, 5, 5, "", "FIVE"}}});    // made against revision 1: line 5 is now line 6
            CHECK(stale.ok && stale.revision == 3 && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nFIVE\nl6\n");
            auto clash = cv.apply({"a.cpp", 1, "w3", {{CanvasOp::Kind::replaceLines, 3, 3, "", "no"}}});       // line 3 was changed by w1 in revision 2
            CHECK(!clash.ok && clash.error.find("w1") != std::string::npos && clash.error.find("revision 2") != std::string::npos && clash.revision == 3);
            auto bad = cv.apply({"a.cpp", 3, "w3", {{CanvasOp::Kind::replaceLines, 1, 1, "", "ONE"}, {CanvasOp::Kind::replaceLines, 99, 99, "", "x"}}});   // the second op is invalid: nothing applies
            CHECK(!bad.ok && cv.text("a.cpp") == "l1\nX\nY\nZ\nl4\nFIVE\nl6\n" && cv.revision("a.cpp") == 3);
            auto multi = cv.apply({"a.cpp", 3, "w3", {{CanvasOp::Kind::replaceLines, 1, 1, "", "ONE"}, {CanvasOp::Kind::insertAfter, 7, 0, "", "tail"}}});   // numbering of the base revision
            CHECK(multi.ok && cv.text("a.cpp") == "ONE\nX\nY\nZ\nl4\nFIVE\nl6\ntail\n");
            auto overlap = cv.apply({"a.cpp", 4, "w3", {{CanvasOp::Kind::replaceLines, 2, 4, "", "a"}, {CanvasOp::Kind::replaceLines, 4, 5, "", "b"}}});
            CHECK(!overlap.ok && overlap.error.find("overlap") != std::string::npos);
            CHECK(cv.textAt("a.cpp", 1) == "l1\nl2\nl3\nl4\nl5\nl6\n");
            auto rv = cv.revertTo("a.cpp", 1, "owner");
            CHECK(rv.ok && rv.revision == 5 && cv.text("a.cpp") == cv.textAt("a.cpp", 1));
            CHECK(!cv.apply({"a.cpp", 4, "w4", {{CanvasOp::Kind::replaceLines, 1, 1, "", "q"}}}).ok);   // a revert touched everything: older edits conflict
            // Replace a function by name: no line arithmetic.
            cv.addFile("f.cpp", "int a() { return 1; }\ndouble f(double x)\n{\n  x += 1;\n  x += 2;\n  return x;\n}\nint z;\n");
            auto sym = cv.apply({"f.cpp", 1, "w5", {{CanvasOp::Kind::replaceSymbol, 0, 0, "f", "double f(double x) { return x; }"}}});
            CHECK(sym.ok && cv.text("f.cpp") == "int a() { return 1; }\ndouble f(double x) { return x; }\nint z;\n");
            CHECK(!cv.apply({"f.cpp", 2, "w5", {{CanvasOp::Kind::replaceSymbol, 0, 0, "nope", "x"}}}).ok);
        }
        {   // Line edits: the worker names lines of the numbered draft it was shown; several edits apply together against that numbering.
            std::map<std::string, std::string> f2 = {{"src/a.js", "one\ntwo\nthree\nfour\nfive\n"}};
            std::set<std::string> al = {"src/a.js"};
            auto one = applyEdits("=== EDIT: src/a.js LINES 2 ===\nTWO\n=== END EDIT ===", f2, al, err);
            CHECK(one && one->at("src/a.js") == "one\nTWO\nthree\nfour\nfive\n");
            auto range = applyEdits("=== EDIT: src/a.js LINES 2-3 ===\nX\nY\nZ\n=== END EDIT ===", f2, al, err);
            CHECK(range && range->at("src/a.js") == "one\nX\nY\nZ\nfour\nfive\n");
            auto del = applyEdits("=== EDIT: src/a.js LINES 4-5 ===\n=== END EDIT ===", f2, al, err);
            CHECK(del && del->at("src/a.js") == "one\ntwo\nthree\n");
            auto ins = applyEdits("=== EDIT: src/a.js AFTER 1 ===\nnew\n=== END EDIT ===", f2, al, err);
            CHECK(ins && ins->at("src/a.js") == "one\nnew\ntwo\nthree\nfour\nfive\n");
            auto both = applyEdits("=== EDIT: src/a.js LINES 1 ===\nONE\n=== END EDIT ===\n=== EDIT: src/a.js LINES 5 ===\nFIVE\n=== END EDIT ===", f2, al, err);   // numbering of the draft, not of the result
            CHECK(both && both->at("src/a.js") == "ONE\ntwo\nthree\nfour\nFIVE\n");
            CHECK(!applyEdits("=== EDIT: src/a.js LINES 9 ===\nx\n=== END EDIT ===", f2, al, err) && err.find("5 lines") != std::string::npos);
            CHECK(!applyEdits("=== EDIT: src/a.js LINES 2 ===\nx\n=== END EDIT ===\n=== EDIT: src/a.js ===\n<<<<<<< SEARCH\none\n=======\n1\n>>>>>>> REPLACE\n=== END EDIT ===", f2, al, err) && err.find("one kind per file") != std::string::npos);
            // A SEARCH copied with the numbered view's "N| " column still matches.
            auto numbered = applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n   2| two\n=======\n2\n>>>>>>> REPLACE\n=== END EDIT ===", f2, al, err);
            CHECK(numbered && numbered->at("src/a.js") == "one\n2\nthree\nfour\nfive\n");
        }
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  return a * b;\n=======\n  return 0;\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err) && err.find("does not appear") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\n  \n=======\n  x\n>>>>>>> REPLACE\n=== END EDIT ===", {{"src/a.js", "  a\n  b\n"}}, allowed, err) && err.find("more than once") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: ../evil.js ===\n<<<<<<< SEARCH\nx\n=======\ny\n>>>>>>> REPLACE\n=== END EDIT ===", files, allowed, err) && err.find("not one of the files") != std::string::npos);
        CHECK(!applyEdits("=== EDIT: src/a.js ===\n<<<<<<< SEARCH\nfoo\n=======\nbar\n=== END EDIT ===", files, allowed, err) && err.find("not closed") != std::string::npos);
        CHECK(!applyEdits("no edits here", files, allowed, err) && err.find("no edit blocks") != std::string::npos);
    }
    return finish();
}
