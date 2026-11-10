#include "sub0squad/edits.hpp"

namespace sub0squad {

// Edits instead of whole files: `=== EDIT: path ===` holding one or more `<<<<<<< SEARCH` / `=======` / `>>>>>>> REPLACE` blocks, applied to the files of the
// previous turn. Each SEARCH must match the current text exactly once. On success the result is the complete updated set of files; on failure `error`
// says which edit did not apply, so the next turn can fix that or fall back to whole files.
std::optional<std::map<std::string, std::string>> applyEdits(const std::string& reply, std::map<std::string, std::string> files, const std::set<std::string>& allowed, std::string& error)
{
    std::size_t pos = 0;
    int applied = 0;
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
        if (!allowed.count(path)) { error = "'" + path + "' is not one of the files you may write"; return std::nullopt; }
        auto it = files.find(path);
        if (it == files.end()) { error = "there is no earlier version of '" + path + "' to edit: write the complete file"; return std::nullopt; }
        std::string& text = it->second;
        std::size_t b = 0;
        for (;;) {
            const auto s0 = body.find("<<<<<<< SEARCH", b);
            if (s0 == std::string::npos) break;
            const auto sepAt = body.find("\n=======", s0);
            const auto repAt = body.find("\n>>>>>>> REPLACE", s0);
            if (sepAt == std::string::npos || repAt == std::string::npos || repAt < sepAt) { error = "an edit block of '" + path + "' is not closed with ======= and >>>>>>> REPLACE"; return std::nullopt; }
            const auto searchFrom = body.find('\n', s0) + 1;
            const std::string search = searchFrom <= sepAt ? body.substr(searchFrom, sepAt - searchFrom) : std::string{};
            const std::size_t replFrom = body.find('\n', sepAt + 1) == std::string::npos ? repAt : body.find('\n', sepAt + 1) + 1;
            const std::string repl = replFrom <= repAt ? body.substr(replFrom, repAt - replFrom) : std::string{};
            b = repAt + 1;
            if (search.empty()) { error = "an edit of '" + path + "' has an empty SEARCH"; return std::nullopt; }
            const auto at = text.find(search);
            if (at == std::string::npos) { error = "the SEARCH text in '" + path + "' does not appear in the file (first line: " + search.substr(0, search.find('\n')).substr(0, 80) + ")"; return std::nullopt; }
            if (text.find(search, at + 1) != std::string::npos) { error = "the SEARCH text in '" + path + "' appears more than once: include more lines so it matches one place"; return std::nullopt; }
            text.replace(at, search.size(), repl);
            ++applied;
        }
    }
    if (!applied) { error = "the reply holds no edit blocks"; return std::nullopt; }
    return files;
}

}  // namespace sub0squad
