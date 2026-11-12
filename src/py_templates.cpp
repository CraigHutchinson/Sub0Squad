#include "sub0squad/harness_py.hpp"

#include "sub0squad/calculator_spec.hpp"

#include <map>

namespace sub0squad {

namespace {

const char* kInit = "";

const char* kStubEngine = R"PY("""Contract: evaluate(expr, degrees=False) -> float; raises ValueError for any invalid input."""


def evaluate(expr, degrees=False):
    # A stub must not raise: a test that expects an exception would pass against it for free (the tester gate needs most tests to FAIL on the stub).
    return None
)PY";

const char* kStubMain = R"PY(import sys

print("not implemented")
sys.exit(2)
)PY";

const char* kTapRunner = R"PY(# Runs one unittest file and prints a TAP report (the same shape the other harnesses print).
import importlib.util
import os
import sys
import traceback
import unittest
import unittest.util

# Show whole values in assertion messages (the default shortens them to "[81 chars]", which hides what the code gave).
unittest.util._MAX_LENGTH = 600


class TapResult(unittest.TestResult):
    def __init__(self, path):
        super().__init__()
        self.path, self.n, self.passed, self.failed = path, 0, 0, 0

    def _name(self, test):
        return ".".join(test.id().split(".")[-2:])

    def addSuccess(self, test):
        super().addSuccess(test)
        self.n += 1
        self.passed += 1
        print("ok %d - %s" % (self.n, self._name(test)))

    def _bad(self, test, err):
        self.n += 1
        self.failed += 1
        exc = err[1]
        text = (str(exc).splitlines() or [type(exc).__name__])[0]
        print("not ok %d - %s\n  ---\n  error: %s: %s" % (self.n, self._name(test), type(exc).__name__, text))
        if isinstance(exc, AssertionError) and " != " in text:
            actual, expected = text.split(" != ", 1)
            print("  expected: %s\n  actual: %s" % (expected.split(" within ")[0], actual))
        frames = [f for f in traceback.extract_tb(err[2]) if os.path.basename(f.filename) == os.path.basename(self.path)]
        if frames:
            print("  location: '%s:%d'" % (self.path, frames[-1].lineno))
        print("  ...")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._bad(test, err)

    def addError(self, test, err):
        super().addError(test, err)
        self._bad(test, err)


def main(path):
    root = os.getcwd()
    sys.path[:0] = [root, os.path.join(root, "tests")]
    spec = importlib.util.spec_from_file_location(os.path.basename(path)[:-3], path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except BaseException:
        traceback.print_exc()
        return 1
    suite = unittest.defaultTestLoader.loadTestsFromModule(module)
    print("TAP version 13")
    result = TapResult(path)
    suite.run(result)
    print("1..%d\n# tests %d\n# pass %d\n# fail %d" % (result.n, result.n, result.passed, result.failed))
    return 0 if result.failed == 0 and result.n > 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
)PY";

const char* kCliHelper = R"PY(import subprocess
import sys


def run(*args, stdin=None):
    """Run the app (python -m calc ARGS) from the project root; returns (stdout and stderr together, exit code)."""
    p = subprocess.run([sys.executable, "-B", "-m", "calc", *args], input=stdin, capture_output=True, text=True, timeout=30)
    return p.stdout + p.stderr, p.returncode
)PY";

// ---- reference deliverables -----------------------------------------------------------------------------------------------------------
const char* kEngine = R"PY(import math


class _Parser:
    def __init__(self, text, degrees):
        self.s, self.degrees, self.pos = text, degrees, 0

    def run(self):
        v = self.expr()
        self.skip()
        if self.pos != len(self.s):
            raise ValueError("unexpected '%s'" % self.s[self.pos])
        return v

    def skip(self):
        while self.pos < len(self.s) and self.s[self.pos].isspace():
            self.pos += 1

    def eat(self, c):
        self.skip()
        if self.pos < len(self.s) and self.s[self.pos] == c:
            self.pos += 1
            return True
        return False

    def expr(self):
        v = self.term()
        while True:
            if self.eat("+"):
                v += self.term()
            elif self.eat("-"):
                v -= self.term()
            else:
                return v

    def term(self):
        v = self.unary()
        while True:
            if self.eat("*"):
                v *= self.unary()
            elif self.eat("/"):
                d = self.unary()
                if d == 0:
                    raise ValueError("division by zero")
                v /= d
            elif self.eat("%"):
                d = self.unary()
                if d == 0:
                    raise ValueError("modulo by zero")
                v = math.fmod(v, d)
            else:
                return v

    # Unary minus binds looser than ^, so -2^2 is -4; the exponent may itself be unary and ^ is right associative.
    def unary(self):
        if self.eat("-"):
            return -self.unary()
        if self.eat("+"):
            return self.unary()
        base = self.postfix()
        if self.eat("^"):
            return math.pow(base, self.unary())
        return base

    def postfix(self):
        v = self.primary()
        while self.eat("!"):
            v = self.factorial(v)
        return v

    @staticmethod
    def factorial(v):
        if v < 0 or v != math.floor(v) or v > 170:
            raise ValueError("factorial needs an integer from 0 to 170")
        return float(math.factorial(int(v)))

    def primary(self):
        self.skip()
        if self.pos >= len(self.s):
            raise ValueError("unexpected end of expression")
        if self.eat("("):
            v = self.expr()
            if not self.eat(")"):
                raise ValueError("missing closing parenthesis")
            return v
        c = self.s[self.pos]
        if c.isdigit() or c == ".":
            return self.number()
        if c.isalpha():
            return self.name()
        raise ValueError("unexpected '%s'" % c)

    def number(self):
        start = self.pos
        while self.pos < len(self.s) and (self.s[self.pos].isdigit() or self.s[self.pos] == "."):
            self.pos += 1
        if self.pos < len(self.s) and self.s[self.pos] in "eE" and self.pos + 1 < len(self.s) and (self.s[self.pos + 1].isdigit() or self.s[self.pos + 1] in "+-"):
            self.pos += 2
            while self.pos < len(self.s) and self.s[self.pos].isdigit():
                self.pos += 1
        try:
            return float(self.s[start:self.pos])
        except ValueError:
            raise ValueError("bad number '%s'" % self.s[start:self.pos])

    def name(self):
        start = self.pos
        while self.pos < len(self.s) and (self.s[self.pos].isalnum() or self.s[self.pos] == "_"):
            self.pos += 1
        ident = self.s[start:self.pos]
        consts = {"pi": math.pi, "e": math.e, "tau": math.tau}
        self.skip()
        if ident in consts and not (self.pos < len(self.s) and self.s[self.pos] == "("):
            return consts[ident]
        if not self.eat("("):
            raise ValueError("unknown name '%s'" % ident)
        args = []
        if not self.eat(")"):
            args.append(self.expr())
            while self.eat(","):
                args.append(self.expr())
            if not self.eat(")"):
                raise ValueError("missing closing parenthesis")
        return self.call(ident, args)

    def call(self, f, a):
        if f in ("pow", "min", "max"):
            if len(a) != 2:
                raise ValueError(f + " takes two arguments")
            return math.pow(a[0], a[1]) if f == "pow" else min(a) if f == "min" else max(a)
        if len(a) != 1:
            raise ValueError(f + " takes one argument")
        x = a[0]
        rad = (lambda v: math.radians(v)) if self.degrees else (lambda v: v)
        deg = (lambda v: math.degrees(v)) if self.degrees else (lambda v: v)
        table = {
            "sin": lambda: math.sin(rad(x)), "cos": lambda: math.cos(rad(x)), "tan": lambda: math.tan(rad(x)),
            "asin": lambda: deg(math.asin(x)), "acos": lambda: deg(math.acos(x)), "atan": lambda: deg(math.atan(x)),
            "sinh": lambda: math.sinh(x), "cosh": lambda: math.cosh(x), "tanh": lambda: math.tanh(x),
            "ln": lambda: math.log(x), "log": lambda: math.log10(x), "log2": lambda: math.log2(x),
            "sqrt": lambda: math.sqrt(x), "cbrt": lambda: math.cbrt(x), "abs": lambda: abs(x),
            "exp": lambda: math.exp(x), "floor": lambda: float(math.floor(x)), "ceil": lambda: float(math.ceil(x)),
            "round": lambda: float(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1), "fact": lambda: self.factorial(x),
        }
        if f not in table:
            raise ValueError("unknown function '%s'" % f)
        return table[f]()


def evaluate(expr, degrees=False):
    try:
        return _Parser(expr, degrees).run()
    except (ArithmeticError, RecursionError) as e:
        raise ValueError(str(e))
)PY";

const char* kEvalTests = R"PY(import unittest

from calc.engine import evaluate


class EvalTests(unittest.TestCase):
    def near(self, expr, want, degrees=False):
        self.assertAlmostEqual(evaluate(expr, degrees), want, places=9)

    def test_addition_and_subtraction(self):
        self.near("1+2", 3)
        self.near("10-4-3", 3)

    def test_multiplication_and_division(self):
        self.near("6*7", 42)
        self.near("7/2", 3.5)

    def test_precedence(self):
        self.near("2+3*4", 14)
        self.near("(2+3)*4", 20)

    def test_power_is_right_associative(self):
        self.near("2^3^2", 512)

    def test_unary_minus_binds_looser_than_power(self):
        self.near("-2^2", -4)
        self.near("2^-1", 0.5)

    def test_modulo(self):
        self.near("10%3", 1)

    def test_factorial(self):
        self.near("5!", 120)
        self.near("0!", 1)

    def test_decimals(self):
        self.near("1.5+.5", 2)
        self.near("1.5e2", 150)

    def test_constants(self):
        self.near("pi", 3.14159265358979)
        self.near("e", 2.71828182845905)
        self.near("tau", 6.28318530717959)

    def test_radians(self):
        self.near("sin(0)", 0)
        self.near("cos(pi)", -1)

    def test_degrees(self):
        self.near("sin(90)", 1, True)
        self.near("asin(1)", 90, True)

    def test_logarithms(self):
        self.near("ln(e)", 1)
        self.near("log(1000)", 3)
        self.near("log2(8)", 3)

    def test_roots_and_rounding(self):
        self.near("sqrt(16)", 4)
        self.near("cbrt(27)", 3)
        self.near("floor(2.7)", 2)
        self.near("round(2.5)", 3)

    def test_two_argument_functions(self):
        self.near("pow(2,10)", 1024)
        self.near("min(3,4)", 3)
        self.near("max(3,4)", 4)

    def test_division_by_zero_raises(self):
        with self.assertRaises(ValueError):
            evaluate("1/0")
        with self.assertRaises(ValueError):
            evaluate("1%0")

    def test_invalid_input_raises(self):
        for bad in ["foo", "sqrt(-1)", "(1+2", "1+", "2 $ 3", "sin(1,2)"]:
            with self.assertRaises(ValueError, msg=bad):
                evaluate(bad)
)PY";

const char* kMain = R"PY(import sys

from calc.engine import evaluate


def screen(expr, result):
    rule = "+" + "-" * 38 + "+"
    row = lambda text: "| " + text[:36].ljust(36) + " |"
    return "\n".join([rule, row(expr), row("= " + result), rule]) + "\n"


def main(argv):
    degrees = "--degrees" in argv
    framed = "--screen" in argv
    expr = " ".join(a for a in argv if a not in ("--degrees", "--screen"))
    if not expr:
        expr = sys.stdin.readline().rstrip("\n")
    try:
        result = format(evaluate(expr, degrees), ".12g")
    except Exception as e:
        if framed:
            sys.stdout.write(screen(expr, "Error: " + str(e)))
        else:
            sys.stderr.write("Error: %s\n" % e)
        return 1
    sys.stdout.write(screen(expr, result) if framed else result + "\n")
    return 0


sys.exit(main(sys.argv[1:]))
)PY";

const char* kCliTests = R"PY(import unittest

from helpers.cli import run


class CliTests(unittest.TestCase):
    def test_prints_the_result(self):
        self.assertEqual(run("2^3^2"), ("512\n", 0))

    def test_joins_arguments(self):
        self.assertEqual(run("1", "+", "2"), ("3\n", 0))

    def test_rounds_floating_noise(self):
        self.assertEqual(run("0.1+0.2"), ("0.3\n", 0))

    def test_degrees_flag(self):
        self.assertEqual(run("--degrees", "sin(90)"), ("1\n", 0))

    def test_error_message_and_status(self):
        out, code = run("1/0")
        self.assertTrue(out.startswith("Error:"))
        self.assertNotEqual(code, 0)

    def test_screen_frame(self):
        expected = ("+--------------------------------------+\n| 2+2                                  |\n"
                    "| = 4                                  |\n+--------------------------------------+\n")
        self.assertEqual(run("--screen", "2+2"), (expected, 0))
)PY";

struct Bug { const char* from; const char* to; };
const std::map<std::string, Bug>& bugs()
{
    static const std::map<std::string, Bug> m = {
        {"calc/engine.py", {"v = math.fmod(v, d)", "v = v / d"}},
        {"calc/__main__.py", {"\".12g\"", "\".2f\""}},
    };
    return m;
}

const CalculatorSurface& pySurface()
{
    static const CalculatorSurface s = {
        "calculator-py", "py-unittest", "sci-calc-py", "unittest", "tests/eval_test.py", "calc/engine.py", "tests/cli_test.py", "calc/__main__.py",
        "evaluate(expr, degrees=False) -> float in calc/engine.py (a plain module-level function)",
        "ValueError",
        "a unittest file: imports `from calc.engine import evaluate` (engine tests), defines unittest.TestCase subclasses, no main and no unittest.main()",
        "In a test, `from helpers.cli import run` gives run(*args, stdin=None) -> (output, exit code), output being stdout and stderr together; it runs `python -m calc ARGS`.",
        "A module run as `python -m calc` (calc/__main__.py), with sys.exit(main(sys.argv[1:])) at the bottom",
        "Python 3, standard library only",
        "A small recursive descent parser works well: expression -> term -> unary -> postfix -> primary"};
    return s;
}

}  // namespace

const std::vector<HarnessFile>& pyScaffold()
{
    static const std::vector<HarnessFile> files = {
        {"calc/__init__.py", kInit}, {"calc/engine.py", kStubEngine}, {"calc/__main__.py", kStubMain},
        {"tests/helpers/tap.py", kTapRunner}, {"tests/helpers/cli.py", kCliHelper},
    };
    return files;
}

std::size_t pyScaffoldBytes()
{
    std::size_t n = 0;
    for (const auto& f : pyScaffold()) n += f.content.size();
    return n;
}

std::string pyHarnessFacts()
{
    return "- Project type `py-unittest`: Python 3, standard library only, tests are unittest.TestCase classes in tests/NAME_test.py, run one file at a time by tests/helpers/tap.py (prints TAP with expected and actual).\n"
           "- The scaffold every item's workspace starts from: the package calc/ (__init__.py), tests/helpers/tap.py and tests/helpers/cli.py (run(*args) runs `python -m calc`), and STUBS of calc/engine.py (returns None and never raises) and calc/__main__.py.\n"
           "- A worker replies with whole files as `=== FILE: path === ... === END FILE ===`; anything outside the item's allowed files is rejected.\n"
           "- Cheapest checks first: the allowlist, then byte-compiling each written file (file, line, message), then the tests.\n"
           "- Tester gate: the tests must load, number at least the item's minimum, and at least 70 % must FAIL against the stubs. Developer gate: every test passes.\n"
           "- The integrator runs every test in Python's development mode with warnings as errors, runs the app (a smoke expression and the --screen frame) and stores the frame as capture.txt.\n";
}

std::string pyHarnessBrief()
{
    return "Already in the workspace (do not recreate): the package calc/ with stubs of calc/engine.py (evaluate(expr, degrees=False) returns None and never raises) and calc/__main__.py, "
           "tests/helpers/tap.py (the runner) and tests/helpers/cli.py (run(*args, stdin=None) -> (output, exit code) runs `python -m calc`). Python 3, standard library only. "
           "A test file imports `from calc.engine import evaluate` and defines unittest.TestCase classes (no unittest.main()).";
}

std::string pyMockFile(const std::string& path, bool buggy)
{
    std::string text;
    if (path == "calc/engine.py") text = kEngine;
    else if (path == "tests/eval_test.py") text = kEvalTests;
    else if (path == "tests/cli_test.py") text = kCliTests;
    else if (path == "calc/__main__.py") text = kMain;
    else return {};
    if (buggy) {
        auto b = bugs().find(path);
        if (b != bugs().end()) {
            const auto pos = text.find(b->second.from);
            if (pos == std::string::npos) return {};
            text.replace(pos, std::string{b->second.from}.size(), b->second.to);
        }
    }
    return text;
}

std::string pyMockBugTarget(const std::string& item)
{
    static const std::map<std::string, std::string> m = {{"dev-engine", "calc/engine.py"}, {"dev-app", "calc/__main__.py"}};
    auto it = m.find(item);
    return it == m.end() ? std::string{} : it->second;
}

bool pyClaimsTask(const std::string& t)
{
    for (const char* w : {"python", "py script", "pip", "unittest"}) if (t.find(w) != std::string::npos) return true;
    return false;
}

std::string pyBlueprintJson(const std::string& name)
{
    return name == "calculator-py" ? calculatorPlan(pySurface()).toJson() : std::string{};
}

}  // namespace sub0squad
