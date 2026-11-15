#include "calc/calc.hpp"
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
