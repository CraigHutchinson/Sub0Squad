#include "sub0squad/harness_cpp.hpp"

#include "sub0squad/calculator_spec.hpp"

#include <map>

namespace sub0squad {

namespace {

// ---- scaffold: what the harness supplies so an agent writes only deliverable files ---------------------------------------------------
const char* kCMake = R"CPP(cmake_minimum_required(VERSION 3.20)
project(calc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
option(SANITIZE "build with AddressSanitizer and UBSan" OFF)
if(SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
enable_testing()

# The library is every src/*.cpp except main.cpp; the app is main.cpp; each tests/<name>.test.cpp is its own executable test_<name>.
file(GLOB LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(calc STATIC ${LIB_SOURCES})
target_include_directories(calc PUBLIC include src)
add_executable(calc_cli src/main.cpp)
target_link_libraries(calc_cli calc)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.test.cpp)
foreach(t ${TEST_SOURCES})
  get_filename_component(n ${t} NAME_WE)
  add_executable(test_${n} ${t} tests/helpers/main.cpp)
  target_include_directories(test_${n} PRIVATE tests)
  target_link_libraries(test_${n} calc)
  add_dependencies(test_${n} calc_cli)
  target_compile_definitions(test_${n} PRIVATE CALC_CLI_PATH="$<TARGET_FILE:calc_cli>")
  add_test(NAME ${n} COMMAND test_${n})
endforeach()
)CPP";

const char* kHeader = R"CPP(#pragma once
#include <string>

namespace calc {

/// Evaluate an arithmetic expression. `degrees` makes sin, cos and tan take degrees and asin, acos and atan return degrees.
/// Throws std::runtime_error with a clear message for any invalid input.
double evaluate(const std::string& expr, bool degrees = false);

}  // namespace calc
)CPP";

const char* kStubCalc = R"CPP(#include "calc/calc.hpp"

#include <stdexcept>

namespace calc {

double evaluate(const std::string&, bool)
{
    throw std::runtime_error("not implemented");
}

}  // namespace calc
)CPP";

const char* kStubMain = R"CPP(#include <cstdio>

int main()
{
    std::puts("not implemented");
    return 2;
}
)CPP";

const char* kCheck = R"CPP(#pragma once
// A tiny test framework. TEST(name) { ... } registers a case; CHECK, CHECK_EQ, CHECK_NEAR and CHECK_THROWS fail it with the expected and the actual value.
// The runner (tests/helpers/main.cpp) prints a TAP report and exits non-zero when a case fails.
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };

struct Failure { std::string message, expected, actual, file; int line; };

template <class T> std::string show(const T& v) { std::ostringstream o; o << v; return o.str(); }

}  // namespace check

#define TEST(name) static void name(); static check::Registrar registrar_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) throw check::Failure{"CHECK failed: " #cond, "true", "false", __FILE__, __LINE__}; } while (0)
#define CHECK_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (!(va_ == vb_)) throw check::Failure{"CHECK_EQ failed: " #a " == " #b, check::show(vb_), check::show(va_), __FILE__, __LINE__}; } while (0)
#define CHECK_NEAR(a, b, tol) do { const double va_ = (a); const double vb_ = (b); if (!(std::fabs(va_ - vb_) <= (tol))) throw check::Failure{"CHECK_NEAR failed: " #a " ~ " #b, check::show(vb_), check::show(va_), __FILE__, __LINE__}; } while (0)
#define CHECK_THROWS(expr) do { bool threw_ = false; try { (void)(expr); } catch (const std::exception&) { threw_ = true; } if (!threw_) throw check::Failure{"CHECK_THROWS failed: " #expr " did not throw", "an exception", "no exception", __FILE__, __LINE__}; } while (0)
)CPP";

const char* kAppHelper = R"CPP(#pragma once
// Runs the app (calc_cli) and returns what it printed. `args` is the command line after the program name, written as in a shell
// (quote an argument that has spaces). The program's path is supplied by the build; do not use CALC_CLI_PATH yourself.
#include <cstdio>
#include <string>
#include <utility>

namespace app {

struct Result { std::string output; int code; };  // stdout and stderr together; the exit code (0 = success)

inline Result run(const std::string& args)
{
    const std::string cmd = std::string(CALC_CLI_PATH) + " " + args + " 2>&1";
    Result r{"", -1};
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof buf, p)) r.output += buf;
        const int st = pclose(p);
        r.code = st == -1 ? -1 : (st >> 8) & 0xff;
    }
    return r;
}

}  // namespace app
)CPP";

const char* kRunner = R"CPP(#include "helpers/check.hpp"

#include <cstdio>
#include <exception>
#include <string>

// A TAP value must stay on one line: a multi-line string is shown with its newlines escaped.
static std::string one(const std::string& s)
{
    std::string r;
    for (const char c : s) { if (c == '\n') r += "\\n"; else r += c; }
    return r;
}

int main()
{
    int pass = 0, fail = 0, n = 0;
    std::printf("TAP version 13\n");
    for (const auto& c : check::registry()) {
        ++n;
        try {
            c.fn();
            ++pass;
            std::printf("ok %d - %s\n", n, c.name.c_str());
        } catch (const check::Failure& f) {
            ++fail;
            std::printf("not ok %d - %s\n  ---\n  error: %s\n  expected: %s\n  actual: %s\n  location: '%s:%d'\n  ...\n", n, c.name.c_str(), one(f.message).c_str(), one(f.expected).c_str(), one(f.actual).c_str(), f.file.c_str(), f.line);
        } catch (const std::exception& e) {
            ++fail;
            std::printf("not ok %d - %s\n  ---\n  error: unexpected exception: %s\n  ...\n", n, c.name.c_str(), e.what());
        } catch (...) {
            ++fail;
            std::printf("not ok %d - %s\n  ---\n  error: unexpected exception of unknown type\n  ...\n", n, c.name.c_str());
        }
    }
    std::printf("1..%d\n# tests %d\n# pass %d\n# fail %d\n", n, n, pass, fail);
    return fail == 0 ? 0 : 1;
}
)CPP";

// ---- reference deliverables (the mock agent's correct files, and the test oracle) -------------------------------------------------------
const char* kCalc = R"CPP(#include "calc/calc.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace calc {

namespace {

class Parser
{
public:
    Parser(const std::string& s, bool degrees) : s_(s), degrees_(degrees) {}

    double run()
    {
        const double v = expr();
        skip();
        if (pos_ != s_.size()) fail(std::string("unexpected '") + s_[pos_] + "'");
        return v;
    }

private:
    [[noreturn]] static void fail(const std::string& m) { throw std::runtime_error(m); }
    void skip() { while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
    bool eat(char c) { skip(); if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; } return false; }

    double expr()
    {
        double v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }

    double term()
    {
        double v = unary();
        for (;;) {
            if (eat('*')) v *= unary();
            else if (eat('/')) { const double d = unary(); if (d == 0) fail("division by zero"); v /= d; }
            else if (eat('%')) { const double d = unary(); if (d == 0) fail("modulo by zero"); v = std::fmod(v, d); }
            else return v;
        }
    }

    // Unary minus binds looser than ^, so -2^2 is -4; the exponent may itself be unary and ^ is right associative.
    double unary()
    {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        const double base = postfix();
        if (eat('^')) return std::pow(base, unary());
        return base;
    }

    double postfix()
    {
        double v = primary();
        while (eat('!')) v = factorial(v);
        return v;
    }

    static double factorial(double v)
    {
        if (v < 0 || v != std::floor(v) || v > 170) fail("factorial needs an integer from 0 to 170");
        double r = 1;
        for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;
        return r;
    }

    double primary()
    {
        skip();
        if (pos_ >= s_.size()) fail("unexpected end of expression");
        if (eat('(')) { const double v = expr(); if (!eat(')')) fail("missing closing parenthesis"); return v; }
        const char c = s_[pos_];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
        if (std::isalpha(static_cast<unsigned char>(c))) return name();
        fail(std::string("unexpected '") + c + "'");
    }

    double number()
    {
        const char* start = s_.c_str() + pos_;
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) fail("bad number");
        pos_ += static_cast<std::size_t>(end - start);
        return v;
    }

    double name()
    {
        std::string id;
        while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) id += s_[pos_++];
        if (id == "pi") return M_PI;
        if (id == "e") return M_E;
        if (id == "tau") return 2 * M_PI;
        if (!eat('(')) fail("unknown name '" + id + "'");
        std::vector<double> args;
        if (!eat(')')) {
            do args.push_back(expr()); while (eat(','));
            if (!eat(')')) fail("missing closing parenthesis");
        }
        return call(id, args);
    }

    double toRad(double x) const { return degrees_ ? x * M_PI / 180 : x; }
    double fromRad(double x) const { return degrees_ ? x * 180 / M_PI : x; }

    double call(const std::string& f, const std::vector<double>& a)
    {
        if (f == "pow" || f == "min" || f == "max") {
            if (a.size() != 2) fail(f + " takes two arguments");
            return f == "pow" ? std::pow(a[0], a[1]) : f == "min" ? std::fmin(a[0], a[1]) : std::fmax(a[0], a[1]);
        }
        if (a.size() != 1) fail(f + " takes one argument");
        const double x = a[0];
        if (f == "sin") return std::sin(toRad(x));
        if (f == "cos") return std::cos(toRad(x));
        if (f == "tan") return std::tan(toRad(x));
        if (f == "asin") return fromRad(std::asin(x));
        if (f == "acos") return fromRad(std::acos(x));
        if (f == "atan") return fromRad(std::atan(x));
        if (f == "sinh") return std::sinh(x);
        if (f == "cosh") return std::cosh(x);
        if (f == "tanh") return std::tanh(x);
        if (f == "ln") { if (x <= 0) fail("ln needs a positive number"); return std::log(x); }
        if (f == "log") { if (x <= 0) fail("log needs a positive number"); return std::log10(x); }
        if (f == "log2") { if (x <= 0) fail("log2 needs a positive number"); return std::log2(x); }
        if (f == "sqrt") { if (x < 0) fail("sqrt of a negative number"); return std::sqrt(x); }
        if (f == "cbrt") return std::cbrt(x);
        if (f == "abs") return std::fabs(x);
        if (f == "exp") return std::exp(x);
        if (f == "floor") return std::floor(x);
        if (f == "ceil") return std::ceil(x);
        if (f == "round") return std::round(x);
        if (f == "fact") return factorial(x);
        fail("unknown function '" + f + "'");
    }

    const std::string& s_;
    bool degrees_;
    std::size_t pos_ = 0;
};

}  // namespace

double evaluate(const std::string& expr, bool degrees)
{
    return Parser(expr, degrees).run();
}

}  // namespace calc
)CPP";

const char* kEvalTests = R"CPP(#include "calc/calc.hpp"
#include "helpers/check.hpp"

#include <stdexcept>

static double ev(const char* e, bool deg = false) { return calc::evaluate(e, deg); }

TEST(addition_and_subtraction) { CHECK_NEAR(ev("1+2"), 3, 1e-9); CHECK_NEAR(ev("10-4-3"), 3, 1e-9); }
TEST(multiplication_and_division) { CHECK_NEAR(ev("6*7"), 42, 1e-9); CHECK_NEAR(ev("7/2"), 3.5, 1e-9); }
TEST(precedence) { CHECK_NEAR(ev("2+3*4"), 14, 1e-9); CHECK_NEAR(ev("(2+3)*4"), 20, 1e-9); }
TEST(power_is_right_associative) { CHECK_NEAR(ev("2^3^2"), 512, 1e-9); }
TEST(unary_minus_binds_looser_than_power) { CHECK_NEAR(ev("-2^2"), -4, 1e-9); CHECK_NEAR(ev("2^-1"), 0.5, 1e-9); }
TEST(modulo) { CHECK_NEAR(ev("10%3"), 1, 1e-9); }
TEST(factorial) { CHECK_NEAR(ev("5!"), 120, 1e-9); CHECK_NEAR(ev("0!"), 1, 1e-9); }
TEST(decimals) { CHECK_NEAR(ev("1.5+.5"), 2, 1e-9); CHECK_NEAR(ev("1.5e2"), 150, 1e-9); }
TEST(constants) { CHECK_NEAR(ev("pi"), 3.14159265358979, 1e-9); CHECK_NEAR(ev("e"), 2.71828182845905, 1e-9); CHECK_NEAR(ev("tau"), 6.28318530717959, 1e-9); }
TEST(radians) { CHECK_NEAR(ev("sin(0)"), 0, 1e-9); CHECK_NEAR(ev("cos(pi)"), -1, 1e-9); }
TEST(degrees) { CHECK_NEAR(ev("sin(90)", true), 1, 1e-9); CHECK_NEAR(ev("asin(1)", true), 90, 1e-9); }
TEST(logarithms) { CHECK_NEAR(ev("ln(e)"), 1, 1e-9); CHECK_NEAR(ev("log(1000)"), 3, 1e-9); CHECK_NEAR(ev("log2(8)"), 3, 1e-9); }
TEST(roots_and_rounding) { CHECK_NEAR(ev("sqrt(16)"), 4, 1e-9); CHECK_NEAR(ev("cbrt(27)"), 3, 1e-9); CHECK_NEAR(ev("floor(2.7)"), 2, 1e-9); CHECK_NEAR(ev("round(2.5)"), 3, 1e-9); }
TEST(two_argument_functions) { CHECK_NEAR(ev("pow(2,10)"), 1024, 1e-9); CHECK_NEAR(ev("min(3,4)"), 3, 1e-9); CHECK_NEAR(ev("max(3,4)"), 4, 1e-9); }
TEST(division_by_zero_throws) { CHECK_THROWS(ev("1/0")); CHECK_THROWS(ev("1%0")); }
TEST(invalid_input_throws) { CHECK_THROWS(ev("foo")); CHECK_THROWS(ev("sqrt(-1)")); CHECK_THROWS(ev("(1+2")); CHECK_THROWS(ev("1+")); CHECK_THROWS(ev("2 $ 3")); CHECK_THROWS(ev("sin(1,2)")); }
)CPP";

const char* kMain = R"CPP(#include "calc/calc.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// A fixed 40-column text frame: the app's "screen", which tests and the harness can compare.
void screen(const std::string& expr, const std::string& result)
{
    const std::string rule = "+" + std::string(38, '-') + "+";
    const auto row = [](const std::string& text) { return "| " + text.substr(0, 36) + std::string(36 - std::min<std::size_t>(36, text.size()), ' ') + " |"; };
    std::printf("%s\n%s\n%s\n%s\n", rule.c_str(), row(expr).c_str(), row("= " + result).c_str(), rule.c_str());
}

std::string format(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

}  // namespace

int main(int argc, char** argv)
{
    bool degrees = false, framed = false;
    std::string expr;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--degrees") degrees = true;
        else if (a == "--screen") framed = true;
        else expr += (expr.empty() ? "" : " ") + a;
    }
    if (expr.empty()) std::getline(std::cin, expr);
    try {
        const std::string r = format(calc::evaluate(expr, degrees));
        if (framed) screen(expr, r); else std::printf("%s\n", r.c_str());
        return 0;
    } catch (const std::exception& e) {
        if (framed) screen(expr, std::string("Error: ") + e.what()); else std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
)CPP";

const char* kCliTests = R"CPP(#include "helpers/app.hpp"
#include "helpers/check.hpp"

TEST(prints_the_result) { const auto r = app::run("2^3^2"); CHECK_EQ(r.output, std::string("512\n")); CHECK_EQ(r.code, 0); }
TEST(joins_arguments) { CHECK_EQ(app::run("1 + 2").output, std::string("3\n")); }
TEST(rounds_floating_noise) { CHECK_EQ(app::run("0.1+0.2").output, std::string("0.3\n")); }
TEST(degrees_flag) { CHECK_EQ(app::run("--degrees \"sin(90)\"").output, std::string("1\n")); }
TEST(error_message_and_status) { const auto r = app::run("1/0"); CHECK(r.output.find("Error:") == 0); CHECK(r.code != 0); }
TEST(screen_frame) {
    const std::string expected = "+--------------------------------------+\n| 2+2                                  |\n| = 4                                  |\n+--------------------------------------+\n";
    CHECK_EQ(app::run("--screen 2+2").output, expected);
}
)CPP";

// Deliberate defects: a change that compiles and fails its own tests.
struct Bug { const char* from; const char* to; };
const std::map<std::string, Bug>& bugs()
{
    static const std::map<std::string, Bug> m = {
        {"src/calc.cpp", {"v = std::fmod(v, d);", "v = v / d;"}},
        {"src/main.cpp", {"\"%.12g\"", "\"%.2f\""}},
    };
    return m;
}

}  // namespace

const std::vector<HarnessFile>& cppScaffold()
{
    static const std::vector<HarnessFile> files = {
        {"CMakeLists.txt", kCMake}, {"include/calc/calc.hpp", kHeader}, {"src/calc.cpp", kStubCalc}, {"src/main.cpp", kStubMain},
        {"tests/helpers/check.hpp", kCheck}, {"tests/helpers/app.hpp", kAppHelper}, {"tests/helpers/main.cpp", kRunner},
    };
    return files;
}

std::size_t cppScaffoldBytes()
{
    std::size_t n = 0;
    for (const auto& f : cppScaffold()) n += f.content.size();
    return n;
}

std::string cppHarnessFacts()
{
    return "- Project type `cpp-cmake`: C++20, standard library only, built with CMake and Ninja under -Wall -Wextra -Werror (a warning is a failure), tests are one executable per tests/NAME.test.cpp.\n"
           "- The scaffold every item's workspace starts from: CMakeLists.txt, include/calc/calc.hpp (the contract: calc::evaluate), tests/helpers/check.hpp (TEST, CHECK, CHECK_EQ, CHECK_NEAR, CHECK_THROWS) and its runner, tests/helpers/app.hpp (app::run), and STUB src/calc.cpp (throws 'not implemented') and src/main.cpp.\n"
           "- The library is every src/*.cpp except main.cpp; the app calc_cli is main.cpp; a test file runs the app with `#include \"helpers/app.hpp\"` and app::run(\"ARGS\") (returns .output and .code).\n"
           "- A worker replies with whole files as `=== FILE: path === ... === END FILE ===`; anything outside the item's allowed files is rejected.\n"
           "- Cheapest checks first: the allowlist, then `-fsyntax-only` of each written file (file, line, message), then a build of only the gate's targets, then the tests (TAP with expected and actual).\n"
           "- Tester gate: the tests must build against the stubs, number at least the item's minimum, and at least 70 % must FAIL against the stubs. Developer gate: every test passes.\n"
           "- The integrator builds everything again under AddressSanitizer and UBSan, runs every test, runs the app (a smoke expression and the --screen frame) and stores the frame as capture.txt.\n";
}

std::string cppHarnessBrief()
{
    return "Already in the workspace (do not recreate): CMakeLists.txt, include/calc/calc.hpp (declares double calc::evaluate(const std::string& expr, bool degrees = false); throws std::runtime_error), "
           "tests/helpers/check.hpp (TEST(name){...}, CHECK, CHECK_EQ, CHECK_NEAR(a,b,tol), CHECK_THROWS) with its runner, and stubs of src/calc.cpp and src/main.cpp. C++20, standard library only, "
           "no warnings allowed (-Wall -Wextra -Werror). A test file includes \"calc/calc.hpp\" and \"helpers/check.hpp\" and defines only TEST cases (no main).";
}

std::string cppMockFile(const std::string& path, bool buggy)
{
    std::string text;
    if (path == "src/calc.cpp") text = kCalc;
    else if (path == "tests/eval.test.cpp") text = kEvalTests;
    else if (path == "tests/cli.test.cpp") text = kCliTests;
    else if (path == "src/main.cpp") text = kMain;
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

std::string cppMockBugTarget(const std::string& item)
{
    static const std::map<std::string, std::string> m = {{"dev-engine", "src/calc.cpp"}, {"dev-app", "src/main.cpp"}};
    auto it = m.find(item);
    return it == m.end() ? std::string{} : it->second;
}

bool cppClaimsTask(const std::string& t)
{
    for (const char* w : {"native", "c++", "cpp", "cmake", "command line", "command-line", "terminal"})
        if (t.find(w) != std::string::npos) return true;
    return false;
}

const CalculatorSurface& cppSurface()
{
    static const CalculatorSurface s = {
        "calculator-cpp", "cpp-cmake", "sci-calc-cpp", "ctest", "tests/eval.test.cpp", "src/calc.cpp", "tests/cli.test.cpp", "src/main.cpp",
        "double calc::evaluate(const std::string& expr, bool degrees = false), declared in include/calc/calc.hpp and defined in src/calc.cpp",
        "std::runtime_error",
        "includes \"calc/calc.hpp\" (engine tests) and \"helpers/check.hpp\" and defines only TEST cases (CHECK_NEAR, CHECK_EQ, CHECK_THROWS), no main",
        "In a test, #include \"helpers/app.hpp\" and call app::run(\"ARGS\"), which returns a struct with .output (stdout and stderr together) and .code (the exit code); do not use popen or CALC_CLI_PATH yourself.",
        "int main(int argc, char** argv)",
        "C++20, standard library only, no warnings",
        "A small recursive descent parser works well: expression -> term -> unary -> postfix -> primary"};
    return s;
}

std::string cppBlueprintJson(const std::string& name)
{
    return name == "calculator-cpp" ? calculatorPlan(cppSurface()).toJson() : std::string{};
}

}  // namespace sub0squad
