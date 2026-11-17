// The layered document, slice 1 (docs/LAYERED_DOCUMENT.md): stable block ids and the issues layer, without a model. Each block is one promise of the design:
// an id survives a SYMBOL replace, a whole-file rewrite, a signature change and a rename; a split keeps the original's id; a merge carries the merged
// block's items to the survivor; a deleted block orphans its items; a block written again, or moved to another file, gets its id back; a verdict becomes
// issues bound to ids (a diagnostic repeated at many places is one issue), rendered at the lines of the CURRENT text.
#include "check.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/doc_layers.hpp"

#include <simdjson.h>

#include <string>

using namespace sub0squad;

namespace {

/// A small calculator in the shape of the tier 0 one: methods of a class, each holding the literals of what it parses.
const std::string kCalc = R"(#include <cmath>
#include <string>

class Parser
{
public:
    double expr()
    {
        double v = term();
        while (true) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term()
    {
        double v = unary();
        while (true) {
            if (eat('*')) v *= unary();
            else if (eat('%')) v = std::fmod(v, unary());
            else return v;
        }
    }
    static double factorial(double v)
    {
        double r = 1;
        for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;
        return r;
    }
    double call(const std::string& f, double x)
    {
        if (f == "sin") return std::sin(x);
        if (f == "log") return std::log10(x);
        if (f == "fact") return factorial(x);
        return x;
    }
};

double evaluate(const std::string& s)
{
    Parser p;
    (void)s;
    return p.expr();
}
)";

std::string idOf(const BlockIndex& index, const std::string& path, const std::string& leafName)
{
    for (const Block* b : index.blocks(path)) {
        const auto c = b->name.rfind(':');
        if ((c == std::string::npos ? b->name : b->name.substr(c + 1)) == leafName) return b->id;
    }
    return {};
}

std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

bool hasEvent(const BlockIndex& index, Carry carry, const std::string& id)
{
    for (const auto& e : index.events()) if (e.carry == carry && e.id == id) return true;
    return false;
}

}  // namespace

int main()
{
    const std::string path = "src/calc.cpp";
    {   // Every function gets an id; a line resolves to the block holding it; a line outside every block resolves to none.
        BlockIndex index;
        index.update(path, kCalc);
        CHECK_EQ(index.blocks(path).size(), static_cast<std::size_t>(5));
        const std::string term = idOf(index, path, "term");
        CHECK(!term.empty() && index.resolve(term) && index.resolve(term)->first == 16);
        CHECK(index.at(path, 20) && index.at(path, 20)->id == term);
        CHECK(index.at(path, 1) == nullptr);
        CHECK(index.resolve(term)->literals.count("%") == 1);
    }
    {   // A SYMBOL replace through the canvas keeps the id (the signature is the same), and the edit is in the block's history.
        BlockIndex index;
        Canvas canvas;
        canvas.addFile(path, kCalc);
        index.update(path, canvas.text(path));
        const std::string term = idOf(index, path, "term"), call = idOf(index, path, "call");
        Transaction tx{path, canvas.revision(path), "dev", {}};
        CanvasOp op;
        op.kind = CanvasOp::Kind::replaceSymbol;
        op.symbol = "term";
        op.text = "    double term()\n    {\n        double v = unary();\n        while (true) {\n            if (eat('*')) v *= unary();\n            else if (eat('/')) v /= unary();\n"
                  "            else if (eat('%')) v = std::fmod(v, unary());\n            else return v;\n        }\n    }\n";
        tx.ops.push_back(op);
        CHECK(canvas.apply(tx).ok);
        index.setTurn(1);
        index.update(path, canvas.text(path));
        CHECK_EQ(idOf(index, path, "term"), term);
        CHECK_EQ(idOf(index, path, "call"), call);                       // moved down a line, same id
        CHECK_EQ(index.resolve(call)->first, 32);
        const auto history = blockHistory(index, term);
        CHECK(history.size() == 2 && history[1].carry == Carry::kept && history[1].detail == "edited" && history[1].turn == 1);   // added, then edited
        CHECK(!hasEvent(index, Carry::kept, call));                      // untouched blocks log nothing
    }
    {   // A whole-file rewrite that reorders functions, changes one signature and renames another keeps every id.
        BlockIndex index;
        index.update(path, kCalc);
        const std::string term = idOf(index, path, "term"), factorial = idOf(index, path, "factorial"), call = idOf(index, path, "call"), expr = idOf(index, path, "expr");
        std::string rewritten = kCalc;
        const std::string factorialText = rewritten.substr(rewritten.find("    static double factorial"), rewritten.find("    double call") - rewritten.find("    static double factorial"));
        rewritten = replaceOnce(rewritten, factorialText, "");
        rewritten = replaceOnce(rewritten, "    double expr()\n", factorialText + "    double expr()\n");           // factorial moved to the top
        rewritten = replaceOnce(rewritten, "double call(const std::string& f, double x)", "double call(const std::string& f, double x, bool degrees)");   // re-signed
        rewritten = replaceOnce(rewritten, "    double term()\n", "    double multiplicative()\n");                // renamed, body unchanged
        rewritten = replaceOnce(rewritten, "v = term();", "v = multiplicative();");
        rewritten = replaceOnce(rewritten, "v += term();", "v += multiplicative();");
        rewritten = replaceOnce(rewritten, "v -= term();", "v -= multiplicative();");
        index.update(path, rewritten);
        CHECK_EQ(idOf(index, path, "factorial"), factorial);
        CHECK_EQ(idOf(index, path, "call"), call);
        CHECK_EQ(idOf(index, path, "multiplicative"), term);
        CHECK_EQ(idOf(index, path, "expr"), expr);
        CHECK(hasEvent(index, Carry::resigned, call) && hasEvent(index, Carry::renamed, term));
        CHECK_EQ(index.resolve(factorial)->first, 7);                    // resolved to its new place
    }
    {   // A split keeps the original's id and adds the new part; a merge carries the merged block's items to the survivor.
        BlockIndex index;
        index.update(path, kCalc);
        const std::string call = idOf(index, path, "call"), factorial = idOf(index, path, "factorial");
        const std::string split = replaceOnce(kCalc, "        if (f == \"log\") return std::log10(x);\n",
                                              "        if (f == \"log\") return logarithm(x);\n");
        const std::string withHelper = replaceOnce(split, "    double call(", "    static double logarithm(double x)\n    {\n        if (x <= 0) return 0;\n        return std::log10(x);\n    }\n    double call(");
        index.update(path, withHelper);
        CHECK_EQ(idOf(index, path, "call"), call);
        CHECK(!idOf(index, path, "logarithm").empty() && idOf(index, path, "logarithm") != call);
        // Merge: factorial's body moves into call(); factorial is gone. An issue anchored to factorial follows to call.
        std::string merged = withHelper;
        const std::string factorialText = merged.substr(merged.find("    static double factorial"), merged.find("    static double logarithm") - merged.find("    static double factorial"));
        merged = replaceOnce(merged, factorialText, "");
        merged = replaceOnce(merged, "        if (f == \"fact\") return factorial(x);\n",
                             "        if (f == \"fact\") {\n            double v = x;\n            double r = 1;\n            for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;\n            return r;\n        }\n");
        index.update(path, merged);
        CHECK(hasEvent(index, Carry::merged, factorial));
        CHECK(index.resolve(factorial) && index.resolve(factorial)->id == call);
        const auto history = blockHistory(index, call);                 // the survivor's history holds the merged block's too
        CHECK(std::any_of(history.begin(), history.end(), [&](const BlockEvent& e) { return e.id == factorial && e.carry == Carry::merged; }));
    }
    {   // Deleted: the item is an orphan, and its detail says so. Written again: the id comes back. Moved to another file: the id comes back there.
        BlockIndex index;
        index.update(path, kCalc);
        const std::string call = idOf(index, path, "call"), evaluate = idOf(index, path, "evaluate");
        const std::string callText = kCalc.substr(kCalc.find("    double call"), kCalc.find("};") - kCalc.find("    double call"));
        const std::string without = replaceOnce(kCalc, callText, "");
        index.update(path, without);
        CHECK(index.resolve(call) == nullptr && hasEvent(index, Carry::retired, call));
        LayerItem orphan{"issues", 1, "test", "test logarithms: log(100) is 4.6 but 2", "FAIL 9 - logarithms: ...", {{call, path, 30, Anchor::Confidence::suspect}}};
        CHECK(layerDetail(orphan, index, {{path, without}}).find("no longer exists") != std::string::npos);
        CHECK(layerSummary({orphan}, index).find("a function that no longer exists") != std::string::npos);
        index.update(path, kCalc);                                      // written again (the canvas's "FILE first, then SYMBOL each function")
        CHECK_EQ(idOf(index, path, "call"), call);
        CHECK(hasEvent(index, Carry::revived, call));
        const std::string evalText = kCalc.substr(kCalc.find("double evaluate"));
        index.update(path, replaceOnce(kCalc, evalText, ""));
        index.update("src/eval.cpp", "#include <string>\n\n" + evalText);
        CHECK_EQ(idOf(index, "src/eval.cpp", "evaluate"), evaluate);
    }
    {   // Python: indentation blocks get ids too.
        BlockIndex index;
        const std::string py = "def add(a, b):\n    return a + b\n\n\ndef sub(a, b):\n    return a - b\n";
        index.update("calc/ops.py", py);
        const std::string sub = idOf(index, "calc/ops.py", "sub");
        index.update("calc/ops.py", "def sub(a, b):\n    return a - b\n\n\ndef add(a, b):\n    return a + b\n");
        CHECK_EQ(idOf(index, "calc/ops.py", "sub"), sub);
        CHECK_EQ(index.resolve(sub)->first, 1);
    }
    {   // A verdict becomes issues bound to blocks: failing tests to their suspect block (named like the test, or holding the literal under test), every
        // diagnostic to the block holding its line, and one diagnostic at several places to ONE issue with an anchor per place.
        BlockIndex index;
        index.update(path, kCalc);
        const std::string verdict =
            "GATE FAIL: 3 of 16 tests failed\n"
            "FAIL 6 - modulo: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\n"
            "FAIL 7 - factorial: CHECK_NEAR failed: ev(\"5!\") is 24 but 120 is 120\n"
            "FAIL 9 - logarithms: CHECK_NEAR failed: ev(\"log(100)\") is 4.60517 but 2 is 2\n";
        const auto issues = parseIssues(verdict, index);
        CHECK_EQ(issues.size(), static_cast<std::size_t>(3));
        if (issues.size() == 3) {
            CHECK(issues[0].kind == "test" && issues[0].number == 1 && issues[0].anchors.size() == 1 && issues[0].anchors[0].blockId == idOf(index, path, "term"));
            CHECK(issues[0].anchors[0].confidence == Anchor::Confidence::suspect);
            CHECK(issues[1].anchors.size() == 1 && issues[1].anchors[0].blockId == idOf(index, path, "factorial"));
            CHECK(issues[2].anchors.size() == 1 && issues[2].anchors[0].blockId == idOf(index, path, "call"));
            const std::string summary = layerSummary(issues, index);
            CHECK(summary.rfind("3 issues (`=== READ: ISSUE n ===`", 0) == 0);
            CHECK(summary.find(" 1. test modulo: CHECK_NEAR failed: ev(\"10%3\")") != std::string::npos && summary.find("likely in term() (lines 16-24)") != std::string::npos);
            CHECK(summary.find(" 3. test logarithms") != std::string::npos && summary.find("likely in call()") != std::string::npos);
            // The ids hold after a whole-file rewrite that moves everything down: the same issues render at the new lines.
            const std::string moved = "// a header comment\n// and another\n" + kCalc;
            index.update(path, moved);
            CHECK(layerSummary(issues, index).find("likely in term() (lines 18-26)") != std::string::npos);
            const std::string detail = layerDetail(issues[0], index, {{path, moved}});
            CHECK(detail.rfind("=== ISSUE 1 (test) ===\nFAIL 6 - modulo", 0) == 0);
            CHECK(detail.find("Likely in src/calc.cpp, term(), now at lines 18-26:\n  18|     double term()") != std::string::npos);
            CHECK(layerFocus(issues, index).find("src/calc.cpp:18\n") != std::string::npos);
        }
        index.update(path, kCalc);
        const std::string compile =
            "does not compile: src/calc.cpp\n"
            "  src/calc.cpp:11:35: error: 'terms' was not declared in this scope\n"
            "     11 |             if (eat('+')) v += terms();\n"
            "        |                                   ^~~~~\n"
            "  src/calc.cpp:12:40: error: 'terms' was not declared in this scope\n"
            "  src/calc.cpp:43:20: error: 'terms' was not declared in this scope\n"
            "  src/calc.cpp:2:1: error: expected unqualified-id before 'oops'\n"
            "The code does not compile, so no test could run. Fix the errors first.\n";
        const auto errs = parseIssues(compile, index);
        CHECK_EQ(errs.size(), static_cast<std::size_t>(2));
        if (errs.size() == 2) {
            CHECK(errs[0].kind == "error" && errs[0].anchors.size() == 3);                       // 1-N: one issue, three places
            CHECK(errs[0].anchors[0].blockId == idOf(index, path, "expr") && errs[0].anchors[0].confidence == Anchor::Confidence::exact);
            CHECK(errs[0].anchors[2].blockId == idOf(index, path, "evaluate"));
            CHECK(errs[0].detail.find("^~~~~") != std::string::npos);                            // the first occurrence keeps its caret
            CHECK(errs[1].anchors.size() == 1 && errs[1].anchors[0].blockId.empty() && errs[1].anchors[0].line == 2);   // outside every block: path and line
            const std::string summary = layerSummary(errs, index);
            CHECK(summary.find("- in 3 places: expr(), evaluate()") != std::string::npos);
            CHECK(summary.find("- in src/calc.cpp:2") != std::string::npos);
            CHECK(layerDetail(errs[0], index, {{path, kCalc}}).find("It occurs at 3 places") != std::string::npos);
            CHECK(layerDetail(errs[1], index, {{path, kCalc}}).find("At src/calc.cpp:2 (outside any function):") != std::string::npos);
        }
        // Python's syntax check names a line too.
        const auto py = parseIssues("syntax error in src/calc.cpp:20: invalid syntax\n    else if\nThe code does not parse, so no test could run.", index);
        CHECK(py.size() == 1 && py[0].anchors.size() == 1 && py[0].anchors[0].blockId == idOf(index, path, "term"));
    }
    {   // Reads name issues by number; anything else is not an issue read.
        const auto asks = parseIssueReads("=== READ: ISSUE 3 ===\n=== READ: ISSUES 1, 2 ===\n=== READ: src/calc.cpp SYMBOL term ===\n");
        CHECK(asks == (std::vector<int>{3, 1, 2}));
        CHECK(parseIssueReads("=== READ: src/ISSUE.cpp LINES 1-3 ===").empty());
    }
    {   // The interchange form is valid JSON carrying the schema, the blocks, their history and the layers with every anchor.
        BlockIndex index;
        index.update(path, kCalc);
        const auto issues = parseIssues("FAIL 6 - modulo: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\n", index);
        const std::string j = layeredJson(4, {{path, kCalc}}, {{path, 2}}, index, {{"issues", issues}});
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        CHECK(!parser.parse(j).get(doc));
        CHECK(j.find("\"schema\":\"layered-doc-1\"") != std::string::npos && j.find("\"turn\":4") != std::string::npos);
        CHECK(j.find("\"revision\":2") != std::string::npos && j.find("\"carry\":\"added\"") != std::string::npos);
        CHECK(j.find("\"confidence\":\"suspect\"") != std::string::npos && j.find("\"blockId\":\"" + idOf(index, path, "term") + "\"") != std::string::npos);
    }
    return finish();
}
