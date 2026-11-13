#pragma once
// Tiny test helpers: no framework dependency.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

inline int gFailures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
#define CHECK_EQ(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

#define CHECK_GE(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a >= _b)) { std::fprintf(stderr, "%s:%d: CHECK_GE failed: %s < %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

/// A fresh directory under `$SUB0SQUAD_TEST_TMP`, or `test-tmp` in the working directory (ctest runs in the build directory, so
/// tests never write to shared locations like /tmp). Unix socket paths are limited to ~107 bytes: socket code uses the
/// /proc/self/fd trick, so a long base is fine.
inline std::filesystem::path makeTempDir(const char* tag)
{
    const char* base = std::getenv("SUB0SQUAD_TEST_TMP");
    const std::filesystem::path root = base ? std::filesystem::path{base} : std::filesystem::current_path() / "test-tmp";
    std::filesystem::create_directories(root);
    std::string pattern = (root / (std::string{"s0q-"} + tag + "-XXXXXX")).string();
    char* made = ::mkdtemp(pattern.data());
    if (!made) { std::perror("mkdtemp"); std::exit(2); }
    return made;
}
/// Remove a test's directory tree and never throw: cleanup must not fail a test that passed. A process that is still shutting down can write into the tree
/// for a moment ("Directory not empty" on slower file systems), so it is retried a few times, then left (the build directory's test-tmp is disposable).
inline void removeTree(const std::filesystem::path& p)
{
    for (int i = 0; i < 20; ++i) {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        if (!ec || !std::filesystem::exists(p, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
inline int finish() { if (gFailures) std::fprintf(stderr, "%d check(s) failed\n", gFailures); return gFailures ? 1 : 0; }
