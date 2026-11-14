#pragma once
/// @file calculator_fake.hpp
/// A fake model for the C++ calculator blueprint, for tests that run whole workflows through a real daemon with no model (test_heal, test_nearpass): a file
/// request gets the reference file, a skeleton request the declared reference, a section request that function. Two hooks let a test reproduce what a real
/// model did: `declare` rewrites the skeleton of a file, `section` the reply to one section's request (it is told how often that section was asked).
#include "agent_bench.hpp"
#include "e2e.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>

namespace calcfake {

using namespace e2e;
namespace fs = std::filesystem;

inline std::string field(const std::string& s, const std::string& key)
{
    const std::string pat = "\"" + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    if (s[p] == '"') { auto e = s.find('"', p + 1); return s.substr(p + 1, e - p - 1); }
    return s.substr(p, s.find_first_of(",}", p) - p);
}
inline std::string between(const std::string& s, const std::string& from, const std::string& to)
{
    const auto a = s.find(from);
    if (a == std::string::npos) return {};
    const auto b = s.find(to, a + from.size());
    return s.substr(a + from.size(), b == std::string::npos ? std::string::npos : b - a - from.size());
}
inline std::vector<std::string> paths(const std::string& list)
{
    std::vector<std::string> v;
    std::size_t at = 0;
    while (at < list.size()) {
        const auto e = list.find(", ", at);
        v.push_back(list.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos) break;
        at = e + 2;
    }
    return v;
}
inline int occurrences(const std::string& s, const std::string& part) { int n = 0; for (auto at = s.find(part); at != std::string::npos; at = s.find(part, at + part.size())) ++n; return n; }
inline std::vector<std::string> conversations(const fs::path& attempt)
{
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(attempt, ec)) if (e.path().filename().string().rfind("conversation-", 0) == 0) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

/// The fake model for the C++ calculator. With no hook its skeleton of src/calc.cpp has NO line for `evaluate` (the defect of run-v10).
struct Calculator
{
    struct File { std::string text, declared; std::map<std::string, std::string> functions; };
    std::map<std::string, File> files;
    std::vector<std::string> sectionsAsked;
    /// The skeleton the model declares for `path`, given the reference's (every worthwhile function an `@@` line). Empty: the run-v10 defect above.
    std::function<std::string(const std::string& path, const std::string& declared)> declare;
    /// The reply to the `nth` (1-based) request for section `name` (its last name) of `file`, given the reference function `text`. Empty: `text`.
    std::function<std::string(const std::string& name, const std::string& file, const std::string& text, int nth)> section;
    std::map<std::string, int> asked;
    const File& of(const std::string& path)
    {
        auto it = files.find(path);
        if (it != files.end()) return it->second;
        File f;
        f.text = sub0squad::findHarness("cpp-cmake")->mockFile(path, false);
        const auto names = bench::sectionNames(f.text, false);
        f.declared = names.empty() ? f.text : bench::skeletonOf(f.text, names, f.functions, false);
        if (declare) f.declared = declare(path, f.declared);
        else if (path == "src/calc.cpp") {   // the defect of run-v10: the entry point is not in the section list
            std::string kept;
            for (const auto& l : bench::lines(f.declared)) if (!(l.find("@@") != std::string::npos && l.find("evaluate(") != std::string::npos)) kept += l + "\n";
            CHECK(kept != f.declared && f.functions.count("evaluate") == 1);
            f.declared = kept;
        }
        return files.emplace(path, std::move(f)).first->second;
    }
    fake::Reply answer(const fake::Request& rq)
    {
        const std::string& ask = rq.last();
        const auto list = paths(between(ask, "Files to write: ", "\n"));
        if (ask.find("Declare the STRUCTURE") != std::string::npos) {
            std::string reply;
            for (const auto& p : list) reply += bench::skeletonBlock(p, of(p).declared);
            return {reply};
        }
        if (const auto own = ask.rfind("You own `"); own != std::string::npos) {
            const std::string name = ask.substr(own + 9, ask.find('`', own + 9) - own - 9);
            std::string file = ask.substr(ask.find(" in ", own) + 4);
            file = file.substr(0, file.find_first_of(" \n"));
            if (!file.empty() && file.back() == '.') file.pop_back();
            sectionsAsked.push_back(name);
            const auto& f = of(file);
            const std::string last = name.rfind(':') == std::string::npos ? name : name.substr(name.rfind(':') + 1);   // `Parser::term` is the reference's `term`
            const auto it = f.functions.find(last);
            if (it == f.functions.end()) return {"I do not know that function."};
            const int nth = ++asked[file + "#" + last];
            return {"=== EDIT: " + file + " SYMBOL " + name + " ===\n" + (section ? section(last, file, it->second, nth) : it->second) + "=== END EDIT ===\n"};
        }
        std::string reply;
        for (const auto& p : list) reply += "=== FILE: " + p + " ===\n" + of(p).text + "=== END FILE ===\n";
        return {reply};
    }
};

/// Run the C++ calculator with the fork executor on `process`; returns the final status JSON. `dir` receives the workflow directory.
inline std::string runWorkflow(Squad& sq, Calculator& calc, const std::string& process, fs::path& dir, bool expectSuccess = true, const std::function<void(const std::string& status)>& onStatus = {})
{
    std::mutex mu;
    fake::Engine engine([&](const fake::Request& rq, int) { std::lock_guard<std::mutex> l(mu); return calc.answer(rq); });
    const std::string profile = engine.profile(8192) + ",slots=4";
    const Result sub = sq.cli({"project", "submit", "--task", "calculator-cpp", "--agent", "llm", "--engine", profile, "--executor", "fork", "--process", process, "--max-tokens", "3000", "--repair-turns", "1", "--size-budget", "0"});
    CHECK_EQ(sub.exitCode, 0);
    const std::string wf = sub.out.substr(0, sub.out.find('\n'));
    std::string status;
    for (int i = 0; i < 2400; ++i) {   // eight minutes: every gate is a real C++ build
        status = sq.cli({"project", "status", wf, "--json"}).out;
        if (onStatus) onStatus(status);
        const std::string state = field(status, "state");
        if (state != "running" && !state.empty()) break;
        sleepMs(200);
    }
    if (expectSuccess && field(status, "state") != "succeeded") std::fprintf(stderr, "[%s] workflow %s ended %s: %s\n", process.c_str(), wf.c_str(), field(status, "state").c_str(), field(status, "note").c_str());
    dir = sq.home() / "state" / "workflows" / wf;
    return status;
}


}  // namespace calcfake
