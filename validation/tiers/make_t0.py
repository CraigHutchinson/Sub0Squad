"""Tier 0 plans for the C++ calculator: small, seeded tasks that each isolate one kind of work a canvas worker must do, before tier 1 (the whole program).

    python validation/tiers/make_t0.py <repo> [case ...]      (no case: all)  ->  out/runs/t0/<case>/plan.json
    python validation/tiers/make_t0.py <repo> --list [smoke | rewrite | tree]      (tree: the case tree by layer, docs/T0_LAYERS.md)

Every case seeds the reference implementation (validation/reference/calculator-cpp) with a defect, a stub or a missing part, and gives the reference
tests as the owner's (`given`) tests. The cases are built on validation/tiers/t1-plan.json (the tier 1 plan). `--check <case> <dir>` writes the seeded
workspace files to <dir> so a case can be checked without a model (it must fail in the way the case says).
"""
import json, os, pathlib, re, sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
REF = ROOT / "validation/reference/calculator-cpp"


def ref(path):
    return (REF / path).read_text(encoding="utf-8")


def sub(text, old, new):
    assert text.count(old) == 1, f"not exactly one site for: {old!r}"
    return text.replace(old, new)


def stub_function(text, head, body):
    start = text.index(head)
    end = text.index("\n    }\n", start) + len("\n    }\n")
    return text[:start] + head + body + "    }\n" + text[end:]


CALC, MAIN = ref("src/calc.cpp"), ref("src/main.cpp")
TEMPLATES = (ROOT / "src/cpp_templates.cpp").read_text(encoding="utf-8")
HEADER = re.search(r'const char\* kHeader = R"CPP\((.*?)\)CPP";', TEMPLATES, re.S).group(1)
STUB_MAIN = re.search(r'const char\* kStubMain = R"CPP\((.*?)\)CPP";', TEMPLATES, re.S).group(1)
TEST_HEAD = '#include "calc/calc.hpp"\n#include "helpers/check.hpp"\n\n#include <stdexcept>\n\nstatic double ev(const char* e, bool deg = false) { return calc::evaluate(e, deg); }\n\n'
# The owner's tests of the whole factorial feature: without them a worker passes write-feature with `5!` alone (run 1 of 2026-11-17 inlined it in postfix,
# left out fact(), the 170 limit and repeated !, and the gate could not tell).
FACT_TESTS = TEST_HEAD + (
    "TEST(factorial_function) { CHECK_NEAR(ev(\"fact(5)\"), 120, 1e-9); CHECK_NEAR(ev(\"fact(0)\"), 1, 1e-9); }\n"
    "TEST(factorial_repeats) { CHECK_NEAR(ev(\"3!!\"), 720, 1e-9); CHECK_NEAR(ev(\"2+3!\"), 8, 1e-9); }\n"
    "TEST(factorial_limits) { CHECK_NEAR(ev(\"170!\") > 1e306 ? 1 : 0, 1, 1e-9); CHECK_THROWS(ev(\"171!\")); CHECK_THROWS(ev(\"(-1)!\")); CHECK_THROWS(ev(\"2.5!\")); CHECK_THROWS(ev(\"fact(1,2)\")); }\n")
# Degrees mode for all six trig functions: the given tests check sin and asin only, so without these a repeated-class case passes with two sites of six fixed.
TRIG_TESTS = TEST_HEAD + (
    "TEST(degrees_forward) { CHECK_NEAR(ev(\"cos(60)\", true), 0.5, 1e-9); CHECK_NEAR(ev(\"tan(45)\", true), 1, 1e-9); CHECK_NEAR(ev(\"sin(30)\", true), 0.5, 1e-9); }\n"
    "TEST(degrees_inverse) { CHECK_NEAR(ev(\"acos(0.5)\", true), 60, 1e-9); CHECK_NEAR(ev(\"atan(1)\", true), 45, 1e-9); CHECK_NEAR(ev(\"asin(0.5)\", true), 30, 1e-9); }\n"
    "TEST(radians_unchanged) { CHECK_NEAR(ev(\"cos(0)\"), 1, 1e-9); CHECK_NEAR(ev(\"atan(1)\"), 0.785398163397448, 1e-9); }\n")
# Arithmetic only (the reference tests of + - * / ^ %, precedence, unary minus, decimals and division by zero): replace-function asks for one function
# redesigned at a modest size, not the whole engine.
EVAL_TESTS = ref("tests/eval.test.cpp")
ARITH_TESTS = TEST_HEAD + "".join(l + "\n" for l in EVAL_TESTS.splitlines() if re.match(
    r"TEST\((addition_and_subtraction|multiplication_and_division|precedence|power_is_right_associative|unary_minus_binds_looser_than_power|modulo|decimals|division_by_zero_throws)\)", l))
ARITH_CONTRACT = (
    "ENGINE: Implement double calc::evaluate(const std::string& expr, bool degrees = false), "
    "declared in include/calc/calc.hpp and defined in src/calc.cpp. "
    "Handle arithmetic expressions with whitespace, decimal and scientific notation, parentheses, "
    "unary + and -, and binary + - * / % ^. Multiplication, division and remainder bind tighter than "
    "addition and subtraction. Power is right associative and binds tighter than unary minus, so -2^2 is -4. "
    "Division or remainder by zero and malformed expressions raise std::runtime_error. "
    "Keep the degrees argument for interface compatibility. It has no effect on arithmetic expressions."
)
GIVEN_TEXT = {"tests/fact.test.cpp": FACT_TESTS, "tests/trig.test.cpp": TRIG_TESTS, "tests/arith.test.cpp": ARITH_TESTS}

# Curveballs: a naive engine whose approach cannot meet the spec (the right reply is a rewrite), and a parser skeleton with every method still a stub.
NAIVE = '''#include "calc/calc.hpp"

#include <cstdlib>
#include <stdexcept>

namespace calc {

// Evaluates left to right: numbers and + - * / only.
double evaluate(const std::string& expr, bool degrees)
{
    (void)degrees;
    double acc = 0;
    char op = '+';
    std::size_t i = 0;
    while (i < expr.size()) {
        if (expr[i] == ' ') { ++i; continue; }
        char* end = nullptr;
        const double v = std::strtod(expr.c_str() + i, &end);
        if (end == expr.c_str() + i) throw std::runtime_error("bad input");
        i = static_cast<std::size_t>(end - expr.c_str());
        switch (op) {
        case '+': acc += v; break;
        case '-': acc -= v; break;
        case '*': acc *= v; break;
        case '/': acc /= v; break;
        default: throw std::runtime_error("bad operator");
        }
        while (i < expr.size() && expr[i] == ' ') ++i;
        if (i < expr.size()) op = expr[i++];
    }
    return acc;
}

}  // namespace calc
'''


def skeleton(text):
    for name, head, extra in [("run", "    double run()\n    {\n", ""), ("expr", "    double expr()\n    {\n", ""), ("term", "    double term()\n    {\n", ""),
                              ("unary", "    double unary()\n    {\n", ""), ("postfix", "    double postfix()\n    {\n", ""),
                              ("factorial", "    static double factorial(double v)\n    {\n", "        (void)v;\n"),
                              ("primary", "    double primary()\n    {\n", ""), ("number", "    double number()\n    {\n", ""),
                              ("name", "    double name()\n    {\n", ""), ("call", "    double call(const std::string& f, const std::vector<double>& a)\n    {\n", "        (void)f;\n        (void)a;\n")]:
        text = stub_function(text, head, extra + f"        fail(\"not implemented: {name}\");\n")
    return text


def conflict(text, line, theirs, branch):
    """A merge conflict left in the file: `line` (ours) against `theirs`."""
    indent = line[:len(line) - len(line.lstrip())]
    return sub(text, line + "\n", f"<<<<<<< HEAD\n{line}\n=======\n{indent}{theirs.strip()}\n>>>>>>> {branch}\n")


def repeat_trig(text):
    """The repeated class: every trig call loses its degree conversion (six sites, one pattern)."""
    for f in ("sin", "cos", "tan"): text = sub(text, f"std::{f}(toRad(x))", f"std::{f}(x)")
    for f in ("asin", "acos", "atan"): text = sub(text, f"fromRad(std::{f}(x))", f"std::{f}(x)")
    return text


def repeat_rename(text):
    """The repeated class: the error helper is called by a name that does not exist at every call site (the definition keeps `fail`)."""
    head = "[[noreturn]] static void fail("
    a = text.index(head) + len(head)
    return text[:a] + text[a:].replace(" fail(", " error(")


# Independent defects, each breaking a different test, for the scaling cases: a worker that fixes one defect per turn costs a turn per defect, which a real
# project cannot afford (tier 0 of 2026-11-17: fix-two-bugs took two turns in three runs of four, with both failures shown in its first message).
DEFECT_BANK = [
    ("v = std::fmod(v, d); }", "v = v / d; }"),                                                                      # modulo
    ("for (int i = 2; i <= static_cast<int>(v); ++i)", "for (int i = 2; i < static_cast<int>(v); ++i)"),             # factorial
    ("return degrees_ ? x * 180 / M_PI : x;", "return degrees_ ? x * M_PI / 180 : x;"),                               # degrees
    ("return std::log10(x); }", "return std::log(x); }"),                                                             # logarithms
    ("f == \"min\" ? std::fmin(a[0], a[1])", "f == \"min\" ? std::fmax(a[0], a[1])"),                                 # two_argument_functions
    ("{ const double d = unary(); if (d == 0) fail(\"division by zero\"); v /= d; }", "v /= unary();"),               # division_by_zero_throws
    ("if (id == \"tau\") return 2 * M_PI;", "if (id == \"tau\") return M_PI;"),                                       # constants
    ("else if (eat('-')) v -= term();", "else if (eat('-')) v += term();"),                                           # addition_and_subtraction
    ("if (f == \"cbrt\") return std::cbrt(x);", "if (f == \"cbrt\") return std::sqrt(x);"),                           # roots_and_rounding
    ("const double v = expr(); if (!eat(')')) fail(\"missing closing parenthesis\"); return v; }",
     "const double v = expr(); (void)eat(')'); return v; }"),                                                        # invalid_input_throws
]


def with_defects(n):
    text = CALC
    for old, new in DEFECT_BANK[:n]: text = sub(text, old, new)
    return text


NUMBER = CALC[CALC.index("    double number()"):CALC.index("    double name()")]
STUB_ENGINE = (CALC[:CALC.index("namespace calc {")] + "namespace calc {\n\ndouble evaluate(const std::string& expr, bool degrees)\n{\n    (void)expr;\n"
               "    (void)degrees;\n    throw std::runtime_error(\"not implemented\");\n}\n\n}  // namespace calc\n")
TRIG_HELPERS = CALC[CALC.index("    double toRad(double x)"):CALC.index("    double call(const std::string& f")]
CALL_AT = CALC.index("    double call(const std::string& f")
CALL = CALC[CALL_AT:CALC.index("\n    }\n", CALL_AT) + len("\n    }\n")]
CALL_HEAD = "    double call(const std::string& f, const std::vector<double>& a)\n    {\n"
FACTORIAL = CALC[CALC.index("    static double factorial(double v)"):CALC.index("    double primary()")]

# Each case: what it exercises, the seeded files (path -> text; anything not given is the reference), the item's files, and the card's context.
CASES = {
    "fix-op": dict(
        exercises="one wrong line: a snippet edit",
        calc=sub(CALC, "v = std::fmod(v, d); }", "v = v / d; }"),
        context="src/calc.cpp is a complete calculator engine that nearly works: some of the provided tests fail. Find the defect and fix it with the smallest edit."),
    "fix-boundary": dict(
        exercises="an off-by-one in a loop",
        calc=sub(CALC, "for (int i = 2; i <= static_cast<int>(v); ++i)", "for (int i = 2; i < static_cast<int>(v); ++i)"),
        context="src/calc.cpp is a complete calculator engine that nearly works: some of the provided tests fail. Find the defect and fix it with the smallest edit."),
    "fix-distant": dict(
        exercises="a symptom (a parse error) far from its cause (the grammar of ^)",
        calc=sub(CALC, "if (eat('^')) return std::pow(base, unary());", "if (eat('^')) return std::pow(base, postfix());"),
        context="src/calc.cpp is a complete calculator engine that nearly works: some of the provided tests fail. Find the defect and fix it with the smallest edit."),
    "fix-missing-check": dict(
        exercises="a missing check: code must be added, not changed",
        calc=sub(CALC, "{ const double d = unary(); if (d == 0) fail(\"division by zero\"); v /= d; }", "v /= unary();"),
        context="src/calc.cpp is a complete calculator engine that nearly works: some of the provided tests fail. Find the defect and fix it with the smallest edit."),
    "fix-two-bugs": dict(
        exercises="two independent defects in two functions: several edits in one reply",
        defects=2,
        calc=sub(sub(CALC, "v = std::fmod(v, d); }", "v = v / d; }"), "return degrees_ ? x * 180 / M_PI : x;", "return degrees_ ? x * M_PI / 180 : x;"),
        context="src/calc.cpp is a complete calculator engine that nearly works: some of the provided tests fail. It has more than one defect. Find them and fix each with the smallest edit."),
    "compile-typo": dict(
        exercises="starting files that do not compile (a syntax error)",
        calc=sub(CALC, "        const double x = a[0];\n", "        const double x = a[0]\n"),
        context="src/calc.cpp is a complete calculator engine, but it does not compile. Fix it with the smallest edit."),
    # T1 (2026-11-18, the 35B, output limit 6000): Parser::check_eol() defined out of the class and called, never declared. The errors name the
    # definition and the call, both right as written; the fix is a declaration in the class, at neither line. The worker read folded bodies for five turns.
    "undeclared-member": dict(
        exercises="a compile error whose fix is away from the lines it names (a member defined and called, never declared in its class)",
        calc=sub(sub(CALC, "        skip();\n        if (pos_ != s_.size()) fail(std::string(\"unexpected '\") + s_[pos_] + \"'\");\n", "        checkEnd();\n"),
                 "    std::size_t pos_ = 0;\n};\n",
                 "    std::size_t pos_ = 0;\n};\n\nvoid Parser::checkEnd()\n{\n    skip();\n    if (pos_ != s_.size()) fail(std::string(\"unexpected '\") + s_[pos_] + \"'\");\n}\n"),
        context="src/calc.cpp is a complete calculator engine, but it does not compile. Fix it with the smallest edit."),
    "undeclared-many": dict(
        exercises="one cause, a long error list: a member moved out of its class without its declaration (every call site errs)",
        calc=sub(sub(CALC, "    bool eat(char c) { skip(); if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; } return false; }\n", ""),
                 "    std::size_t pos_ = 0;\n};\n",
                 "    std::size_t pos_ = 0;\n};\n\nbool Parser::eat(char c)\n{\n    skip();\n    if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }\n    return false;\n}\n"),
        context="src/calc.cpp is a complete calculator engine, but it does not compile. Fix it with the smallest edit."),
    "compile-warning": dict(
        exercises="a warning that the build treats as an error",
        calc=sub(CALC, "        const char* start = s_.c_str() + pos_;\n", "        const char* start = s_.c_str() + pos_;\n        const std::size_t left = s_.size() - pos_;\n"),
        context="src/calc.cpp is a complete calculator engine, but it does not build: warnings are errors here. Fix it with the smallest edit."),
    "write-function": dict(
        exercises="one function written into a working file (SYMBOL replaces a stub)",
        calc=stub_function(CALC, CALL_HEAD, "        (void)a;\n        fail(\"not implemented: \" + f);\n"),
        context="src/calc.cpp is a complete calculator engine except for one function, call(), which is still a stub: it must apply a named function (sin, pow, ...) to its arguments. Write that one function."),
    "write-feature": dict(
        exercises="a feature missing from several places: a new function (SYMBOL creates) and the lines that use it",
        calc=sub(sub(CALC.replace(FACTORIAL, ""), "        while (eat('!')) v = factorial(v);\n", ""), "        if (f == \"fact\") return factorial(x);\n", ""),
        given=["tests/fact.test.cpp"],
        context="src/calc.cpp is a complete calculator engine except that factorial is missing: neither the postfix ! operator nor the fact() function exists. Add it."),
    "write-file": dict(
        exercises="a whole small file written (FILE) over a stub, against a finished engine",
        main=STUB_MAIN, files=["src/main.cpp"],
        context="The engine (src/calc.cpp) is finished. src/main.cpp is a stub: write the command-line app."),
    "write-tests": dict(
        exercises="the tester's work: a test file written against a finished engine (it must compile and pass)",
        persona="tester", files=["tests/more.test.cpp"], tests=["tests/more.test.cpp"], given=[], minTests=8,
        context="The engine (src/calc.cpp) is finished and correct. Write tests/more.test.cpp: at least 8 TEST cases for calc::evaluate that the provided tests "
                "do not cover (degrees mode for every trig function, hyperbolic functions, exp, abs, ceil, nested functions, whitespace, and errors such as "
                "unknown functions, ln of 0 and a trailing operator). Use tests/helpers/check.hpp (TEST, CHECK, CHECK_EQ, CHECK_NEAR, CHECK_THROWS) the way "
                "tests/eval.test.cpp does. Every test must pass against the engine."),
    # ---- curveballs: messier starting points, and cards that do not say what is wrong ----
    "fix-many": dict(
        exercises="curveball: five defects in five functions",
        defects=5,
        calc=sub(sub(sub(sub(sub(CALC, "v = std::fmod(v, d); }", "v = v / d; }"),
                                 "for (int i = 2; i <= static_cast<int>(v); ++i)", "for (int i = 2; i < static_cast<int>(v); ++i)"),
                             "return degrees_ ? x * 180 / M_PI : x;", "return degrees_ ? x * M_PI / 180 : x;"),
                         "return std::log10(x); }", "return std::log(x); }"),
                     "f == \"min\" ? std::fmin(a[0], a[1])", "f == \"min\" ? std::fmax(a[0], a[1])"),
        context="src/calc.cpp is a calculator engine. Many of the provided tests fail."),
    "half-written": dict(
        exercises="curveball: a file cut off part way through a function (it does not compile)",
        calc=CALC[:CALC.index("        if (!eat('(')) fail(\"unknown name '\" + id + \"'\");")],
        context="src/calc.cpp is a calculator engine that was being written when its author stopped."),
    "merge-conflict": dict(
        exercises="curveball: unresolved merge conflicts, one right and one wrong side each",
        calc=conflict(conflict(CALC, "            else if (eat('%')) { const double d = unary(); if (d == 0) fail(\"modulo by zero\"); v = std::fmod(v, d); }",
                               "else if (eat('%')) { const double d = unary(); v = v - d * static_cast<long>(v / d); }", "feature/fast-modulo"),
                      "    double fromRad(double x) const { return degrees_ ? x * 180 / M_PI : x; }",
                      "double fromRad(double x) const { return degrees_ ? x * M_PI / 180 : x; }", "feature/degrees"),
        context="src/calc.cpp is a calculator engine. It does not build."),
    "wrong-approach": dict(
        exercises="curveball: an engine whose approach cannot meet the spec (the right reply is a rewrite)",
        calc=NAIVE,
        context="src/calc.cpp is a calculator engine. Most of the provided tests fail."),
    "duplicate": dict(
        exercises="curveball: a function defined twice, the second copy wrong (it does not compile; deleting the wrong copy leaves a test failing)",
        calc=sub(CALC, NUMBER, NUMBER + NUMBER.replace("const double v = std::strtod(start, &end);", "const double v = std::strtol(start, &end, 10);")),
        context="src/calc.cpp is a calculator engine. It does not build."),
    "compile-and-logic": dict(
        exercises="curveball: a syntax error hiding a logic defect (the tests fail only once it builds)",
        calc=sub(sub(CALC, "        const double x = a[0];\n", "        const double x = a[0]\n"), "v = std::fmod(v, d); }", "v = v / d; }"),
        context="src/calc.cpp is a calculator engine. It does not build."),
    "skeleton": dict(
        exercises="curveball: a parser skeleton, every method a stub: the whole engine written into an outline",
        calc=skeleton(CALC),
        context="src/calc.cpp is the outline of a recursive descent calculator engine: the class and its methods are laid out, but no method is written yet."),
    # ---- base requirements of a rewrite (wrong-approach decomposed, 2026-11-18): each tested on its own, the composite only once they pass ----
    "write-engine": dict(
        exercises="base of a rewrite, R1: the whole engine written over a stub (FILE allowed; long enough to need continuations if written verbosely)",
        calc=STUB_ENGINE, diagnostic=True,
        context="src/calc.cpp is a stub: evaluate() only throws. Write the calculator engine."),
    "add-functions": dict(
        exercises="base of a rewrite, R2: a design grown function by function (four missing functions added with SYMBOL; the file does not compile until they are)",
        calc=CALC.replace(FACTORIAL, "").replace(TRIG_HELPERS, "").replace(CALL, ""), diagnostic=True,
        context="src/calc.cpp is a calculator engine with four functions missing: factorial, toRad, fromRad and call (it does not build). Add them."),
    "replace-function": dict(
        exercises="base of a rewrite, R3: one function whose approach is wrong, redesigned at a modest size (arithmetic only: scope rules and escalation)",
        calc=NAIVE, only_given=["tests/arith.test.cpp"], contract=ARITH_CONTRACT, diagnostic=True,
        context="src/calc.cpp is an arithmetic evaluator. Repair it for the arithmetic contract below."),
    # ---- scaling: more independent defects (each a prefix of the next); the gate reports defects fixed per turn ----
    **{f"bugs-{n}": dict(
        exercises=f"scaling: {n} independent defects, each breaking a different test (defects fixed per turn)",
        calc=with_defects(n), defects=n,
        context="src/calc.cpp is a calculator engine. Several of the provided tests fail.") for n in (3, 6, 10)},
    # ---- a repeated class of defect: one pattern at many sites (a batch fix, or many small ones) ----
    "repeat-trig": dict(
        exercises="a repeated class: the same defect at six sites (every trig call lost its degree conversion)",
        calc=repeat_trig(CALC), given=["tests/trig.test.cpp"],
        context="src/calc.cpp is a calculator engine. Some of the provided tests fail."),
    "repeat-rename": dict(
        exercises="a repeated class: about twenty calls to a function by a name that does not exist (it does not compile)",
        calc=repeat_rename(CALC),
        context="src/calc.cpp is a calculator engine. It does not build."),
    "two-files": dict(
        exercises="defects in two files: edits across files in one reply",
        calc=sub(CALC, "v = std::fmod(v, d); }", "v = v / d; }"),
        main=sub(MAIN, "if (a == \"--degrees\")", "if (a == \"--degree\")"),
        files=["src/calc.cpp", "src/main.cpp"],
        context="src/calc.cpp (the engine) and src/main.cpp (the app) nearly work: some of the provided tests fail, and there is a defect in each file. Fix each with the smallest edit."),
}

# Staged composites are calibration cases until live evidence justifies promotion.
edit_mixed = repeat_trig(CASES["fix-distant"]["calc"])
for index in (0, 1, 3, 4, 5):
    edit_mixed = sub(edit_mixed, *DEFECT_BANK[index])
build_mixed = sub(CASES["compile-and-logic"]["calc"], NUMBER,
                  NUMBER + NUMBER.replace("std::strtod", "std::strtol").replace("(start, &end)", "(start, &end, 10)"))
build_mixed = sub(build_mixed, "        const double base = postfix();",
                  "        const double base = postfix();\n        const double unused = base;")
CASES.update({
    "edit-mix": dict(exercises="independent local, grammar and repeated conversion defects", diagnostic=True,
                     calc=edit_mixed, given=["tests/trig.test.cpp"],
                     context="The calculator builds and several distinct behaviors fail. Repair the individual causes."),
    "build-mix": dict(exercises="syntax, duplicate definitions, unresolved names, a warning and a logic defect", diagnostic=True,
                      calc=repeat_rename(build_mixed), context="The calculator does not build. Repair it until all tests pass."),
    "files-mix": dict(exercises="a conflict in the evaluator alongside defects across two source files", diagnostic=True,
                      calc=sub(CASES["merge-conflict"]["calc"], *DEFECT_BANK[1]), main=CASES["two-files"]["main"],
                      files=["src/calc.cpp", "src/main.cpp"], context="Repair the engine and command-line app. Begin with the broken build."),
})

# The smoke set: one fast case for each kind of edit the worker must make (a snippet, several edits in one reply, an insertion, a broken build, SYMBOL,
# FILE, conflict markers, two files), one run each: about 8 minutes on the 35B (tier 0 of 1e74bd4: the seven measured each under 90 s a run; two-files not yet run) against about 3.5 hours for the
# gate. For iterating on a change; it never stands in for the gate (gate_t0.py --set smoke writes smoke.md, not gate.md).
SMOKE = ["fix-op", "fix-two-bugs", "fix-missing-check", "compile-typo", "write-function", "write-file", "merge-conflict", "two-files"]
assert set(SMOKE) <= set(CASES)
# Named sets for run_t0.sh @<set> and gate_t0.py --set <set>. The diagnostic cases are not part of the gate: they localise a failure of a gate case.
SETS = {"smoke": SMOKE, "rewrite": ["write-engine", "add-functions", "replace-function"]}
GATE = [n for n in CASES if not CASES[n].get("diagnostic")]

# The case tree (docs/T0_LAYERS.md, step 1 of its migration: data only, no plan, key or gate changes). A case's children are the base requirements that
# explain it: when it fails, they localise the cause (the hand decomposition of wrong-approach on 2026-11-18 is the first branch). The layer is derived: 0 for
# a leaf, else 1 + its highest child. The composites the design proposes (edit-mix, build-mix, files-mix, skeleton-fill) are backlog.
REQUIRES = {
    "wrong-approach": ["replace-function", "write-engine", "add-functions"],
    "skeleton": ["write-engine", "add-functions"],
    "add-functions": ["write-function", "write-feature"],
    "write-file": ["half-written", "write-engine"],
    "half-written": ["write-function"],
    "bugs-10": ["bugs-6"], "bugs-6": ["bugs-3"],
    "bugs-3": ["fix-op", "fix-boundary", "fix-missing-check"],
    "fix-many": ["fix-op", "fix-boundary", "fix-missing-check"],
    "fix-two-bugs": ["fix-op", "fix-boundary", "fix-missing-check"],
    "compile-and-logic": ["compile-typo", "fix-op"],
    "undeclared-many": ["undeclared-member"],
    "merge-conflict": ["fix-op"], "two-files": ["fix-op"],
    "edit-mix": ["bugs-10", "fix-distant", "repeat-trig", "fix-many", "fix-two-bugs"],
    "build-mix": ["compile-and-logic", "compile-warning", "duplicate", "repeat-rename"],
    "files-mix": ["two-files", "merge-conflict"],
}
assert all(p in CASES and set(c) <= set(CASES) for p, c in REQUIRES.items()), "REQUIRES names a case that does not exist"
LAYER = {}


def layer_of(name, path=()):
    assert name not in path, f"REQUIRES has a cycle through {name}"
    if name not in LAYER: LAYER[name] = 1 + max((layer_of(c, path + (name,)) for c in REQUIRES.get(name, [])), default=-1)
    return LAYER[name]


for _n in CASES: layer_of(_n)
TOPS = [n for n in CASES if not any(n in c for c in REQUIRES.values())]
SETS.update(quick=TOPS, tops=TOPS, calibration=["edit-mix", "build-mix", "files-mix"], release=list(CASES))


def tree_lines(name, depth=0, seen=None):
    """The case tree under `name`, one line per node (a node already shown is named, not expanded again)."""
    seen = set() if seen is None else seen
    lines = [f"{'  ' * depth}{name} (layer {LAYER[name]}){' ...' if name in seen and REQUIRES.get(name) else ''}"]
    if name not in seen:
        seen.add(name)
        for c in REQUIRES.get(name, []): lines += tree_lines(c, depth + 1, seen)
    return lines


def plan_for(name):
    c = CASES[name]
    files = c.get("files", ["src/calc.cpp"])
    given = c["only_given"] if "only_given" in c else ((["tests/eval.test.cpp"] if "src/calc.cpp" in files or c.get("persona") == "tester" else []) +
                                                       (["tests/cli.test.cpp"] if "src/main.cpp" in files else []) + c.get("given", []))
    tests = c.get("tests", given)
    base = json.loads((ROOT / "validation/tiers/t1-plan.json").read_text(encoding="utf-8"))
    item = base["items"][0]
    contract = c.get("contract", item["card"]["contract"])
    if files == ["src/calc.cpp"]: contract = contract.split("\nAPP:")[0]
    item.update({"id": "app", "title": f"Tier 0 {name}", "goal": "Make every provided test pass" if c.get("persona") != "tester" else "Write passing tests",
                 "complexity": 1, "files": files, "persona": c.get("persona", "developer")})
    item["card"].update({
        "context": c["context"] + " Do not rewrite a file that is mostly right.",
        "contract": contract,
        "acceptance": [f"every test in {' and '.join(tests)} passes", "C++20, standard library only, no warnings"],
        "include": given,
        "notes": ["Output only " + " and ".join(files)],
    })
    count = sum(ref(t).count("\nTEST(") + ref(t).startswith("TEST(") if (REF / t).exists() else GIVEN_TEXT.get(t, "").count("TEST(") for t in tests)
    item["gate"].update({"tests": tests, "minTests": c.get("minTests", count), "mustFailOnStub": False})
    plan = dict(base)
    plan.update({
        "task": f"tier 0 {name}: {c['exercises']}",
        "project": f"t0-{name}",
        "reasons": [f"tier 0 spike '{name}': one worker on the canvas; exercises {c['exercises']}"],
        "scaffold": [{"path": "include/calc/calc.hpp", "content": HEADER},
                     {"path": "src/calc.cpp", "content": c.get("calc", CALC)},
                     {"path": "src/main.cpp", "content": c.get("main", MAIN)}],
        "given": [{"path": t, "content": GIVEN_TEXT.get(t) or ref(t)} for t in given],
        "items": [item],
    })
    return plan


if __name__ == "__main__":
    args = sys.argv[2:]
    if args[:2] == ["--list", "tree"]:
        for t in sorted(TOPS, key=lambda n: -LAYER[n]): print("\n".join(tree_lines(t)))
        sys.exit(0)
    if args[:1] == ["--list"]:
        for n in (SETS[args[1]] if args[1:2] else GATE): print(f"{n:18} {CASES[n]['exercises']}")
        sys.exit(0)
    if args[:1] == ["--check"]:
        p = plan_for(args[1])
        for f in p["scaffold"] + p["given"]:
            out = pathlib.Path(args[2]) / f["path"]
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(f["content"], encoding="utf-8")
        print(json.dumps({"files": p["items"][0]["files"], "tests": p["items"][0]["gate"]["tests"]}))
        sys.exit(0)
    for n in args or list(CASES):
        out = pathlib.Path(os.environ.get("T0_OUT", ROOT / "out/runs/t0")) / n / "plan.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(plan_for(n), indent=1), encoding="utf-8")
        print(out)
