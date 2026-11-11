#pragma once
/// @file calculator_spec.hpp
/// The calculator proving-ground project stated ONCE, independent of platform (docs/PROVING_GROUND.md section 1): the expression language, the
/// engine's error behaviour and the command-line app's output are the same contract on the web, in native C++ and in Python. Each platform supplies
/// only a `CalculatorSurface` (file names, how the engine is declared, how a test runs the app, style) and gets its plan-2 from here. A platform
/// whose blueprint needs more than a surface is a sign the abstraction is wrong, which is the point of having several platforms.

#include "sub0squad/plan.hpp"

#include <string>
#include <vector>

namespace sub0squad {

/// What differs between platforms for the calculator.
struct CalculatorSurface
{
    std::string name;                     ///< blueprint name, e.g. "calculator-cpp"
    std::string harness;                  ///< registry name, e.g. "cpp-cmake"
    std::string project;                  ///< project name in the plan
    std::string gateKind;                 ///< the harness's gate kind
    std::string engineTests, engine;      ///< file paths of the engine's tests and the engine
    std::string cliTests, app;            ///< file paths of the app's tests and the app
    std::string engineApi;                ///< how the engine is declared in this language ("double calc::evaluate(const std::string& expr, bool degrees = false) in include/calc/calc.hpp")
    std::string errorKind;                ///< what invalid input raises ("std::runtime_error")
    std::string testRules;                ///< how a test file is shaped here (includes, framework, no main)
    std::string runApiNote;               ///< how a test runs the app ("std::string(CALC_CLI_PATH) is the program's path; run it with popen")
    std::string appEntry;                 ///< how the app starts ("int main(int argc, char** argv)")
    std::string style;                    ///< "C++20, standard library only, no warnings"
    std::string engineHint;               ///< a note for the engine developer
};

/// The calculator blueprint (four items: engine tests, engine, app tests, app) for a platform's surface.
[[nodiscard]] Plan calculatorPlan(const CalculatorSurface& s);

}  // namespace sub0squad
