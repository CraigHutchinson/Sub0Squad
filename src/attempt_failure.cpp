#include "sub0squad/attempt_failure.hpp"

#include <fstream>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

void recordAttemptFailure(const fs::path& attemptDir, const std::string& role, const std::string& kind, const std::string& reason)
{
    const auto escaped = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
            else out += c;
        }
        return out;
    };
    std::string why = reason.substr(0, 700);
    while (!why.empty() && (static_cast<unsigned char>(why.back()) & 0xC0) == 0x80) why.pop_back();   // never cut inside a UTF-8 sequence
    if (!why.empty() && (static_cast<unsigned char>(why.back()) & 0x80) != 0 && why.size() == 700) why.pop_back();
    std::error_code ec;
    fs::create_directories(attemptDir, ec);
    std::ofstream(attemptDir / "failure.json", std::ios::trunc) << ("{\"schema\":\"attempt-failure-1\",\"role\":\"" + escaped(role) + "\",\"kind\":\"" + escaped(kind) + "\",\"reason\":\"" + escaped(why) + "\"}\n");
}

void clearAttemptFailure(const fs::path& attemptDir)
{
    std::error_code ec;
    fs::remove(attemptDir / "failure.json", ec);
}

std::string attemptFailureJson(const fs::path& attemptDir)
{
    std::ifstream in(attemptDir / "failure.json");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

}  // namespace sub0squad
