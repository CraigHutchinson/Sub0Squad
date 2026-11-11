#include "sub0squad/edits.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace sub0squad {

namespace {

// Drop a leading line-number column ("  12| ") that a worker copied from the numbered view of a draft into a SEARCH block.
std::string stripNumbers(const std::string& block)
{
    std::istringstream in(block);
    std::string out, line;
    bool first = true;
    while (std::getline(in, line)) {
        std::size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        std::size_t d = i;
        while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
        if (d > i && d < line.size() && line[d] == '|') line = line.substr(d + (d + 1 < line.size() && line[d + 1] == ' ' ? 2 : 1));
        out += (first ? "" : "\n") + line;
        first = false;
    }
    return out;
}

struct LineEdit { std::size_t from = 0, to = 0; bool insertAfter = false; std::string text; };

// `LINES a-b`, `LINES a` or `AFTER a` at the end of an EDIT header's path; returns false when the header has none.
bool parseLineSpec(std::string& path, LineEdit& e)
{
    for (const char* key : {" LINES ", " LINE ", " AFTER "}) {
        const auto at = path.find(key);
        if (at == std::string::npos) continue;
        const std::string spec = path.substr(at + std::string(key).size());
        e.insertAfter = std::string(key) == " AFTER ";
        e.from = static_cast<std::size_t>(std::atol(spec.c_str()));
        const auto dash = spec.find('-');
        e.to = dash == std::string::npos ? e.from : static_cast<std::size_t>(std::atol(spec.c_str() + dash + 1));
        path = path.substr(0, at);
        return true;
    }
    return false;
}

}  // namespace

// Edits instead of whole files: `=== EDIT: path ===` holding one or more `<<<<<<< SEARCH` / `=======` / `>>>>>>> REPLACE` blocks, applied to the files of the
// previous turn. Each SEARCH must match the current text exactly once. On success the result is the complete updated set of files; on failure `error`
// says which edit did not apply, so the next turn can fix that or fall back to whole files.
std::optional<std::map<std::string, std::string>> applyEdits(const std::string& reply, std::map<std::string, std::string> files, const std::set<std::string>& allowed, std::string& error)
{
    std::size_t pos = 0;
    int applied = 0;
    std::map<std::string, std::vector<LineEdit>> pending;   // line edits per file, applied together, highest line first
    std::set<std::string> searched;                          // files that had search edits: a file takes one kind of edit per reply
    for (;;) {
        const auto start = reply.find("=== EDIT:", pos);
        if (start == std::string::npos) break;
        const auto nameEnd = reply.find("===", start + 9);
        if (nameEnd == std::string::npos) { error = "an EDIT header has no closing ==="; return std::nullopt; }
        std::string path = reply.substr(start + 9, nameEnd - start - 9);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t\r\n") + 1);
        std::size_t end = reply.find("=== END EDIT ===", nameEnd);
        const std::size_t nextEdit = reply.find("=== EDIT:", nameEnd);
        if (end == std::string::npos || (nextEdit != std::string::npos && nextEdit < end)) end = nextEdit == std::string::npos ? reply.size() : nextEdit;
        const std::string body = reply.substr(nameEnd + 3, end - nameEnd - 3);
        pos = end;
        LineEdit le;
        const bool byLines = parseLineSpec(path, le);
        if (!allowed.count(path)) { error = "'" + path + "' is not one of the files you may write"; return std::nullopt; }
        auto it = files.find(path);
        if (it == files.end()) { error = "there is no earlier version of '" + path + "' to edit: write the complete file"; return std::nullopt; }
        std::string& text = it->second;
        if (byLines && searched.count(path)) { error = "'" + path + "' has both LINES and SEARCH edits: use one kind per file"; return std::nullopt; }
        if (!byLines) { if (pending.count(path)) { error = "'" + path + "' has both LINES and SEARCH edits: use one kind per file"; return std::nullopt; } searched.insert(path); }
        if (byLines) {   // collected now, applied together below against the numbering of the draft that was shown
            std::string repl = body;
            if (!repl.empty() && repl.front() == '\n') repl.erase(0, 1);
            while (!repl.empty() && (repl.back() == '\n' || repl.back() == '\r')) repl.pop_back();
            le.text = stripNumbers(repl);
            pending[path].push_back(le);
            ++applied;
            continue;
        }
        std::size_t b = 0;
        for (;;) {
            const auto s0 = body.find("<<<<<<< SEARCH", b);
            if (s0 == std::string::npos) break;
            const auto sepAt = body.find("\n=======", s0);
            const auto repAt = body.find("\n>>>>>>> REPLACE", s0);
            if (sepAt == std::string::npos || repAt == std::string::npos || repAt < sepAt) { error = "an edit block of '" + path + "' is not closed with ======= and >>>>>>> REPLACE"; return std::nullopt; }
            const auto searchFrom = body.find('\n', s0) + 1;
            std::string search = searchFrom <= sepAt ? body.substr(searchFrom, sepAt - searchFrom) : std::string{};
            const std::size_t replFrom = body.find('\n', sepAt + 1) == std::string::npos ? repAt : body.find('\n', sepAt + 1) + 1;
            const std::string repl = replFrom <= repAt ? body.substr(replFrom, repAt - replFrom) : std::string{};
            b = repAt + 1;
            if (search.empty()) { error = "an edit of '" + path + "' has an empty SEARCH"; return std::nullopt; }
            const std::string searchPlain = stripNumbers(search);
            const auto at = text.find(search) != std::string::npos ? text.find(search) : text.find(searchPlain);
            if (searchPlain != search && text.find(search) == std::string::npos && at != std::string::npos) search = searchPlain;
            if (at == std::string::npos) { error = "the SEARCH text in '" + path + "' does not appear in the file (first line: " + search.substr(0, search.find('\n')).substr(0, 80) + ")"; return std::nullopt; }
            if (text.find(search, at + 1) != std::string::npos) { error = "the SEARCH text in '" + path + "' appears more than once: include more lines so it matches one place"; return std::nullopt; }
            text.replace(at, search.size(), repl);
            ++applied;
        }
    }
    for (auto& [path, edits] : pending) {   // highest line first, so earlier numbers stay valid
        std::vector<std::string> lines;
        {
            std::istringstream in(files[path]);
            for (std::string l; std::getline(in, l);) lines.push_back(l);
        }
        std::sort(edits.begin(), edits.end(), [](const LineEdit& a, const LineEdit& b) { return a.from > b.from; });
        std::size_t lastFrom = lines.size() + 2;
        for (const auto& e : edits) {
            const std::size_t lo = e.insertAfter ? e.from : e.from - (e.from ? 1 : 0), hi = e.insertAfter ? e.from : e.to;   // 0-based [lo, hi)
            if (e.from == 0 && !e.insertAfter) { error = "a LINES edit of '" + path + "' names line 0: lines start at 1"; return std::nullopt; }
            if (hi > lines.size() || hi < lo) { error = "a LINES edit of '" + path + "' names lines " + std::to_string(e.from) + "-" + std::to_string(e.to) + " but the draft has " + std::to_string(lines.size()) + " lines"; return std::nullopt; }
            if (hi > lastFrom - 1 + (e.insertAfter ? 1 : 0) && !e.insertAfter) { error = "two LINES edits of '" + path + "' overlap"; return std::nullopt; }
            std::vector<std::string> repl;
            if (!e.text.empty()) { std::istringstream in(e.text); for (std::string l; std::getline(in, l);) repl.push_back(l); }
            lines.erase(lines.begin() + static_cast<long>(lo), lines.begin() + static_cast<long>(hi));
            lines.insert(lines.begin() + static_cast<long>(lo), repl.begin(), repl.end());
            lastFrom = lo + 1;
        }
        std::string joined;
        for (const auto& l : lines) joined += l + "\n";
        files[path] = joined;
    }
    if (!applied) { error = "the reply holds no edit blocks"; return std::nullopt; }
    return files;
}

}  // namespace sub0squad
