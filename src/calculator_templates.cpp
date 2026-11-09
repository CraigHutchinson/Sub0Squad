// The sandbox's "agent output": a small scientific calculator as header-only modules, plus a deliberate defect per module.
// Each defect is one textual replacement; a replacement that no longer matches is caught by test_sandbox.
#include "sub0squad/sandbox.hpp"

#include <map>

namespace sub0squad {

namespace {

const char* kTokenizer = R"CPP(#pragma once
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace calc {

struct Token {
    enum Kind { Number, Ident, Op, LParen, RParen, Comma, End } kind;
    std::string text;
    double value = 0;
};

inline std::vector<Token> tokenize(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            std::size_t j = i;
            while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
                if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                    while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) ++k;
                    j = k;
                }
            }
            Token t{Token::Number, std::string(s.substr(i, j - i)), 0};
            t.value = std::stod(t.text);
            out.push_back(t);
            i = j;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            out.push_back({Token::Ident, std::string(s.substr(i, j - i)), 0});
            i = j;
            continue;
        }
        if (c == '(') { out.push_back({Token::LParen, "(", 0}); ++i; continue; }
        if (c == ')') { out.push_back({Token::RParen, ")", 0}); ++i; continue; }
        if (c == ',') { out.push_back({Token::Comma, ",", 0}); ++i; continue; }
        if (std::string_view("+-*/^%!").find(c) != std::string_view::npos) { out.push_back({Token::Op, std::string(1, c), 0}); ++i; continue; }
        throw std::runtime_error(std::string("unexpected character '") + c + "'");
    }
    out.push_back({Token::End, "", 0});
    return out;
}

}  // namespace calc
)CPP";

const char* kTestTokenizer = R"CPP(#include "tokenizer.hpp"
#include <cstdio>
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
    auto t = calc::tokenize("12.5 + foo(3, 2e3)");
    CHECK(t.size() == 9);  // 12.5 + foo ( 3 , 2e3 ) End
    CHECK(t[0].kind == calc::Token::Number && t[0].value == 12.5);
    CHECK(t[1].kind == calc::Token::Op && t[1].text == "+");
    CHECK(t[2].kind == calc::Token::Ident && t[2].text == "foo");
    CHECK(t[6].kind == calc::Token::Number && t[6].value == 2000.0);
    CHECK(t.back().kind == calc::Token::End);
    CHECK(calc::tokenize(".25")[0].value == 0.25);  // a leading decimal point
    bool threw = false;
    try { calc::tokenize("2 $ 3"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    std::puts("tokenizer ok");
    return 0;
}
)CPP";

const char* kParser = R"CPP(#pragma once
#include "tokenizer.hpp"
#include <memory>
#include <stdexcept>
#include <vector>

namespace calc {

struct Node;
using NodePtr = std::unique_ptr<Node>;
struct Node {
    enum Kind { Number, Ident, Neg, Bin, Call, Fact } kind = Number;
    double value = 0;
    std::string name;
    char op = 0;
    std::vector<NodePtr> kids;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : t_(tokens) {}
    NodePtr parseAll() {
        NodePtr n = expr();
        if (peek().kind != Token::End) throw std::runtime_error("unexpected token '" + peek().text + "'");
        return n;
    }
private:
    const Token& peek() const { return t_[p_]; }
    bool isOp(const char* o) const { return peek().kind == Token::Op && peek().text == o; }
    static NodePtr bin(char op, NodePtr l, NodePtr r) {
        auto n = std::make_unique<Node>();
        n->kind = Node::Bin; n->op = op;
        n->kids.push_back(std::move(l)); n->kids.push_back(std::move(r));
        return n;
    }
    NodePtr expr() {
        NodePtr l = term();
        while (isOp("+") || isOp("-")) { const char op = t_[p_++].text[0]; l = bin(op, std::move(l), term()); }
        return l;
    }
    NodePtr term() {
        NodePtr l = unary();
        while (isOp("*") || isOp("/") || isOp("%")) { const char op = t_[p_++].text[0]; l = bin(op, std::move(l), unary()); }
        return l;
    }
    NodePtr unary() {
        if (isOp("-")) { ++p_; auto n = std::make_unique<Node>(); n->kind = Node::Neg; n->kids.push_back(unary()); return n; }
        if (isOp("+")) { ++p_; return unary(); }
        return power();
    }
    NodePtr power() {
        NodePtr base = postfix();
        if (isOp("^")) { ++p_; return bin('^', std::move(base), unary()); }
        return base;
    }
    NodePtr postfix() {
        NodePtr n = primary();
        while (isOp("!")) { ++p_; auto f = std::make_unique<Node>(); f->kind = Node::Fact; f->kids.push_back(std::move(n)); n = std::move(f); }
        return n;
    }
    NodePtr primary() {
        const Token tok = peek();
        if (tok.kind == Token::Number) { ++p_; auto n = std::make_unique<Node>(); n->value = tok.value; return n; }
        if (tok.kind == Token::Ident) {
            ++p_;
            auto n = std::make_unique<Node>();
            n->name = tok.text;
            if (peek().kind == Token::LParen) {
                ++p_;
                n->kind = Node::Call;
                if (peek().kind != Token::RParen) {
                    n->kids.push_back(expr());
                    while (peek().kind == Token::Comma) { ++p_; n->kids.push_back(expr()); }
                }
                if (peek().kind != Token::RParen) throw std::runtime_error("expected ')'");
                ++p_;
            } else {
                n->kind = Node::Ident;
            }
            return n;
        }
        if (tok.kind == Token::LParen) {
            ++p_;
            NodePtr n = expr();
            if (peek().kind != Token::RParen) throw std::runtime_error("expected ')'");
            ++p_;
            return n;
        }
        throw std::runtime_error("unexpected token '" + tok.text + "'");
    }
    const std::vector<Token>& t_;
    std::size_t p_ = 0;
};

inline NodePtr parse(const std::vector<Token>& tokens) { return Parser(tokens).parseAll(); }

}  // namespace calc
)CPP";

const char* kTestParser = R"CPP(#include "parser.hpp"
#include <cstdio>
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
    auto ast = calc::parse(calc::tokenize("2^3^2"));
    CHECK(ast->kind == calc::Node::Bin && ast->op == '^');
    CHECK(ast->kids[1]->kind == calc::Node::Bin && ast->kids[1]->op == '^');  // right associative
    auto neg = calc::parse(calc::tokenize("-2^2"));
    CHECK(neg->kind == calc::Node::Neg && neg->kids[0]->op == '^');           // -(2^2)
    auto mixed = calc::parse(calc::tokenize("1+2*3"));
    CHECK(mixed->op == '+' && mixed->kids[1]->op == '*');
    auto call = calc::parse(calc::tokenize("max(1, 2+3)!"));
    CHECK(call->kind == calc::Node::Fact && call->kids[0]->kind == calc::Node::Call && call->kids[0]->kids.size() == 2);
    bool threw = false;
    try { calc::parse(calc::tokenize("2+")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { calc::parse(calc::tokenize("(1+2")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    std::puts("parser ok");
    return 0;
}
)CPP";

const char* kFunctions = R"CPP(#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace calc {

inline bool constant(std::string_view n, double& v) {
    if (n == "pi") { v = 3.14159265358979323846; return true; }
    if (n == "e") { v = 2.71828182845904523536; return true; }
    if (n == "tau") { v = 6.28318530717958647692; return true; }
    return false;
}

inline double call(std::string_view n, const std::vector<double>& a, bool degrees) {
    const double k = degrees ? 3.14159265358979323846 / 180.0 : 1.0;
    auto need = [&](std::size_t count) {
        if (a.size() != count) throw std::runtime_error(std::string(n) + " takes " + std::to_string(count) + " argument(s)");
    };
    if (n == "sin") { need(1); return std::sin(a[0] * k); }
    if (n == "cos") { need(1); return std::cos(a[0] * k); }
    if (n == "tan") { need(1); return std::tan(a[0] * k); }
    if (n == "asin") { need(1); return std::asin(a[0]) / k; }
    if (n == "acos") { need(1); return std::acos(a[0]) / k; }
    if (n == "atan") { need(1); return std::atan(a[0]) / k; }
    if (n == "sinh") { need(1); return std::sinh(a[0]); }
    if (n == "cosh") { need(1); return std::cosh(a[0]); }
    if (n == "tanh") { need(1); return std::tanh(a[0]); }
    if (n == "ln") { need(1); return std::log(a[0]); }
    if (n == "log") { need(1); return std::log10(a[0]); }
    if (n == "log2") { need(1); return std::log2(a[0]); }
    if (n == "sqrt") { need(1); if (a[0] < 0) throw std::runtime_error("sqrt of a negative number"); return std::sqrt(a[0]); }
    if (n == "cbrt") { need(1); return std::cbrt(a[0]); }
    if (n == "abs") { need(1); return std::fabs(a[0]); }
    if (n == "exp") { need(1); return std::exp(a[0]); }
    if (n == "floor") { need(1); return std::floor(a[0]); }
    if (n == "ceil") { need(1); return std::ceil(a[0]); }
    if (n == "round") { need(1); return std::round(a[0]); }
    if (n == "pow") { need(2); return std::pow(a[0], a[1]); }
    if (n == "min") { need(2); return a[0] < a[1] ? a[0] : a[1]; }
    if (n == "max") { need(2); return a[0] > a[1] ? a[0] : a[1]; }
    if (n == "fact") {
        need(1);
        if (a[0] < 0 || a[0] > 170 || a[0] != std::floor(a[0])) throw std::runtime_error("factorial needs an integer in 0..170");
        return std::round(std::tgamma(a[0] + 1.0));
    }
    throw std::runtime_error("unknown function '" + std::string(n) + "'");
}

}  // namespace calc
)CPP";

const char* kTestFunctions = R"CPP(#include "functions.hpp"
#include <cstdio>
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
int main() {
    CHECK(near(calc::call("sin", {0.5235987755982988}, false), 0.5));
    CHECK(near(calc::call("sin", {30}, true), 0.5));
    CHECK(near(calc::call("cos", {60}, true), 0.5));
    CHECK(near(calc::call("asin", {0.5}, true), 30));
    CHECK(near(calc::call("fact", {5}, false), 120));
    CHECK(near(calc::call("fact", {0}, false), 1));
    CHECK(near(calc::call("log", {1000}, false), 3));
    CHECK(near(calc::call("ln", {2.718281828459045}, false), 1));
    CHECK(near(calc::call("pow", {2, 10}, false), 1024));
    CHECK(near(calc::call("max", {3, 7}, false), 7));
    double v = 0;
    CHECK(calc::constant("pi", v) && near(v, 3.141592653589793));
    CHECK(!calc::constant("zzz", v));
    bool threw = false;
    try { calc::call("nope", {1}, false); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { calc::call("sin", {1, 2}, false); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    std::puts("functions ok");
    return 0;
}
)CPP";

const char* kEvaluator = R"CPP(#pragma once
#include "functions.hpp"
#include "parser.hpp"
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace calc {

inline double evalNode(const Node& n, bool degrees) {
    switch (n.kind) {
        case Node::Number: return n.value;
        case Node::Ident: {
            double v = 0;
            if (constant(n.name, v)) return v;
            throw std::runtime_error("unknown name '" + n.name + "'");
        }
        case Node::Neg: return -evalNode(*n.kids[0], degrees);
        case Node::Fact: return call("fact", {evalNode(*n.kids[0], degrees)}, degrees);
        case Node::Call: {
            std::vector<double> args;
            for (const auto& k : n.kids) args.push_back(evalNode(*k, degrees));
            return call(n.name, args, degrees);
        }
        case Node::Bin: {
            const double l = evalNode(*n.kids[0], degrees);
            const double r = evalNode(*n.kids[1], degrees);
            switch (n.op) {
                case '+': return l + r;
                case '-': return l - r;
                case '*': return l * r;
                case '/': if (r == 0) throw std::runtime_error("division by zero"); return l / r;
                case '%': if (r == 0) throw std::runtime_error("modulo by zero"); return std::fmod(l, r);
                case '^': return std::pow(l, r);
                default: throw std::runtime_error("bad operator");
            }
        }
    }
    throw std::runtime_error("bad node");
}

inline double evaluate(std::string_view expr, bool degrees = false) {
    const auto tokens = tokenize(expr);
    const auto ast = parse(tokens);
    return evalNode(*ast, degrees);
}

}  // namespace calc
)CPP";

const char* kTestEvaluator = R"CPP(#include "evaluator.hpp"
#include <cstdio>
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
int main() {
    CHECK(near(calc::evaluate("2+3*4"), 14));
    CHECK(near(calc::evaluate("(2+3)*4"), 20));
    CHECK(near(calc::evaluate("2^3^2"), 512));
    CHECK(near(calc::evaluate("-2^2"), -4));
    CHECK(near(calc::evaluate("5!"), 120));
    CHECK(near(calc::evaluate("sin(pi/6)"), 0.5));
    CHECK(near(calc::evaluate("sin(30)", true), 0.5));
    bool threw = false;
    try { calc::evaluate("1/0"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    std::puts("evaluator ok");
    return 0;
}
)CPP";

const char* kCli = R"CPP(#pragma once
#include "evaluator.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace calc {

inline int run(int argc, char** argv, std::istream& in, std::ostream& out, std::ostream& err) {
    bool degrees = false;
    std::vector<std::string> exprs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--deg") degrees = true; else exprs.push_back(a);
    }
    auto format = [](double v) { std::ostringstream o; o << std::setprecision(12) << v; return o.str(); };
    auto one = [&](const std::string& e) {
        try { out << format(evaluate(e, degrees)) << "\n"; return true; }
        catch (const std::exception& x) { err << "error: " << x.what() << "\n"; return false; }
    };
    int rc = 0;
    if (exprs.empty()) {
        std::string line;
        while (std::getline(in, line)) { if (line.empty()) continue; if (!one(line)) rc = 1; }
    } else {
        for (const auto& e : exprs) if (!one(e)) rc = 1;
    }
    return rc;
}

}  // namespace calc
)CPP";

const char* kMain = R"CPP(#include "cli.hpp"

int main(int argc, char** argv) { return calc::run(argc, argv, std::cin, std::cout, std::cerr); }
)CPP";

const char* kTestCli = R"CPP(#include "cli.hpp"
#include <cstdio>
#include <sstream>
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
    {
        char a0[] = "calc"; char a1[] = "2+3*4";
        char* argv[] = {a0, a1};
        std::istringstream in; std::ostringstream out, err;
        CHECK(calc::run(2, argv, in, out, err) == 0);
        CHECK(out.str() == "14\n");
    }
    {
        char a0[] = "calc"; char a1[] = "2+";
        char* argv[] = {a0, a1};
        std::istringstream in; std::ostringstream out, err;
        CHECK(calc::run(2, argv, in, out, err) == 1);
        CHECK(err.str().find("error:") == 0);
    }
    {
        char a0[] = "calc"; char a1[] = "--deg"; char a2[] = "sin(30)";
        char* argv[] = {a0, a1, a2};
        std::istringstream in; std::ostringstream out, err;
        CHECK(calc::run(3, argv, in, out, err) == 0);
        CHECK(out.str() == "0.5\n");
    }
    {
        char a0[] = "calc";
        char* argv[] = {a0};
        std::istringstream in("1+1\n\n3*3\n"); std::ostringstream out, err;
        CHECK(calc::run(1, argv, in, out, err) == 0);
        CHECK(out.str() == "2\n9\n");
    }
    std::puts("cli ok");
    return 0;
}
)CPP";

const char* kCalcTests = R"CPP(#include "evaluator.hpp"
#include <cstdio>
#include <cstring>
#include <string>

struct Case { const char* expr; double expect; bool degrees; };
static bool near(double a, double b) { return std::fabs(a - b) <= 1e-9 * (1.0 + std::fabs(b)); }

int main() {
    const Case cases[] = {
        {"2+3*4", 14, false}, {"(2+3)*4", 20, false}, {"10/4", 2.5, false}, {"10%3", 1, false}, {"2^10", 1024, false},
        {"2^3^2", 512, false}, {"-2^2", -4, false}, {"2^-1", 0.5, false}, {"--3", 3, false}, {"5!", 120, false},
        {"0!", 1, false}, {"3!!", 720, false}, {"1.5e2+1", 151, false}, {".5+.5", 1, false}, {"sin(pi/6)", 0.5, false},
        {"cos(0)", 1, false}, {"tan(pi/4)", 1, false}, {"sin(30)", 0.5, true}, {"cos(60)", 0.5, true}, {"tan(45)", 1, true},
        {"asin(1)", 90, true}, {"atan(1)", 45, true}, {"ln(e)", 1, false}, {"log(1000)", 3, false}, {"log2(8)", 3, false},
        {"sqrt(16)", 4, false}, {"sqrt(2)^2", 2, false}, {"cbrt(27)", 3, false}, {"abs(-7.5)", 7.5, false}, {"exp(0)", 1, false},
        {"floor(2.7)", 2, false}, {"ceil(2.1)", 3, false}, {"round(2.5)", 3, false}, {"pow(2,8)", 256, false}, {"max(3,7)", 7, false},
        {"min(3,7)", 3, false}, {"fact(6)", 720, false}, {"pi*2", 6.283185307179586, false}, {"tau/2", 3.141592653589793, false},
        {"sinh(0)", 0, false}, {"cosh(0)", 1, false}, {"max(sin(pi/2), cos(0))+1", 2, false}, {"2*(3+(4-1)*2)", 18, false},
    };
    int failed = 0, total = 0;
    for (const auto& c : cases) {
        ++total;
        try {
            const double got = calc::evaluate(c.expr, c.degrees);
            if (!near(got, c.expect)) { std::printf("FAIL %s = %.12g, expected %.12g\n", c.expr, got, c.expect); ++failed; }
        } catch (const std::exception& e) { std::printf("FAIL %s threw: %s\n", c.expr, e.what()); ++failed; }
    }
    const char* bad[] = {"1/0", "2+", "(1+2", "foo(1)", "sin(1,2)", "2 $ 3", "sqrt(-1)", "unknown"};
    for (const char* e : bad) {
        ++total;
        bool threw = false;
        try { calc::evaluate(e); } catch (const std::exception&) { threw = true; }
        if (!threw) { std::printf("FAIL '%s' should be rejected\n", e); ++failed; }
    }
    std::printf("calc_tests: %d/%d passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
)CPP";

struct Bug { const char* from; const char* to; };

const std::map<std::string, const char*>& files()
{
    static const std::map<std::string, const char*> m = {
        {"tokenizer.hpp", kTokenizer}, {"test_tokenizer.cpp", kTestTokenizer}, {"parser.hpp", kParser}, {"test_parser.cpp", kTestParser},
        {"functions.hpp", kFunctions}, {"test_functions.cpp", kTestFunctions}, {"evaluator.hpp", kEvaluator},
        {"test_evaluator.cpp", kTestEvaluator}, {"cli.hpp", kCli}, {"main.cpp", kMain}, {"test_cli.cpp", kTestCli}, {"calc_tests.cpp", kCalcTests},
    };
    return m;
}

const std::map<std::string, Bug>& bugs()
{
    static const std::map<std::string, Bug> m = {
        // a decimal point is no longer part of a number: "12.5" fails at run time
        {"tokenizer.hpp", {"if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {", "if (std::isdigit(static_cast<unsigned char>(c))) {"}},
        // ^ becomes left-leaning: "2^3^2" no longer parses
        {"parser.hpp", {"return bin('^', std::move(base), unary());", "return bin('^', std::move(base), postfix());"}},
        // factorial off by one
        {"functions.hpp", {"std::tgamma(a[0] + 1.0)", "std::tgamma(a[0] + 2.0)"}},
        // a signature that does not compile: a parameter without a default after one with a default
        {"evaluator.hpp", {"inline double evaluate(std::string_view expr, bool degrees = false) {", "inline double evaluate(std::string_view expr, bool degrees = false, int injected) {"}},
        // errors no longer set the exit code
        {"cli.hpp", {"for (const auto& e : exprs) if (!one(e)) rc = 1;", "for (const auto& e : exprs) (void)one(e);"}},
        // a wrong expectation in the combined tests
        {"calc_tests.cpp", {"{\"2+3*4\", 14, false}", "{\"2+3*4\", 15, false}"}},
    };
    return m;
}

}  // namespace

std::string calculatorFile(const std::string& file, bool buggy)
{
    auto it = files().find(file);
    if (it == files().end()) return {};
    std::string text = it->second;
    if (buggy) {
        auto b = bugs().find(file);
        if (b != bugs().end()) {
            const auto pos = text.find(b->second.from);
            if (pos != std::string::npos) text.replace(pos, std::string{b->second.from}.size(), b->second.to);
            else return {};  // the defect no longer matches the template: callers treat an empty result as an error
        }
    }
    return text;
}

std::string calculatorBugTarget(const std::string& item)
{
    static const std::map<std::string, std::string> m = {
        {"tokenizer", "tokenizer.hpp"}, {"parser", "parser.hpp"}, {"functions", "functions.hpp"}, {"evaluator", "evaluator.hpp"},
        {"cli", "cli.hpp"}, {"tests", "calc_tests.cpp"}, {"calculator", "parser.hpp"},
    };
    auto it = m.find(item);
    return it == m.end() ? std::string{} : it->second;
}

}  // namespace sub0squad
