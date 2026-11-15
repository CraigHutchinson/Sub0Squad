#include "helpers/check.hpp"
#include "wc/wc.hpp"

TEST(two_words) { CHECK_EQ(wc::countWords("hello world"), static_cast<std::size_t>(2)); }
TEST(empty) { CHECK_EQ(wc::countWords(""), static_cast<std::size_t>(0)); }
TEST(extra_spaces) { CHECK_EQ(wc::countWords("  a   b  "), static_cast<std::size_t>(2)); }
TEST(mixed_whitespace) { CHECK_EQ(wc::countWords("one\ntwo\tthree"), static_cast<std::size_t>(3)); }
TEST(single) { CHECK_EQ(wc::countWords("x"), static_cast<std::size_t>(1)); }
TEST(tabs_between) { CHECK_EQ(wc::countWords("a\tb"), static_cast<std::size_t>(2)); }
