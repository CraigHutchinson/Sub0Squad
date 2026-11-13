#include "sub0squad/mends.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace sub0squad {

namespace {

// The text of registry/mends.json (test_mends holds the file and this equal, byte for byte).
const char* const kBuiltin = R"JSON({"schema":"mends-1","mends":[
{"name":"undefined-contract-symbol","class":"M","match":["undefined reference to '([A-Za-z_][A-Za-z0-9_:]*)","'([A-Za-z_][A-Za-z0-9_:]*)' was not declared in this scope","cannot import name '([A-Za-z_][A-Za-z0-9_]*)'","has no attribute '([A-Za-z_][A-Za-z0-9_]*)'"],"requires":"contract","action":"add-section","maxPerItem":2,"fixture":"tests/data/mends/undefined-reference.txt"},
{"name":"helper-never-defined","class":"M","match":["'([^']+)' (?:declared 'static'|used) but never defined"],"requires":"prototype","action":"add-section","maxPerItem":2,"fixture":"tests/data/mends/never-defined.txt"},
{"name":"unused-entity","class":"M","match":["variable '([A-Za-z_][A-Za-z0-9_]*)' set but not used","unused variable '([A-Za-z_][A-Za-z0-9_]*)'","unused parameter '([A-Za-z_][A-Za-z0-9_]*)'"],"requires":"location","action":"silence-unused","maxPerItem":2,"fixture":"tests/data/mends/unused-but-set.txt"}
]}
)JSON";

bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

/// Typographic quotes (GCC writes U+2018 and U+2019 in a UTF-8 locale) and backticks (the linker's) as plain '.
std::string plainQuotes(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xE2 && static_cast<unsigned char>(s[i + 1]) == 0x80 && (static_cast<unsigned char>(s[i + 2]) == 0x98 || static_cast<unsigned char>(s[i + 2]) == 0x99)) { out += '\''; i += 2; }
        else out += s[i] == '`' ? '\'' : s[i];
    }
    return out;
}

}  // namespace

std::string MendMatch::toJson() const
{
    json::Writer w;
    w.beginObject().kv("name", name).kv("action", action).kv("requires", requires_).kv("symbol", symbol).kv("file", file).kv("line", line).kv("column", column).kv("error", error).kv("maxPerItem", maxPerItem).endObject();
    return w.take();
}

std::optional<MendMatch> MendMatch::fromJson(std::string_view text)
{
    MendMatch m;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "name") m.name = json::toString(v);
                else if (k == "action") m.action = json::toString(v);
                else if (k == "requires") m.requires_ = json::toString(v);
                else if (k == "symbol") m.symbol = json::toString(v);
                else if (k == "file") m.file = json::toString(v);
                else if (k == "line") m.line = static_cast<int>(v.get_int64());
                else if (k == "column") m.column = static_cast<int>(v.get_int64());
                else if (k == "error") m.error = json::toString(v);
                else if (k == "maxPerItem") m.maxPerItem = static_cast<int>(v.get_int64());
            }
        });
    } catch (const simdjson::simdjson_error&) { return std::nullopt; }
    if (m.name.empty() || m.action.empty()) return std::nullopt;
    return m;
}

const std::string& builtinMendsJson() { static const std::string text = kBuiltin; return text; }

const std::vector<Mend>& builtinMends()
{
    static const std::vector<Mend> table = [] { auto t = parseMends(kBuiltin); return t ? *t : std::vector<Mend>{}; }();
    return table;
}

std::expected<std::vector<Mend>, std::string> parseMends(std::string_view text)
{
    std::vector<Mend> out;
    bool schema = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "schema") schema = json::toString(v) == "mends-1";
                else if (k == "mends") {
                    for (auto element : v.get_array()) {
                        Mend m;
                        for (auto x : element.get_object()) {
                            const std::string_view mk = x.unescaped_key();
                            auto mv = x.value();
                            if (mk == "name") m.name = json::toString(mv);
                            else if (mk == "class") m.errorClass = json::toString(mv);
                            else if (mk == "match") { for (auto p : mv.get_array()) m.match.push_back(json::toString(p.value())); }
                            else if (mk == "requires") m.requires_ = json::toString(mv);
                            else if (mk == "action") m.action = json::toString(mv);
                            else if (mk == "maxPerItem") m.maxPerItem = static_cast<int>(mv.get_int64());
                            else if (mk == "fixture") m.fixture = json::toString(mv);
                            else throw std::runtime_error("unknown key '" + std::string{mk} + "' in a mend");
                        }
                        out.push_back(std::move(m));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) { return std::unexpected(std::string{"bad mends table: "} + e.what()); }
    catch (const std::runtime_error& e) { return std::unexpected(std::string{"bad mends table: "} + e.what()); }
    if (!schema) return std::unexpected("a mends table needs \"schema\": \"mends-1\"");
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Mend& m = out[i];
        const std::string where = "mend '" + m.name + "': ";
        if (m.name.empty() || m.name.size() > 40 || !std::all_of(m.name.begin(), m.name.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-'; })) return std::unexpected("mend " + std::to_string(i + 1) + ": the name '" + m.name + "' must be 1 to 40 characters of a-z, 0-9 and '-'");
        for (std::size_t e = 0; e < i; ++e) if (out[e].name == m.name) return std::unexpected(where + "the name is used twice");
        if (m.action != "add-section" && m.action != "silence-unused") return std::unexpected(where + "action '" + m.action + "' is not a mend action (add-section, silence-unused)");
        if (m.requires_ != "contract" && m.requires_ != "prototype" && m.requires_ != "location") return std::unexpected(where + "requires '" + m.requires_ + "' must be contract, prototype or location");
        if (m.action == "silence-unused" && m.requires_ != "location") return std::unexpected(where + "silence-unused edits a place in a file, so it requires location");
        if (m.maxPerItem < 1 || m.maxPerItem > 5) return std::unexpected(where + "maxPerItem must be 1..5");
        if (m.match.empty()) return std::unexpected(where + "needs at least one pattern");
        for (const auto& p : m.match) {
            if (p.size() > 300) return std::unexpected(where + "a pattern is longer than 300 characters");
            try { const std::regex re(p); if (re.mark_count() < 1) return std::unexpected(where + "pattern \"" + p + "\" captures no name (group 1 is the name the error is about)"); }
            catch (const std::regex_error& e) { return std::unexpected(where + "pattern \"" + p + "\" is not a regular expression: " + e.what()); }
        }
    }
    return out;
}

std::vector<Mend> mergedMends(std::vector<Mend> base, const std::vector<Mend>& extra)
{
    for (const auto& m : extra) {
        const auto it = std::find_if(base.begin(), base.end(), [&](const Mend& b) { return b.name == m.name; });
        if (it != base.end()) *it = m; else base.push_back(m);
    }
    return base;
}

std::string firstErrorLine(const std::string& output)
{
    std::istringstream in(output);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("GATE FAIL", 0) == 0 && line.find(": error") == std::string::npos && line.find("undefined reference") == std::string::npos) continue;   // the gate's own headline
        std::string low = line;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low.find("error") == std::string::npos && low.find("undefined reference") == std::string::npos) continue;
        return plainQuotes(line);
    }
    return {};
}

std::optional<MendMatch> matchMend(const std::vector<Mend>& table, const std::string& output)
{
    const std::string line = firstErrorLine(output);
    if (line.empty()) return std::nullopt;
    for (const Mend& m : table) {
        for (const auto& p : m.match) {
            std::smatch found;
            try { if (!std::regex_search(line, found, std::regex(p)) || found.size() < 2) continue; } catch (const std::regex_error&) { continue; }
            MendMatch r;
            r.name = m.name; r.action = m.action; r.requires_ = m.requires_; r.maxPerItem = m.maxPerItem;
            r.symbol = found[1].str();
            if (const auto paren = r.symbol.find('('); paren != std::string::npos) {   // a captured declaration (`double f(int)`): the function's name
                std::size_t end = paren;
                while (end > 0 && r.symbol[end - 1] == ' ') --end;
                std::size_t begin = end;
                while (begin > 0 && (isIdent(r.symbol[begin - 1]) || r.symbol[begin - 1] == ':')) --begin;
                r.symbol = r.symbol.substr(begin, end - begin);
            }
            if (r.symbol.empty()) continue;
            // `path:LINE:COL: error:` (GCC and Clang): the place, when the line starts with one
            static const std::regex place(R"(([A-Za-z0-9_./+-]+\.[A-Za-z0-9]+):(\d+):(\d+):)");
            if (std::smatch at; std::regex_search(line, at, place)) { r.file = at[1].str(); r.line = std::atoi(at[2].str().c_str()); r.column = std::atoi(at[3].str().c_str()); }
            r.error = line.substr(0, 400);
            return r;
        }
    }
    return std::nullopt;
}

std::optional<std::string> silenceUnused(const std::string& text, const std::string& path, int line, int column, const std::string& name)
{
    const auto dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string{} : path.substr(dot);
    if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".hpp" && ext != ".h" && ext != ".c") return std::nullopt;
    if (line < 1 || name.empty()) return std::nullopt;
    std::vector<std::string> lines;
    { std::istringstream in(text); for (std::string l; std::getline(in, l);) lines.push_back(l); }
    if (static_cast<std::size_t>(line) > lines.size()) return std::nullopt;
    std::string& l = lines[static_cast<std::size_t>(line) - 1];
    const auto wordAt = [&](std::size_t at) { return at + name.size() <= l.size() && l.compare(at, name.size(), name) == 0 && (at == 0 || !isIdent(l[at - 1])) && (at + name.size() == l.size() || !isIdent(l[at + name.size()])); };
    std::size_t at = column >= 1 ? static_cast<std::size_t>(column) - 1 : std::string::npos;
    if (at == std::string::npos || !wordAt(at)) {   // the column is off (tabs, a multi-byte character before it): the name as a whole word on that line
        at = std::string::npos;
        for (std::size_t i = l.find(name); i != std::string::npos; i = l.find(name, i + 1)) if (wordAt(i)) { at = i; break; }
        if (at == std::string::npos) return std::nullopt;
    }
    // Back from the name to where its declaration begins: after a `(` or a `,` of a parameter list, after `;` or `{`, or at the start of the line.
    std::size_t begin = 0;
    int angle = 0;
    bool afterComma = false;
    for (std::size_t i = at; i > 0; --i) {
        const char c = l[i - 1];
        if (c == '>') ++angle;
        else if (c == '<' && angle > 0) --angle;
        else if (angle == 0 && (c == '(' || c == ';' || c == '{')) { begin = i; break; }
        else if (angle == 0 && c == ',') { begin = i; afterComma = true; break; }
    }
    if (afterComma) {   // only a PARAMETER may take the attribute after a comma; in `int a, b;` it may not
        int depth = 0;
        bool inParens = false;
        for (std::size_t i = begin; i > 0; --i) { const char c = l[i - 1]; if (c == ')') ++depth; else if (c == '(') { if (depth == 0) { inParens = true; break; } --depth; } }
        if (!inParens) return std::nullopt;
    }
    while (begin < at && (l[begin] == ' ' || l[begin] == '\t')) ++begin;
    if (l.compare(begin, 16, "[[maybe_unused]]") == 0) return std::nullopt;   // already mended: the same error again is not this mend's to fix
    l.insert(begin, "[[maybe_unused]] ");
    std::string out;
    for (const auto& x : lines) out += x + "\n";
    if (!text.empty() && text.back() != '\n') out.pop_back();
    return out;
}

}  // namespace sub0squad
