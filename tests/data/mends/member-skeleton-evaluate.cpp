#include "calc/calc.hpp"
#include <stdexcept>
#include <string>
#include <cctype>
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace calc {

struct Token {
    enum Type { NUM, OP, FUNC, LPAR, RPAR, COMMA, FACTORIAL, IDENT };
    Type type;
    std::string text;
    double value;
};

class Parser {
    std::string expr;
    size_t pos;
    bool degrees;
    std::vector<Token> tokens;
    size_t token_idx;

    void tokenize();
    void next();
    double parseExpression();
    double parseTerm();
    double parseUnary();
    double parsePostfix();
    double parsePrimary();
    double parseFunctionCall();
    double parseNumber();
    void expect(char c);
    void skipWhitespace();
    double applyOp(double left, double right, char op);
    double factorial(double n);
    double toRad(double x);
    double fromRad(double x);

public:
    Parser(const std::string& expr, bool degrees);
    double evaluate();
};

double evaluate(const std::string& expr, bool degrees);

double evaluate(const std::string& expr, bool degrees)
{
    Parser p(expr, degrees);
    return p.evaluate();
}

}  // namespace calc
