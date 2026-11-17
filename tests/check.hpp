#pragma once
// Tiny test helpers: no framework dependency.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <cstdint>
#include <random>
#include <string>
#include <thread>

inline int gFailures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
#define CHECK_EQ(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

#define CHECK_GE(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a >= _b)) { std::fprintf(stderr, "%s:%d: CHECK_GE failed: %s < %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

/// A fresh directory under `$SUB0SQUAD_TEST_TMP`, or `test-tmp` in the working directory (ctest runs in the build directory, so
/// tests never write to shared locations like /tmp). Local socket paths are limited to ~107 bytes: the POSIX socket code uses the
/// /proc/self/fd trick, so a long base is fine there; on Windows the path itself must fit.
inline std::filesystem::path makeTempDir(const char* tag)
{
    const char* base = std::getenv("SUB0SQUAD_TEST_TMP");
    const std::filesystem::path root = base ? std::filesystem::path{base} : std::filesystem::current_path() / "test-tmp";
    std::filesystem::create_directories(root);
    // create_directory fails on a name that exists, so the first name it makes is this test's alone (what mkdtemp does, on every system).
    std::mt19937_64 random{std::random_device{}() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    const char* letters = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix(6, 'a');
        for (char& c : suffix) c = letters[random() % 36];
        const std::filesystem::path p = root / (std::string{"s0q-"} + tag + "-" + suffix);
        std::error_code ec;
        if (std::filesystem::create_directory(p, ec)) return p;
    }
    std::fprintf(stderr, "cannot make a temporary directory under %s\n", root.string().c_str());
    std::exit(2);
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
