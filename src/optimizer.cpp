// `optimize-job`: take ONE job in isolation (its inputs fixed: the accepted outputs of its dependencies) and spin on the input. Each prompt
// treatment is tried for a number of rounds; every attempt goes through the same trusted gate (without publishing), and the report compares
// pass rate, failing tests, tokens and time, so the input that works best on the current weak model is chosen from data (docs/OPTIMIZATION.md).
#include "json_util.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/sandbox.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
std::string arg(const std::vector<std::string>& a, const std::string& key, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}
double numberAfter(const std::string& text, const char* key)
{
    const auto p = text.find(key);
    return p == std::string::npos ? 0.0 : std::atof(text.c_str() + p + std::string{key}.size());
}
struct Row { std::string treatment; int attempt = 0; bool generated = false, passed = false; int failing = -1, tests = -1; double tokensIn = 0, tokensOut = 0, seconds = 0; };
}  // namespace

int runOptimizeJob(const std::vector<std::string>& args)
{
    const fs::path dir = arg(args, "workflow-dir");
    const std::string item = arg(args, "item"), engine = arg(args, "engine");
    const int rounds = std::max(1, std::atoi(arg(args, "rounds", "1").c_str()));
    const std::string maxTokens = arg(args, "max-tokens", "3500");
    const std::string repairTurns = arg(args, "repair-turns", "0");
    const std::string timeoutSec = arg(args, "timeout", "1700");
    std::vector<std::string> treatments;
    {
        std::stringstream ss(arg(args, "treatments", "baseline,index,planfirst"));
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) treatments.push_back(t);
    }
    if (dir.empty() || item.empty() || engine.empty()) { std::fprintf(stderr, "optimize-job: --workflow-dir, --item and --engine are required\n"); return 64; }
    std::vector<Row> rows;
    int attempt = 200;  // far from real attempts; stepping by 2 keeps attempts from reading each other's gate output as feedback
    for (const auto& t : treatments) {
        for (int r = 1; r <= rounds; ++r, attempt += 2) {
            Row row;
            row.treatment = t;
            row.attempt = attempt;
            const auto began = std::chrono::steady_clock::now();
            const int rc = runLlmAgent({"--workflow-dir", dir.string(), "--item", item, "--tier", "local", "--attempt", std::to_string(attempt), "--engine", engine,
                                        "--max-tokens", maxTokens, "--treatment", t, "--repair-turns", repairTurns, "--timeout", timeoutSec});
            row.generated = rc == 0;
            if (rc == 0) {
                const int g = runHarnessGate({"--workflow-dir", dir.string(), "--item", item, "--attempt", std::to_string(attempt), "--no-publish", "1"});
                row.passed = g == 0;
                const std::string gate = slurp(dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "gate.txt");
                const auto of = gate.find(" of ");
                if (gate.rfind("GATE FAIL: ", 0) == 0 && of != std::string::npos) { row.failing = std::atoi(gate.c_str() + 11); row.tests = std::atoi(gate.c_str() + of + 4); }
                else if (row.passed) { row.failing = 0; }
            }
            const std::string metrics = slurp(dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "agent.json");
            row.tokensIn = numberAfter(metrics, "\"tokensIn\":");
            row.tokensOut = numberAfter(metrics, "\"tokensOut\":");
            row.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            std::printf("RESULT treatment=%s attempt=%d generated=%d passed=%d failing=%d/%d tokens_in=%.0f tokens_out=%.0f seconds=%.0f\n", t.c_str(), attempt, row.generated, row.passed, row.failing, row.tests,
                        row.tokensIn, row.tokensOut, row.seconds);
            std::fflush(stdout);
            rows.push_back(row);
        }
    }
    json::Writer w;
    w.beginObject().kv("schema", "optimization-1").kv("item", item).kv("engine", engine).key("rows").beginArray();
    for (const auto& r : rows)
        w.beginObject().kv("treatment", r.treatment).kv("attempt", r.attempt).kvBool("generated", r.generated).kvBool("passed", r.passed).kv("failing", r.failing).kv("tests", r.tests)
            .kv("tokensIn", r.tokensIn).kv("tokensOut", r.tokensOut).kv("seconds", r.seconds).endObject();
    w.endArray().endObject();
    std::ofstream(dir / "items" / item / "optimization.json") << w.take();
    std::printf("\n%-10s %7s %7s %12s %10s %10s %8s\n", "treatment", "rounds", "passed", "fail(avg)", "tokens in", "tokens out", "seconds");
    for (const auto& t : treatments) {
        int n = 0, ok = 0, failN = 0; double fail = 0, tin = 0, tout = 0, secs = 0;
        for (const auto& r : rows) if (r.treatment == t) { ++n; ok += r.passed; if (r.failing >= 0) { fail += r.failing; ++failN; } tin += r.tokensIn; tout += r.tokensOut; secs += r.seconds; }
        std::printf("%-10s %7d %7d %12.1f %10.0f %10.0f %8.0f\n", t.c_str(), n, ok, failN ? fail / failN : -1.0, n ? tin / n : 0.0, n ? tout / n : 0.0, n ? secs / n : 0.0);
    }
    return 0;
}

}  // namespace sub0squad
