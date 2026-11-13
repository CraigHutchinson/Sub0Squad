#include "sub0squad/scenario_compare.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sub0squad {

namespace {

struct Row { std::string platform, element; int passed = 0, trials = 0; double seconds = 0, tokens = 0; };
struct File { std::string build, engine, executor, treatment; std::vector<Row> rows; };

/// One bound of the 95 % Wilson score interval; `sign` -1 for the lower bound, +1 for the upper.
double wilson(int passes, int trials, double sign)
{
    if (trials <= 0) return sign < 0 ? 0.0 : 1.0;
    const double z = 1.96, n = trials, p = static_cast<double>(passes) / n;
    const double centre = p + z * z / (2 * n), spread = z * std::sqrt(p * (1 - p) / n + z * z / (4 * n * n));
    return std::clamp((centre + sign * spread) / (1 + z * z / n), 0.0, 1.0);
}

std::expected<File, std::string> read(std::string_view text, const char* side)
{
    File f;
    bool schema = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view k = field.unescaped_key();
                auto v = field.value();
                if (k == "schema") schema = json::toString(v) == "scenario-results-1";
                else if (k == "build") f.build = json::toString(v);
                else if (k == "engine") f.engine = json::toString(v);
                else if (k == "executor") f.executor = json::toString(v);
                else if (k == "treatment") f.treatment = json::toString(v);
                else if (k == "results") {
                    for (auto element : v.get_array()) {
                        Row r;
                        for (auto x : element.get_object()) {
                            const std::string_view rk = x.unescaped_key();
                            auto rv = x.value();
                            if (rk == "platform") r.platform = json::toString(rv);
                            else if (rk == "element") r.element = json::toString(rv);
                            else if (rk == "passed") r.passed = static_cast<int>(rv.get_int64());
                            else if (rk == "trials") r.trials = static_cast<int>(rv.get_int64());
                            else if (rk == "meanSeconds") r.seconds = rv.get_double();
                            else if (rk == "meanTokens") r.tokens = rv.get_double();
                        }
                        if (r.platform.empty() || r.element.empty()) throw std::runtime_error("a result has no platform or element");
                        if (r.trials < 1 || r.passed < 0 || r.passed > r.trials) throw std::runtime_error("result " + r.platform + "/" + r.element + ": passed " + std::to_string(r.passed) + " of " + std::to_string(r.trials) + " trials is not possible");
                        f.rows.push_back(std::move(r));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{side} + ": not a scenario-results file: " + e.what()); }
    catch (const std::runtime_error& e) { return std::unexpected(std::string{side} + ": " + e.what()); }
    if (!schema) return std::unexpected(std::string{side} + ": the file's schema is not scenario-results-1");
    return f;
}

}  // namespace

double wilsonUpperBound(int passes, int trials) { return wilson(passes, trials, +1); }

std::expected<ScenarioComparison, std::string> compareScenarioResults(std::string_view a, std::string_view b)
{
    const auto fa = read(a, "A"), fb = read(b, "B");
    if (!fa) return std::unexpected(fa.error());
    if (!fb) return std::unexpected(fb.error());
    ScenarioComparison c;
    c.buildA = fa->build; c.buildB = fb->build; c.engineA = fa->engine; c.engineB = fb->engine;
    c.executorA = fa->executor; c.executorB = fb->executor; c.treatmentA = fa->treatment; c.treatmentB = fb->treatment;
    const auto find = [](const File& f, const Row& r) -> const Row* { for (const auto& x : f.rows) if (x.platform == r.platform && x.element == r.element) return &x; return nullptr; };
    const auto delta = [&](const Row* ra, const Row* rb) {
        ScenarioDelta d;
        const Row& any = ra ? *ra : *rb;
        d.platform = any.platform; d.element = any.element;
        if (ra) { d.passedA = ra->passed; d.trialsA = ra->trials; d.secondsA = ra->seconds; d.tokensA = ra->tokens; }
        if (rb) { d.passedB = rb->passed; d.trialsB = rb->trials; d.secondsB = rb->seconds; d.tokensB = rb->tokens; }
        d.lowerA = wilson(d.passedA, d.trialsA, -1); d.upperA = wilson(d.passedA, d.trialsA, +1);
        d.lowerB = wilson(d.passedB, d.trialsB, -1); d.upperB = wilson(d.passedB, d.trialsB, +1);
        if (!ra) d.verdict = "only in B";
        else if (!rb) d.verdict = "only in A";
        else if (d.upperB < d.lowerA) { d.verdict = "regression"; ++c.regressions; }
        else if (d.lowerB > d.upperA) { d.verdict = "improvement"; ++c.improvements; }
        else d.verdict = "no evidence of change";
        c.rows.push_back(std::move(d));
    };
    for (const auto& r : fa->rows) delta(&r, find(*fb, r));
    for (const auto& r : fb->rows) if (!find(*fa, r)) delta(nullptr, &r);
    return c;
}

std::string ScenarioComparison::text() const
{
    std::string out;
    char line[400];
    const auto side = [&](const char* name, const std::string& build, const std::string& engine, const std::string& executor, const std::string& treatment) {
        std::snprintf(line, sizeof line, "%s: build %s, engine %s, executor %s, treatment %s\n", name, build.c_str(), engine.c_str(), executor.c_str(), treatment.c_str());
        out += line;
    };
    side("A", buildA, engineA, executorA, treatmentA);
    side("B", buildB, engineB, executorB, treatmentB);
    if (engineA != engineB) out += "note: the engines differ, so a difference is the engine's as much as the build's\n";
    std::snprintf(line, sizeof line, "%-12s %-27s %9s %13s %9s %13s %9s %9s  %s\n", "platform", "element", "A passed", "A interval", "B passed", "B interval", "seconds", "tokens", "verdict");
    out += line;
    for (const auto& d : rows) {
        const auto passed = [](int p, int t) { return t ? std::to_string(p) + "/" + std::to_string(t) : std::string{"-"}; };
        const auto interval = [](double lo, double hi, int t) { char b[32]; if (t) std::snprintf(b, sizeof b, "%.2f..%.2f", lo, hi); else std::snprintf(b, sizeof b, "-"); return std::string{b}; };
        const auto change = [](double x, double y, bool both) { char b[32]; if (both && x > 0) std::snprintf(b, sizeof b, "%+.0f%%", 100.0 * (y - x) / x); else std::snprintf(b, sizeof b, "-"); return std::string{b}; };
        const bool both = d.trialsA > 0 && d.trialsB > 0;
        std::snprintf(line, sizeof line, "%-12s %-27s %9s %13s %9s %13s %9s %9s  %s\n", d.platform.c_str(), d.element.c_str(), passed(d.passedA, d.trialsA).c_str(), interval(d.lowerA, d.upperA, d.trialsA).c_str(),
                      passed(d.passedB, d.trialsB).c_str(), interval(d.lowerB, d.upperB, d.trialsB).c_str(), change(d.secondsA, d.secondsB, both).c_str(), change(d.tokensA, d.tokensB, both).c_str(), d.verdict.c_str());
        out += line;
    }
    std::snprintf(line, sizeof line, "%d regression(s), %d improvement(s), %zu scenario(s) compared (a difference counts only when the 95 %% intervals do not overlap)\n", regressions, improvements, rows.size());
    out += line;
    return out;
}

}  // namespace sub0squad
