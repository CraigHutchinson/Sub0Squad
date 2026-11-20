#include "sub0squad/build_id.hpp"

#include "sub0platform/proc.hpp"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>

namespace sub0squad {

namespace {

std::string& version() { static std::string v = "unknown"; return v; }

/// FNV-1a over the bytes of the running program's file, as hex; "" when it cannot be read. The file is only READ (never executed again: catalogue row 32).
/// A daemon started before a rebuild still reads the file it was started from, so its fingerprint stays the old one.
std::string fingerprint()
{
    const auto self = sub0platform::selfExecutable();
    if (self.empty()) return {};
    std::ifstream in(self, std::ios::binary);   // a path, not a char*: on Windows the path is wide
    if (!in) return {};
    std::uint64_t h = 1469598103934665603ULL;
    char buf[65536];
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        for (std::streamsize i = 0; i < in.gcount(); ++i) { h ^= static_cast<unsigned char>(buf[i]); h *= 1099511628211ULL; }
        if (!in) break;
    }
    return std::format("{:016x}", h);
}

}  // namespace

void setBuildId(std::string id) { if (!id.empty()) version() = std::move(id); }

std::string buildId()
{
    if (const char* forced = std::getenv("SUB0SQUAD_BUILD_ID"); forced && *forced) return forced;
    static const std::string print = fingerprint();   // once per process: the version is generated when the build is configured, the fingerprint is of what was linked
    return print.empty() ? version() : version() + "+" + print;
}

}  // namespace sub0squad
