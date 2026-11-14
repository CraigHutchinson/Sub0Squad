#include "calc/calc.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <map>
#include <functional>

namespace calc {

struct Token {
    enum Type { NUMBER, NAME, OP, LPAREN, RPAREN, COMMA, END };
    Type type;
    std::string text;
    double value;
};

class Parser {
public:
    Parser(const std::string& expr, bool degrees);
    double parse();

private:
    std::string expr_;
    size_t pos_;
    bool degrees_;
    Token current_;

    void advance();
    double parseExpression();
    double parseTerm();
    double parseFactor();
    double parseUnary();
    double parsePrimary();
    double parseFunctionCall();
    double parseNumber();
    double parseName();
    void expect(char c);
    void skipWhitespace();
    bool isOperator(char c);
    bool isDigitOrPoint(char c);
    bool isAlpha(char c);
    bool isEnd() const;
    double evaluateFunction(const std::string& name, const std::vector<double>& args);
};

double evaluate(const std::string& expr, bool degrees);

double evaluate(const std::string& expr, bool degrees)
{
    return Parser(expr, degrees).parse();
}

}  // namespace calc
