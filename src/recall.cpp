#include "sub0squad/recall.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace sub0squad {

namespace {

std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

std::vector<std::string> words(const std::string& q)
{
    std::vector<std::string> w;
    std::string cur;
    for (char c : q + " ") {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-') cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else { if (cur.size() >= 3) w.push_back(cur); cur.clear(); }
    }
    return w;
}

const char* kindOf(const std::string& name)
{
    if (name == "conversation.md") return "conversation";
    if (name == "gate.txt") return "gate";
    if (name == "prompt.txt") return "prompt";
    if (name.rfind("reply", 0) == 0) return "reply";
    if (name.rfind("briefing-", 0) == 0) return "briefing";
    if (name == "size.json") return "sizing";
    if (name == "report.md") return "report";
    return nullptr;
}

}  // namespace

std::vector<RecallHit> recallSearch(const fs::path& sandbox, const std::string& query, int limit, const std::string& item)
{
    std::vector<RecallHit> hits;
    const auto ws = words(query);
    if (ws.empty()) return hits;
    const int need = std::max<int>(1, static_cast<int>((ws.size() + 1) / 2));
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(sandbox, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec) && (name == "ws" || name == "out" || name == "accepted" || name == "project" || name == "node_modules")) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || !kindOf(name) || it->file_size(ec) > 600000) continue;
        const std::string rel = fs::relative(it->path(), sandbox, ec).generic_string();
        if (!item.empty() && rel.rfind("items/" + item + "/", 0) != 0) continue;
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f);
        const std::string rel = fs::relative(f, sandbox, ec).generic_string();
        int n = 0;
        for (std::string line; std::getline(in, line);) {
            ++n;
            const std::string l = lower(line);
            int score = 0;
            for (const auto& w : ws) if (l.find(w) != std::string::npos) ++score;
            if (score < need) continue;
            const auto b = line.find_first_not_of(" \t");
            std::string text = b == std::string::npos ? std::string{} : line.substr(b);
            if (text.size() > 220) text = text.substr(0, 219) + "…";
            hits.push_back({rel, kindOf(f.filename().string()), n, score, text});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const RecallHit& a, const RecallHit& b) { return a.score > b.score; });
    if (limit > 0 && static_cast<int>(hits.size()) > limit) hits.resize(static_cast<std::size_t>(limit));
    return hits;
}

std::string recallLastConversation(const fs::path& sandbox, const std::string& item, std::size_t maxChars)
{
    std::error_code ec;
    int best = 0;
    for (const auto& e : fs::directory_iterator(sandbox / "items" / item, ec)) {
        const std::string n = e.path().filename().string();
        int a = 0;
        if (n.rfind("attempt-", 0) == 0 && std::sscanf(n.c_str(), "attempt-%d", &a) == 1 && fs::exists(e.path() / "conversation.md", ec)) best = std::max(best, a);
    }
    if (!best) return {};
    std::ifstream in(sandbox / "items" / item / ("attempt-" + std::to_string(best)) / "conversation.md");
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string t = ss.str();
    return t.size() <= maxChars ? t : "[... earlier turns left out]\n" + t.substr(t.size() - maxChars);
}

}  // namespace sub0squad
