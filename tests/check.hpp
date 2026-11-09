#pragma once
// Tiny test helpers: no framework dependency.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

inline int gFailures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
#define CHECK_EQ(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

/// A fresh short directory (unix socket paths are limited to ~107 bytes, so it lives under /tmp or $SUB0SQUAD_TEST_TMP).
inline std::filesystem::path makeTempDir(const char* tag)
{
    const char* base = std::getenv("SUB0SQUAD_TEST_TMP");
    std::string pattern = std::string{base ? base : "/tmp"} + "/s0q-" + tag + "-XXXXXX";
    char* made = ::mkdtemp(pattern.data());
    if (!made) { std::perror("mkdtemp"); std::exit(2); }
    return made;
}
inline int finish() { if (gFailures) std::fprintf(stderr, "%d check(s) failed\n", gFailures); return gFailures ? 1 : 0; }
