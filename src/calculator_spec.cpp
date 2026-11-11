#include "sub0squad/calculator_spec.hpp"

namespace sub0squad {

namespace {

const char* kLanguage =
    "Operators: + - * / % ^ (right associative; binds tighter than unary minus, so -2^2 is -4), postfix ! (factorial), parentheses, unary + and -. "
    "Numbers: 12, 1.5, .5, 1.5e2. Constants: pi e tau. One-argument functions: sin cos tan asin acos atan sinh cosh tanh ln log (base 10) log2 sqrt cbrt abs exp floor ceil round fact. "
    "Two-argument functions: pow min max. degrees=true makes sin cos tan take degrees and asin acos atan return degrees. ";

const char* kErrors =
    "Invalid input raises the error: division or modulo by zero, unknown names or functions, wrong argument count, sqrt of a negative, unbalanced parentheses, a trailing operator, unexpected characters.";

const char* kApp =
    "The app takes an expression as its arguments (joined with spaces) or, with none, one line from standard input. It prints the result with 12 significant digits (%.12g) and a newline and exits 0. "
    "On invalid input it prints 'Error: ' and the message to standard error and exits non-zero. --degrees sets degrees mode. --screen prints a fixed frame instead: a line of '+' then 38 '-' then '+', "
    "a line '| ' + the expression padded to 36 characters + ' |', a line '| = ' + the result padded to 34 characters + ' |' (an error shows as 'Error: message'), and the first rule line again. ";

}  // namespace

Plan calculatorPlan(const CalculatorSurface& s)
{
    Plan p;
    p.task = s.name;
    p.harness = s.harness;
    p.project = s.project;
    p.reasons = {"task is a program with testable logic: the engine and the app are separate deliverables with a clear interface",
                 "test-first: a tester item writes tests from the contract, the developer item is graded by tests it did not write",
                 "decision: split into 4 items (2 testers, 2 developers)"};

    WorkItem et;
    et.id = "eval-tests"; et.title = "Engine tests"; et.persona = "tester"; et.complexity = 2;
    et.goal = "Write tests for the calculator engine before it exists";
    et.files = {s.engineTests};
    et.card.context = "A developer will implement the engine against your tests, so they must be correct and specific.";
    et.card.contract = "The engine: " + s.engineApi + ". " + kLanguage + kErrors + " Here invalid input raises " + s.errorKind + ".";
    et.card.acceptance = {s.testRules,
                          "at least 10 test cases: operators, precedence, ^ associativity, factorial, decimals, constants, radians and degrees, logs, two-argument functions, error cases",
                          "floating point results are compared with a tolerance of 1e-9, never with exact equality", "tests do not depend on each other"};
    et.card.notes = {"Output only " + s.engineTests};
    et.gate = {s.gateKind, {s.engineTests}, 8, true};

    WorkItem en;
    en.id = "engine"; en.title = "Expression engine"; en.persona = "developer"; en.complexity = 3;
    en.goal = "Implement the engine so the provided tests pass";
    en.files = {s.engine}; en.deps = {"eval-tests"};
    en.card.context = "Replace the stub " + s.engine + " with a real implementation; the tests below are the specification.";
    en.card.contract = "Implement " + s.engineApi + ". " + kLanguage + "Raise " + s.errorKind + " with a clear message on any invalid input (" + kErrors + ")";
    en.card.acceptance = {"every test in " + s.engineTests + " passes", s.style};
    en.card.include = {s.engineTests};
    en.card.notes = {s.engineHint, "Output only " + s.engine};
    en.gate = {s.gateKind, {s.engineTests}, 8, false};

    WorkItem ct;
    ct.id = "cli-tests"; ct.title = "App tests"; ct.persona = "tester"; ct.complexity = 2;
    ct.goal = "Write tests for the calculator command-line app by running it";
    ct.files = {s.cliTests};
    ct.card.context = "A developer will build the app against your tests. The tests run the real program.";
    ct.card.contract = std::string(kApp) + s.runApiNote;
    ct.card.acceptance = {s.testRules, "at least 5 tests: a result, joined arguments, floating point noise (0.1+0.2 prints 0.3), --degrees, an error message with a non-zero exit, and the --screen frame",
                          "each test runs the program itself"};
    ct.card.notes = {"Output only " + s.cliTests};
    ct.gate = {s.gateKind, {s.cliTests}, 5, true};

    WorkItem ap;
    ap.id = "app"; ap.title = "Command-line app"; ap.persona = "developer"; ap.complexity = 2;
    ap.goal = "Write the command-line app so the provided tests pass";
    ap.files = {s.app}; ap.deps = {"cli-tests", "engine"};
    ap.card.context = "Replace the stub " + s.app + "; the tests below are the specification.";
    ap.card.contract = s.appEntry + ". " + kApp + "Evaluate with the engine (" + s.engineApi + ").";
    ap.card.acceptance = {"every test in " + s.cliTests + " passes", s.style};
    ap.card.include = {s.cliTests};
    ap.card.notes = {"Output only " + s.app};
    ap.gate = {s.gateKind, {s.cliTests}, 5, false};

    p.items = {et, en, ct, ap};
    return p;
}

}  // namespace sub0squad
