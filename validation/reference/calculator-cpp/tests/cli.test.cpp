#include "helpers/app.hpp"
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
