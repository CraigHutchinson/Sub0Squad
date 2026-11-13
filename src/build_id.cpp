#include "sub0squad/build_id.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace sub0squad {

namespace {

std::string& version() { static std::string v = "unknown"; return v; }

/// FNV-1a over the bytes of the running program's file, as hex; "" when it cannot be read. The file is only READ (never executed again: catalogue row 32).
/// A daemon started before a rebuild still reads the file it was started from, so its fingerprint stays the old one.
std::string fingerprint()
{
    std::FILE* f = std::fopen("/proc/self/exe", "rb");
    if (!f) return {};
    std::uint64_t h = 1469598103934665603ULL;
    unsigned char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
        for (std::size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 1099511628211ULL; }
    std::fclose(f);
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
    return hex;
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
