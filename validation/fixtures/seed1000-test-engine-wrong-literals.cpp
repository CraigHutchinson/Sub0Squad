#include "calc/calc.hpp"
#include "helpers/check.hpp"
#include <string>
#include <cmath>

TEST(numeric_literals) {
    CHECK_NEAR(12.0, calc::evaluate("12"), 1e-9);
    CHECK_NEAR(1.5, calc::evaluate("1.5"), 1e-9);
    CHECK_NEAR(0.5, calc::evaluate(".5"), 1e-9);
    CHECK_NEAR(150.0, calc::evaluate("1.5e2"), 1e-9);
}

TEST(constants) {
    CHECK_NEAR(3.14159265358979323846, calc::evaluate("pi"), 1e-9);
    CHECK_NEAR(2.71828182845904523536, calc::evaluate("e"), 1e-9);
    CHECK_NEAR(6.28318530717958647692, calc::evaluate("tau"), 1e-9);
}

TEST(arithmetic_operators) {
    CHECK_NEAR(5.0, calc::evaluate("2 + 3"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("2 - 1"), 1e-9);
    CHECK_NEAR(6.0, calc::evaluate("2 * 3"), 1e-9);
    CHECK_NEAR(2.0, calc::evaluate("6 / 3"), 1e-9);
    CHECK_NEAR(0.0, calc::evaluate("5 % 2"), 1e-9);
}

TEST(operator_precedence) {
    CHECK_NEAR(7.0, calc::evaluate("2 + 3 * 4"), 1e-9);
    CHECK_NEAR(10.0, calc::evaluate("(2 + 3) * 4"), 1e-9);
    CHECK_NEAR(0.0, calc::evaluate("2 * 3 + 4 * 5 - 26"), 1e-9);
}

TEST(power_and_unary_minus) {
    // -2^2 should be -(2^2) = -4 due to precedence
    CHECK_NEAR(-4.0, calc::evaluate("-2^2"), 1e-9);
    // (-2)^2 should be 4
    CHECK_NEAR(4.0, calc::evaluate("(-2)^2"), 1e-9);
    // Right associativity: 2^2^3 = 2^(2^3) = 2^8 = 256
    CHECK_NEAR(256.0, calc::evaluate("2^2^3"), 1e-9);
}

TEST(factorial) {
    CHECK_NEAR(1.0, calc::evaluate("0!"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("1!"), 1e-9);
    CHECK_NEAR(6.0, calc::evaluate("3!"), 1e-9);
    CHECK_NEAR(120.0, calc::evaluate("5!"), 1e-9);
    CHECK_NEAR(3628800.0, calc::evaluate("10!"), 1e-9);
}

TEST(trig_radians) {
    CHECK_NEAR(0.0, calc::evaluate("sin(0)"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("sin(pi/2)"), 1e-9);
    CHECK_NEAR(0.0, calc::evaluate("cos(pi/2)"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("cos(0)"), 1e-9);
}

TEST(trig_degrees) {
    CHECK_NEAR(1.0, calc::evaluate("sin(90)", true), 1e-9);
    CHECK_NEAR(0.0, calc::evaluate("cos(90)", true), 1e-9);
    CHECK_NEAR(45.0, calc::evaluate("asin(1)*180/pi", true), 1e-9); // asin(1) = pi/2 rad = 90 deg
}

TEST(logarithms) {
    CHECK_NEAR(0.0, calc::evaluate("ln(1)"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("ln(e)"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("log(10)"), 1e-9);
    CHECK_NEAR(1.0, calc::evaluate("log2(2)"), 1e-9);
}

TEST(two_arg_functions) {
    CHECK_NEAR(8.0, calc::evaluate("pow(2,3)"), 1e-9);
    CHECK_NEAR(2.0, calc::evaluate("min(2,3)"), 1e-9);
    CHECK_NEAR(3.0, calc::evaluate("max(2,3)"), 1e-9);
}

TEST(error_cases) {
    CHECK_THROWS(calc::evaluate("1 / 0"));
    CHECK_THROWS(calc::evaluate("1 % 0"));
    CHECK_THROWS(calc::evaluate("sqrt(-1)"));
    CHECK_THROWS(calc::evaluate("sin(1, 2)"));
    CHECK_THROWS(calc::evaluate("unknown"));
    CHECK_THROWS(calc::evaluate("(1 + 2"));
    CHECK_THROWS(calc::evaluate("2 +"));
    CHECK_THROWS(calc::evaluate("2 + 3 +"));
}
