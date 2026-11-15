#include "calc/calc.hpp"

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
