#include "calc/calc.hpp"

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
