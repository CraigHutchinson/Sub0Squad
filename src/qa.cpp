#include "sub0squad/qa.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace sub0squad {

namespace {

struct Line { int no; std::string text; };

std::string normalise(std::string l)
{
    if (const auto c = l.find("//"); c != std::string::npos) l.erase(c);
    std::string out;
    bool space = false;
    for (char ch : l) {
        if (std::isspace(static_cast<unsigned char>(ch))) { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += ch;
    }
    return out;
}

struct Source { std::string path; std::vector<std::string> raw; std::vector<Line> lines; };

bool isSource(const fs::path& p)
{
    const std::string ext = p.extension().string();
    if (ext != ".js" && ext != ".mjs") return false;
    const std::string s = p.generic_string();
    return s.find("/node_modules/") == std::string::npos && s.find("/tests/") == std::string::npos && s.find(".test.") == std::string::npos;
}

// "function name(", "export function name(", "async function name(", "const name = (...) =>"
std::string declaredName(const std::string& t)
{
    for (const char* k : {"export function ", "export async function ", "async function ", "function "})
        if (t.rfind(k, 0) == 0) { const std::string r = t.substr(std::string{k}.size()); return r.substr(0, r.find_first_of("( ")); }
    for (const char* k : {"export const ", "const "})
        if (t.rfind(k, 0) == 0 && t.find("=>") != std::string::npos) { const std::string r = t.substr(std::string{k}.size()); return r.substr(0, r.find_first_of(" =")); }
    return {};
}

}  // namespace

std::vector<QaFinding> qaScan(const fs::path& root)
{
    std::vector<Source> srcs;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || !isSource(it->path())) continue;
        Source s;
        s.path = fs::relative(it->path(), root, ec).generic_string();
        std::ifstream in(it->path());
        int n = 0;
        for (std::string line; std::getline(in, line);) {
            ++n;
            s.raw.push_back(line);
            const std::string t = normalise(line);
            if (t.size() >= 12) s.lines.push_back({n, t});   // short lines (braces, closers) say nothing about duplication
        }
        srcs.push_back(std::move(s));
    }
    std::sort(srcs.begin(), srcs.end(), [](const Source& a, const Source& b) { return a.path < b.path; });
    std::vector<QaFinding> out;

    // duplication: windows of five meaningful lines that appear in more than one place, merged into runs
    constexpr int K = 5;
    std::map<std::string, std::vector<std::pair<int, int>>> windows;   // text -> (source index, start index in lines)
    for (std::size_t si = 0; si < srcs.size(); ++si)
        for (std::size_t i = 0; i + K <= srcs[si].lines.size(); ++i) {
            std::string w;
            for (int k = 0; k < K; ++k) w += srcs[si].lines[i + k].text + "\n";
            windows[w].push_back({static_cast<int>(si), static_cast<int>(i)});
        }
    struct Hit { int a, b, ia, ib; };
    std::vector<Hit> hits;
    for (const auto& [w, at] : windows) {
        if (at.size() < 2 || at.size() > 6) continue;
        for (std::size_t x = 0; x < at.size(); ++x)
            for (std::size_t y = x + 1; y < at.size(); ++y)
                if (at[x].first != at[y].first || std::abs(at[x].second - at[y].second) >= K) hits.push_back({at[x].first, at[y].first, at[x].second, at[y].second});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& l, const Hit& r) { return std::tie(l.a, l.b, l.ia, l.ib) < std::tie(r.a, r.b, r.ia, r.ib); });
    for (std::size_t i = 0; i < hits.size();) {
        std::size_t j = i;
        while (j + 1 < hits.size() && hits[j + 1].a == hits[i].a && hits[j + 1].b == hits[i].b && hits[j + 1].ia == hits[j].ia + 1 && hits[j + 1].ib == hits[j].ib + 1) ++j;
        const int runLines = static_cast<int>(j - i) + K;
        if (runLines >= 6) {
            const Source &A = srcs[static_cast<std::size_t>(hits[i].a)], &B = srcs[static_cast<std::size_t>(hits[i].b)];
            QaFinding f;
            f.axis = "duplication"; f.severity = runLines >= 12 ? "warn" : "info";
            f.where = {A.path + ":" + std::to_string(A.lines[static_cast<std::size_t>(hits[i].ia)].no) + "-" + std::to_string(A.lines[static_cast<std::size_t>(hits[j].ia + K - 1)].no),
                       B.path + ":" + std::to_string(B.lines[static_cast<std::size_t>(hits[i].ib)].no) + "-" + std::to_string(B.lines[static_cast<std::size_t>(hits[j].ib + K - 1)].no)};
            f.what = "about " + std::to_string(runLines) + " lines appear twice";
            f.suggestion = "move the shared code into one module and import it from both places";
            out.push_back(std::move(f));
        }
        i = j + 1;
    }
    // the same function declared in more than one file: workers wrote the same helper independently
    std::map<std::string, std::vector<std::string>> declared;
    for (const auto& s : srcs) {
        std::set<std::string> seen;
        for (std::size_t i = 0; i < s.raw.size(); ++i) {
            const std::string name = declaredName(normalise(s.raw[i]));
            if (name.empty() || name.size() < 3 || !seen.insert(name).second) continue;
            declared[name].push_back(s.path + ":" + std::to_string(i + 1));
        }
    }
    for (const auto& [name, where] : declared)
        if (where.size() >= 2) {
            std::set<std::string> files;
            for (const auto& w : where) files.insert(w.substr(0, w.find(':')));
            if (files.size() < 2) continue;
            out.push_back({"duplication", "warn", where, "'" + name + "' is declared in " + std::to_string(files.size()) + " files", "declare it once in a shared module and import it"});
        }
    // simplicity: a function that runs past forty lines
    for (const auto& s : srcs)
        for (std::size_t i = 0; i < s.raw.size(); ++i) {
            const std::string name = declaredName(normalise(s.raw[i]));
            if (name.empty() || s.raw[i].find('{') == std::string::npos) continue;
            int depth = 0;
            std::size_t end = i;
            for (std::size_t k = i; k < s.raw.size(); ++k) {
                for (char c : s.raw[k]) { if (c == '{') ++depth; else if (c == '}') --depth; }
                end = k;
                if (depth <= 0 && k >= i) break;
            }
            if (end - i + 1 > 40) out.push_back({"simplicity", "info", {s.path + ":" + std::to_string(i + 1) + "-" + std::to_string(end + 1)}, "'" + name + "' is " + std::to_string(end - i + 1) + " lines long", "split it into steps that can be read and tested on their own"});
        }
    return out;
}

fs::path qaCollect(const fs::path& sandbox, const fs::path& into)
{
    std::error_code ec;
    fs::remove_all(into, ec);
    fs::create_directories(into, ec);
    for (const auto& e : fs::directory_iterator(sandbox / "items", ec)) {
        const fs::path acc = e.path() / "accepted";
        if (fs::exists(acc, ec)) fs::copy(acc, into, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }
    return into;
}

std::string qaText(const std::vector<QaFinding>& fs_)
{
    if (fs_.empty()) return "no findings\n";
    std::string o;
    for (const auto& f : fs_) {
        o += "[" + f.severity + "] " + f.axis + ": " + f.what + "\n";
        for (const auto& w : f.where) o += "    " + w + "\n";
        o += "    suggestion: " + f.suggestion + "\n";
    }
    return o;
}

}  // namespace sub0squad
