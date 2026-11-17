// The layered document, slice 1 (docs/LAYERED_DOCUMENT.md): the block index that gives every function a stable id, and the issues layer bound to it.
#include "sub0squad/doc_layers.hpp"

#include "json_util.hpp"
#include "sub0squad/outline.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>

namespace sub0squad {

namespace {

constexpr double kRenameJaccard = 0.5;    ///< body similarity at which an unmatched block is the same block renamed
constexpr double kReviveJaccard = 0.8;    ///< body similarity at which a new block brings back a retired id (a block written again)
constexpr double kMergeContainment = 0.6; ///< share of a retired block's tokens found in one live block for it to count as merged into that block
constexpr std::size_t kMinTokensToCompare = 3;   ///< below this a body is too small for similarity to mean anything

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

std::string collapse(const std::string& s)
{
    std::string out;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!out.empty() && out.back() != ' ') out += ' '; }
        else out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '{')) out.pop_back();
    return out;
}

std::string leaf(const std::string& name)
{
    const auto c = name.rfind(':');
    return c == std::string::npos ? name : name.substr(c + 1);
}

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Identifiers and numbers outside literals go to `tokens`; the contents of string and character literals to `literals`. `//` and `#` comments are skipped.
void scan(const std::string& line, std::set<std::string>& tokens, std::set<std::string>& literals)
{
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (c == '"' || c == '\'') {
            std::string lit;
            std::size_t j = i + 1;
            for (; j < line.size() && line[j] != c; ++j) {
                if (line[j] == '\\' && j + 1 < line.size()) ++j;
                lit += line[j];
            }
            if (!lit.empty()) literals.insert(lit);
            i = j + 1;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' || line[j] == '.')) ++j;
            tokens.insert(line.substr(i, j - i));
            i = j;
            continue;
        }
        ++i;
    }
}

double jaccard(const std::set<std::string>& a, const std::set<std::string>& b)
{
    if (a.size() < kMinTokensToCompare || b.size() < kMinTokensToCompare) return 0.0;
    std::size_t both = 0;
    for (const auto& t : a) both += b.count(t);
    return static_cast<double>(both) / static_cast<double>(a.size() + b.size() - both);
}

double containment(const std::set<std::string>& part, const std::set<std::string>& whole)
{
    if (part.size() < kMinTokensToCompare) return 0.0;
    std::size_t both = 0;
    for (const auto& t : part) both += whole.count(t);
    return static_cast<double>(both) / static_cast<double>(part.size());
}

const char* carryName(Carry c)
{
    switch (c) {
    case Carry::kept: return "kept";
    case Carry::resigned: return "resigned";
    case Carry::renamed: return "renamed";
    case Carry::revived: return "revived";
    case Carry::added: return "added";
    case Carry::retired: return "retired";
    case Carry::merged: return "merged";
    }
    return "?";
}

const char* confidenceName(Anchor::Confidence c)
{
    return c == Anchor::Confidence::exact ? "exact" : c == Anchor::Confidence::suspect ? "suspect" : "none";
}

/// Numbered lines `from..to` of `text` (clamped), at most `maxLines`.
std::string numbered(const std::string& text, int from, int to, int maxLines = 80)
{
    const auto lines = splitLines(text);
    from = std::max(1, from);
    to = std::min(static_cast<int>(lines.size()), to);
    std::string out;
    for (int k = from; k <= to && k < from + maxLines; ++k) {
        char n[16];
        std::snprintf(n, sizeof n, "%4d| ", k);
        out += n + lines[static_cast<std::size_t>(k) - 1] + "\n";
    }
    return out;
}

/// How an anchor reads in a summary: "term() (lines 40-52)", "src/a.cpp:12", or that its block is gone.
std::string place(const Anchor& a, const BlockIndex& index)
{
    if (!a.blockId.empty()) {
        if (const Block* b = index.resolve(a.blockId)) return leaf(b->name) + "() (lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ")";
        return "a function that no longer exists";
    }
    if (!a.path.empty() && a.line > 0) return a.path + ":" + std::to_string(a.line);
    return {};
}

}  // namespace

// ---- the block index ----------------------------------------------------------------------------------------------------------------------------------

void BlockIndex::update(const std::string& path, const std::string& text)
{
    if (const auto t = texts_.find(path); t != texts_.end() && t->second == text) return;
    texts_[path] = text;
    const auto lines = splitLines(text);
    const bool python = isPythonPath(path);
    std::vector<Block> fresh;
    std::vector<std::size_t> hashes;
    for (const auto& r : foldRegions(text, python, true)) {
        Block b;
        b.path = path;
        b.name = r.name;
        b.first = r.first;
        b.last = std::max(r.first, r.bodyTo);
        b.signature = collapse(lines[static_cast<std::size_t>(r.first) - 1]);
        std::string whole;
        for (int k = b.first; k <= b.last; ++k) {
            const std::string& l = lines[static_cast<std::size_t>(k) - 1];
            whole += l + "\n";
            if (k > b.first || b.first == b.last) scan(l, b.tokens, b.literals);
        }
        fresh.push_back(std::move(b));
        hashes.push_back(std::hash<std::string>{}(collapse(whole)));
    }
    std::vector<Block> old = std::move(live_[path]);
    std::vector<std::size_t> oldHashes;
    for (const auto& o : old) {   // the old block's text, from the text it was indexed at, is not kept: its hash is
        oldHashes.push_back(hashes_.count(o.id) ? hashes_.at(o.id) : 0);
    }
    std::vector<int> match(fresh.size(), -1);   // index into `old`
    std::vector<bool> used(old.size(), false);
    std::vector<Carry> how(fresh.size(), Carry::added);
    const auto take = [&](std::size_t n, std::size_t o, Carry c) { match[n] = static_cast<int>(o); used[o] = true; how[n] = c; };
    // 1. the same signature and name
    for (std::size_t n = 0; n < fresh.size(); ++n)
        for (std::size_t o = 0; o < old.size(); ++o)
            if (!used[o] && old[o].signature == fresh[n].signature && old[o].name == fresh[n].name) { take(n, o, Carry::kept); break; }
    // 2. the same name, unique on both sides
    for (std::size_t n = 0; n < fresh.size(); ++n) {
        if (match[n] >= 0) continue;
        int oldCount = 0, newCount = 0;
        std::size_t at = 0;
        for (std::size_t o = 0; o < old.size(); ++o) if (!used[o] && old[o].name == fresh[n].name) { ++oldCount; at = o; }
        for (std::size_t m = 0; m < fresh.size(); ++m) if (match[m] < 0 && fresh[m].name == fresh[n].name) ++newCount;
        if (oldCount == 1 && newCount == 1) take(n, at, Carry::resigned);
    }
    // 3. a similar body: the best pairs first
    while (true) {
        double best = kRenameJaccard;
        int bn = -1, bo = -1;
        for (std::size_t n = 0; n < fresh.size(); ++n) {
            if (match[n] >= 0) continue;
            for (std::size_t o = 0; o < old.size(); ++o) {
                if (used[o]) continue;
                if (const double j = jaccard(old[o].tokens, fresh[n].tokens); j >= best) { best = j; bn = static_cast<int>(n); bo = static_cast<int>(o); }
            }
        }
        if (bn < 0) break;
        take(static_cast<std::size_t>(bn), static_cast<std::size_t>(bo), Carry::renamed);
    }
    for (std::size_t n = 0; n < fresh.size(); ++n) {
        if (match[n] >= 0) {
            const Block& o = old[static_cast<std::size_t>(match[n])];
            fresh[n].id = o.id;
            const bool edited = oldHashes[static_cast<std::size_t>(match[n])] != hashes[n];
            if (how[n] != Carry::kept || edited)
                events_.push_back({how[n], o.id, path, fresh[n].name, how[n] == Carry::renamed ? o.name : how[n] == Carry::resigned ? o.signature : std::string{"edited"}, turn_});
            continue;
        }
        // 4. a retired id comes back: the same signature, or a near-identical body (deleted and written again; moved from another file)
        std::string revive;
        double best = kReviveJaccard;
        for (const auto& [id, r] : retired_) {
            if (r.block.signature == fresh[n].signature && r.block.name == fresh[n].name) { revive = id; break; }
            if (const double j = jaccard(r.block.tokens, fresh[n].tokens); j >= best) { best = j; revive = id; }
        }
        if (!revive.empty()) {
            fresh[n].id = revive;
            events_.push_back({Carry::revived, revive, path, fresh[n].name, retired_.at(revive).block.path, turn_});
            retired_.erase(revive);
            continue;
        }
        // 5. a block not seen before
        fresh[n].id = "b" + std::to_string(next_++);
        events_.push_back({Carry::added, fresh[n].id, path, fresh[n].name, {}, turn_});
    }
    for (std::size_t n = 0; n < fresh.size(); ++n) hashes_[fresh[n].id] = hashes[n];
    live_[path] = std::move(fresh);
    // Old blocks left over are retired; one whose body now mostly lives in a single live block was merged into it.
    for (std::size_t o = 0; o < old.size(); ++o) {
        if (used[o]) continue;
        std::string into;
        double best = kMergeContainment;
        for (const auto& [p, blocks] : live_)
            for (const auto& b : blocks)
                if (const double c = containment(old[o].tokens, b.tokens); c >= best) { best = c; into = b.id; }
        events_.push_back({into.empty() ? Carry::retired : Carry::merged, old[o].id, path, old[o].name, into, turn_});
        const std::string id = old[o].id;   // copied first: in `retired_[x.id] = {std::move(x), ...}` the move happens before the key is read
        hashes_.erase(id);
        retired_[id] = {std::move(old[o]), into};
    }
}

const Block* BlockIndex::resolve(const std::string& id) const
{
    std::string at = id;
    for (int hops = 0; hops < 16; ++hops) {
        for (const auto& [p, blocks] : live_)
            for (const auto& b : blocks) if (b.id == at) return &b;
        const auto r = retired_.find(at);
        if (r == retired_.end() || r->second.mergedInto.empty()) return nullptr;
        at = r->second.mergedInto;
    }
    return nullptr;
}

const Block* BlockIndex::at(const std::string& path, int line) const
{
    const auto it = live_.find(path);
    if (it == live_.end()) return nullptr;
    for (const auto& b : it->second) if (line >= b.first && line <= b.last) return &b;
    return nullptr;
}

std::vector<const Block*> BlockIndex::blocks(const std::string& path) const
{
    std::vector<const Block*> out;
    if (const auto it = live_.find(path); it != live_.end()) for (const auto& b : it->second) out.push_back(&b);
    return out;
}

std::vector<const Block*> BlockIndex::all() const
{
    std::vector<const Block*> out;
    for (const auto& [p, blocks] : live_) for (const auto& b : blocks) out.push_back(&b);
    return out;
}

std::string BlockIndex::retiredJson() const
{
    json::Writer w;
    w.beginArray();
    for (const auto& [id, r] : retired_) w.beginObject().kv("id", id).kv("path", r.block.path).kv("name", r.block.name).kv("mergedInto", r.mergedInto).endObject();
    w.endArray();
    return std::string{w.view()};
}

std::string BlockIndex::json() const
{
    json::Writer w;
    w.beginObject().key("blocks").beginArray();
    for (const Block* b : all())
        w.beginObject().kv("id", b->id).kv("path", b->path).kv("name", b->name).kv("signature", b->signature).kv("first", b->first).kv("last", b->last).endObject();
    w.endArray().kvRaw("retired", retiredJson()).key("events").beginArray();
    for (const auto& e : events_)
        w.beginObject().kv("turn", e.turn).kv("carry", carryName(e.carry)).kv("id", e.id).kv("path", e.path).kv("name", e.name).kv("detail", e.detail).endObject();
    w.endArray().endObject();
    return std::string{w.view()};
}

std::vector<BlockEvent> blockHistory(const BlockIndex& index, const std::string& id)
{
    std::set<std::string> ids{id};
    for (bool grew = true; grew;) {   // every id merged into one of these, transitively
        grew = false;
        for (const auto& e : index.events())
            if (e.carry == Carry::merged && ids.count(e.detail) && !ids.count(e.id)) { ids.insert(e.id); grew = true; }
    }
    std::vector<BlockEvent> out;
    for (const auto& e : index.events()) if (ids.count(e.id)) out.push_back(e);
    return out;
}

// ---- the issues layer ---------------------------------------------------------------------------------------------------------------------------------

namespace {

/// The tokens of an expression under test: identifiers, and the operator characters a parser names in its literals.
std::set<std::string> probeTokens(const std::string& expr)
{
    std::set<std::string> probe;
    for (std::size_t i = 0; i < expr.size();) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            std::size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_')) ++j;
            probe.insert(expr.substr(i, j - i));
            i = j;
        } else {
            if (std::string_view{"%^!+-*/<>=&|,"}.find(expr[i]) != std::string_view::npos) probe.insert(std::string(1, expr[i]));
            ++i;
        }
    }
    return probe;
}

/// The suspects of a failing test: the block named like a word of the test's name (3 points), plus for each identifier of the expression under test
/// 1/k points to each of the k blocks whose literals hold it (a rare token says more). An operator character counts only when ONE block holds it: `+`
/// in `expr()` and `unary()` says nothing about which is wrong, and a wrong pointer costs more than none (tier 0, bugs-10). Every block at the top
/// score, in file order, at most three; no evidence, no suspect.
std::vector<Anchor> suspects(const std::string& testName, const std::string& expr, const std::vector<const Block*>& blocks)
{
    std::map<std::string, double> score;
    std::string words = testName;
    for (char& c : words) if (!std::isalnum(static_cast<unsigned char>(c))) c = ' ';
    std::istringstream ws(words);
    for (std::string w; ws >> w;) for (const Block* b : blocks) if (lower(leaf(b->name)) == lower(w)) score[b->id] += 3.0;
    for (const auto& t : probeTokens(expr)) {
        std::vector<const Block*> holders;
        for (const Block* b : blocks) if (b->literals.count(t)) holders.push_back(b);
        const bool word = std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_';
        if (!word && holders.size() != 1) continue;
        for (const Block* b : holders) score[b->id] += 1.0 / static_cast<double>(holders.size());
    }
    double top = 0;
    for (const auto& [id, v] : score) top = std::max(top, v);
    std::vector<Anchor> out;
    if (top <= 0) return out;
    for (const Block* b : blocks)
        if (score.count(b->id) && score[b->id] >= top - 1e-9 && out.size() < 3) out.push_back({b->id, b->path, b->first, Anchor::Confidence::suspect});
    return out;
}

}  // namespace

std::vector<LayerItem> parseIssues(const std::string& verdict, const BlockIndex& index, const std::map<std::string, std::string>& sources)
{
    static const std::regex diag(R"(([\w./+-]+\.[A-Za-z]+):(\d+):(?:(\d+):)? ?(?:(fatal error|error|warning|note): )?(.*))");
    static const std::regex quoted(R"re("((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)')re");
    std::vector<LayerItem> items;
    std::map<std::size_t, std::string> testNames;   // item index -> the failing test's name
    std::map<std::string, std::size_t> byKey;      // a diagnostic's message without numbers -> its item
    std::optional<std::size_t> current;            // the item later context lines belong to (an index: the vector grows)
    std::string clean = verdict;                   // a Windows runner's output carries carriage returns, even inside a line (`test_modulo\r: ...`)
    std::erase(clean, '\r');
    for (const std::string& raw : splitLines(clean)) {
        const auto lead = raw.find_first_not_of(' ');
        const std::string line = lead == std::string::npos ? std::string{} : raw.substr(lead);
        if (line.empty() || line.rfind("GATE ", 0) == 0) { current.reset(); continue; }
        if (line.rfind("FAIL ", 0) == 0) {
            // A failing test: `FAIL 6 - modulo: CHECK_NEAR failed: ev("10%3") is 3.33 but 1 is 1`, or a runner's `FAIL name: error`.
            std::string rest = line.substr(5);
            if (const auto dash = rest.find(" - "); dash != std::string::npos && dash < 8 && std::isdigit(static_cast<unsigned char>(rest[0]))) rest = rest.substr(dash + 3);
            const auto colon = rest.find(": ");
            const std::string name = rest.substr(0, colon), message = colon == std::string::npos ? std::string{} : rest.substr(colon + 2);
            testNames[items.size()] = name;
            current = items.size();
            items.push_back({"issues", 0, "test", "test " + name + (message.empty() ? "" : ": " + message), line, {}, "issues/test/" + name});
            continue;
        }
        std::smatch m;
        if (std::regex_search(line, m, diag) && line.find(" | ") == std::string::npos && line.rfind("location:", 0) != 0 && line.rfind("the test expected", 0) != 0) {
            const std::string kindWord = m[4].str(), message = m[5].str();
            if (kindWord == "note") { if (current) items[*current].detail += "\n" + line; continue; }
            const std::string path = m[1].str();
            const int at = std::atoi(m[2].str().c_str());
            const std::string kind = kindWord == "warning" && message.find("-Werror") == std::string::npos ? "warning" : "error";
            Anchor a{"", path, at, Anchor::Confidence::exact};
            if (const Block* blk = index.at(path, at)) a.blockId = blk->id;
            std::string key = kind + "|";
            for (const char c : message) if (!std::isdigit(static_cast<unsigned char>(c))) key += c;
            if (const auto seen = byKey.find(key); seen != byKey.end()) {
                LayerItem& it = items[seen->second];
                const bool same = std::any_of(it.anchors.begin(), it.anchors.end(), [&](const Anchor& x) { return x.path == path && x.line == at; });
                if (!same) it.anchors.push_back(a);
                current.reset();   // the context of a repeat is not added: the first occurrence carries it
                continue;
            }
            byKey[key] = items.size();
            current = items.size();
            char hash[17];
            std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(std::hash<std::string>{}(key)));
            items.push_back({"issues", 0, kind, kind + " " + path + ":" + std::to_string(at) + ": " + message, line, {a}, "issues/" + kind + "/" + hash});
            continue;
        }
        if (current && raw.size() > 1 && raw[0] == ' ') items[*current].detail += "\n" + raw;   // a context line (the source line, the caret, expected/actual)
        else current.reset();
    }
    // Failing tests are localised once their context is in: the expression is the quoted text of the message, plus the quoted text of the test's own
    // function in the given sources (a runner such as Python's unittest reports `test_modulo: 30.0 != 1`, never the expression; the line it names is
    // usually a shared assertion helper, so the function is found by name).
    const auto blocks = index.all();
    for (const auto& [i, name] : testNames) {
        LayerItem& it = items[i];
        std::string expr;
        const std::string& d = it.detail;
        for (std::sregex_iterator q(d.begin(), d.end(), quoted), end; q != end; ++q) expr += (*q)[1].str() + (*q)[2].str() + " ";
        const std::string testLeaf = name.substr(name.find_last_of(".:") == std::string::npos ? 0 : name.find_last_of(".:") + 1);
        for (const auto& [path, text] : sources) {
            const auto srcLines = splitLines(text);
            for (const auto& r : foldRegions(text, isPythonPath(path), true)) {
                if (leaf(r.name) != testLeaf) continue;
                for (int k = r.first; k <= std::max(r.first, r.bodyTo) && k <= static_cast<int>(srcLines.size()); ++k) {
                    const std::string& src = srcLines[static_cast<std::size_t>(k) - 1];
                    for (std::sregex_iterator q(src.begin(), src.end(), quoted), end; q != end; ++q) expr += (*q)[1].str() + (*q)[2].str() + " ";
                }
            }
        }
        it.anchors = suspects(name, expr, blocks);
    }
    for (std::size_t i = 0; i < items.size(); ++i) items[i].number = static_cast<int>(i) + 1;
    return items;
}


std::string layerSummary(const std::vector<LayerItem>& items, const BlockIndex& index, std::size_t lineChars)
{
    if (items.empty()) return {};
    const std::string layer = items.front().layer, noun = layer.size() > 1 && layer.back() == 's' ? layer.substr(0, layer.size() - 1) : layer;
    std::string keyword = noun;
    for (char& c : keyword) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string s = std::to_string(items.size()) + " " + (items.size() == 1 ? noun : layer) + " (`=== READ: " + keyword + " n ===` shows one in full with its code):\n";
    for (const auto& it : items) {
        std::string where;
        if (it.anchors.size() == 1) where = place(it.anchors[0], index);
        else if (it.anchors.size() > 1) {
            std::set<std::string> names;
            std::string list;
            for (const auto& a : it.anchors) {
                const std::string p = place(a, index);
                const std::string shortName = p.substr(0, p.find(" ("));
                if (names.insert(shortName).second && names.size() <= 4) list += (list.empty() ? "" : ", ") + shortName;
            }
            where = std::to_string(it.anchors.size()) + " places: " + list + (names.size() > 4 ? ", ..." : "");
        }
        const std::string tail = where.empty() ? std::string{} : (it.anchors.front().confidence == Anchor::Confidence::suspect ? " - likely in " : " - in ") + where;
        std::string head = " " + std::to_string(it.number) + ". " + it.title;
        const std::size_t room = lineChars > tail.size() + 20 ? lineChars - tail.size() : 20;
        if (head.size() > room) head = head.substr(0, room - 3) + "...";
        s += head + tail + "\n";
    }
    return s;
}

std::string layerDetail(const LayerItem& item, const BlockIndex& index, const std::map<std::string, std::string>& files)
{
    std::string keyword = item.layer.size() > 1 && item.layer.back() == 's' ? item.layer.substr(0, item.layer.size() - 1) : item.layer;
    for (char& c : keyword) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string s = "=== " + keyword + " " + std::to_string(item.number) + " (" + item.kind + ") ===\n" + item.detail + "\n";
    if (item.anchors.size() > 1) s += "It occurs at " + std::to_string(item.anchors.size()) + " places: the same fix applies at each.\n";
    int shown = 0;
    std::set<std::string> blocksShown;
    for (const auto& a : item.anchors) {
        const auto file = files.find(a.path);
        if (!a.blockId.empty()) {
            const Block* b = index.resolve(a.blockId);
            if (!b) { s += "(it was found in a function that no longer exists: the issue may be stale)\n"; continue; }
            if (!blocksShown.insert(b->id).second) continue;
            if (shown++ >= 3) { s += "... and in " + leaf(b->name) + "() (lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ")\n"; continue; }
            const auto f = files.find(b->path);
            s += std::string{a.confidence == Anchor::Confidence::suspect ? "Likely in " : "In "} + b->path + ", " + leaf(b->name) + "(), now at lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ":\n" +
                 (f == files.end() ? std::string{} : numbered(f->second, b->first, b->last));
        } else if (a.line > 0 && file != files.end()) {
            if (shown++ >= 3) { s += "... and at " + a.path + ":" + std::to_string(a.line) + "\n"; continue; }
            s += "At " + a.path + ":" + std::to_string(a.line) + " (outside any function):\n" + numbered(file->second, a.line - 3, a.line + 3);
        }
    }
    return s;
}

std::string layerFocus(const std::vector<LayerItem>& items, const BlockIndex& index)
{
    std::string s;
    for (const auto& it : items)
        for (const auto& a : it.anchors) {
            if (!a.blockId.empty()) { if (const Block* b = index.resolve(a.blockId)) s += b->path + ":" + std::to_string(b->first) + "\n"; }
            else if (a.line > 0) s += a.path + ":" + std::to_string(a.line) + "\n";
        }
    return s;
}

std::vector<int> parseIssueReads(const std::string& reply)
{
    std::vector<int> out;
    for (std::size_t pos = 0; (pos = reply.find("=== READ:", pos)) != std::string::npos;) {
        const auto eol = reply.find('\n', pos);
        std::string head = reply.substr(pos + 9, (eol == std::string::npos ? reply.size() : eol) - pos - 9);
        pos += 9;
        if (const auto close = head.rfind("==="); close != std::string::npos) head.erase(close);
        std::istringstream in(head);
        std::string word;
        in >> word;
        if (word != "ISSUE" && word != "ISSUES") continue;
        for (std::string n; in >> n;) {
            for (char& c : n) if (c == ',' || c == '#') c = ' ';
            std::istringstream ns(n);
            for (int v = 0; ns >> v;) if (v > 0) out.push_back(v);
        }
    }
    return out;
}

std::string layeredJson(int turn, const std::map<std::string, std::string>& files, const std::map<std::string, int>& revisions, const BlockIndex& index,
                        const std::map<std::string, std::vector<LayerItem>>& layers)
{
    json::Writer w;
    w.beginObject().kv("schema", "layered-doc-1").kv("turn", turn).key("files").beginObject();
    for (const auto& [path, text] : files) w.key(path).beginObject().kv("revision", revisions.count(path) ? revisions.at(path) : 0).kv("text", text).endObject();
    w.endObject();
    // The index's own JSON holds blocks, retired and events: spliced in whole, so there is one writer of that shape.
    const std::string idx = index.json();
    w.raw(std::string_view{idx}.substr(1, idx.size() - 2));
    w.key("layers").beginObject();
    for (const auto& [name, items] : layers) {
        w.key(name).beginArray();
        for (const auto& it : items) {
            w.beginObject().kv("id", it.key).kv("number", it.number).kv("kind", it.kind).kv("title", it.title).kv("detail", it.detail).key("anchors").beginArray();
            for (const auto& a : it.anchors) w.beginObject().kv("blockId", a.blockId).kv("path", a.path).kv("line", a.line).kv("confidence", confidenceName(a.confidence)).endObject();
            w.endArray().endObject();
        }
        w.endArray();
    }
    w.endObject().endObject();
    return std::string{w.view()};
}

}  // namespace sub0squad
