#include "sub0squad/contract.hpp"

#include "sub0squad/outline.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace sub0squad {

namespace {

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> v;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) { if (!l.empty() && l.back() == '\r') l.pop_back(); v.push_back(l); }
    return v;
}
std::string joined(const std::vector<std::string>& v) { std::string out; for (const auto& l : v) out += l + "\n"; return out; }
std::string trimmed(const std::string& l) { const auto a = l.find_first_not_of(" \t"); return a == std::string::npos ? std::string{} : l.substr(a, l.find_last_not_of(" \t") - a + 1); }
bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
std::string leaf(const std::string& name) { const auto c = name.find_last_of(":."); return c == std::string::npos ? name : name.substr(c + 1); }

/// Where `name(` stands in `line` as a whole identifier, or npos.
std::size_t callAt(const std::string& line, const std::string& name)
{
    for (std::size_t at = line.find(name); at != std::string::npos; at = line.find(name, at + 1)) {
        std::size_t after = at + name.size();
        while (after < line.size() && line[after] == ' ') ++after;
        if ((at == 0 || !isIdent(line[at - 1])) && after < line.size() && line[after] == '(') return at;
    }
    return std::string::npos;
}

/// A declaration without its default arguments (`f(int a = 1, bool b = false)` -> `f(int a, bool b)`): what a definition may say.
std::string withoutDefaults(const std::string& s)
{
    std::string out;
    int depth = 0;
    bool skipping = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (skipping) {
            if (c == '(' || c == '{') ++depth;
            else if ((c == ')' || c == '}') && depth > 1) --depth;
            else if (depth == 1 && (c == ',' || c == ')')) { skipping = false; while (!out.empty() && out.back() == ' ') out.pop_back(); }
            if (skipping) continue;
        }
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == '=' && depth == 1 && i + 1 < s.size() && s[i + 1] != '=') { skipping = true; continue; }
        out += c;
    }
    return out;
}

std::string signatureOfLine(std::string line)
{
    line = trimmed(line);
    while (!line.empty() && (line.back() == '{' || line.back() == ':' || line.back() == ';' || line.back() == ' ')) line.pop_back();
    return line;
}

/// The innermost NAMED namespace open before line `index` (0-based): braces are counted from the top, and namespaces nest (an anonymous one inside
/// `namespace calc` closes without closing `calc`).
std::string namespaceBefore(const std::vector<std::string>& v, std::size_t index)
{
    std::vector<std::pair<std::string, int>> open;   // (name, the depth it was opened at)
    int depth = 0;
    for (std::size_t i = 0; i < index && i < v.size(); ++i) {
        const std::string& l = v[i];
        if (l.rfind("namespace", 0) == 0 && l.find('{') != std::string::npos) { std::string name = l.substr(9, l.find('{') - 9); open.emplace_back(trimmed(name), depth); }
        for (const char c : l) { if (c == '{') ++depth; else if (c == '}') --depth; }
        while (!open.empty() && depth <= open.back().second) open.pop_back();
    }
    for (auto it = open.rbegin(); it != open.rend(); ++it) if (!it->first.empty()) return it->first;
    return {};
}

/// The index (0-based) of the line before which a function is added: the last column-0 `}` when the file opens `namespace scope`, else the end.
std::size_t insertionLine(const std::vector<std::string>& v, const std::string& scope, bool python)
{
    if (python || scope.empty()) return v.size();
    bool opens = false;
    for (const auto& l : v) if (l.find("namespace " + scope) != std::string::npos) opens = true;
    if (!opens) return v.size();
    for (std::size_t i = v.size(); i > 0; --i) if (!v[i - 1].empty() && v[i - 1][0] == '}') return i - 1;
    return v.size();
}

std::string qualified(const std::string& signature, const std::string& name, const std::string& scope)
{
    const auto at = callAt(signature, name);
    return at == std::string::npos || scope.empty() ? signature : signature.substr(0, at) + scope + "::" + signature.substr(at);
}

}  // namespace

std::vector<ContractFunction> contractFunctions(const std::string& path, const std::string& stubText, const std::vector<std::string>& headers)
{
    std::vector<ContractFunction> out;
    const bool python = isPythonPath(path);
    const auto v = linesOf(stubText);
    for (const auto& r : foldRegions(stubText, python, true)) {
        if (r.name.empty() || r.first < 1 || static_cast<std::size_t>(r.first) > v.size()) continue;
        const std::string& decl = v[static_cast<std::size_t>(r.first) - 1];
        const std::string line = trimmed(decl);
        ContractFunction f;
        f.name = leaf(r.name);
        if (python) { if (decl.find_first_not_of(" \t") != 0 || f.name[0] == '_' || line.rfind("class ", 0) == 0) continue; }
        else if (line.rfind("static ", 0) == 0 || f.name == "main" || line.rfind("class ", 0) == 0 || line.rfind("struct ", 0) == 0 || line.rfind("namespace", 0) == 0) continue;
        if (std::any_of(out.begin(), out.end(), [&](const ContractFunction& e) { return e.name == f.name; })) continue;
        if (!python) f.scope = namespaceBefore(v, static_cast<std::size_t>(r.first) - 1);
        f.signature = signatureOfLine(decl);
        for (const auto& header : headers)
            for (const auto& hl : linesOf(header)) {
                const std::string t = trimmed(hl);
                if (t.empty() || t.back() != ';' || t[0] == '/' || t[0] == '#' || t.rfind("return", 0) == 0 || callAt(t, f.name) == std::string::npos) continue;
                f.signature = withoutDefaults(signatureOfLine(t));
            }
        out.push_back(std::move(f));
    }
    return out;
}

std::string withContractDeclarations(const std::string& declared, const std::string& path, const std::vector<ContractFunction>& contract)
{
    auto v = linesOf(declared);
    const bool python = isPythonPath(path);
    std::vector<std::string> written;   // functions the skeleton writes out itself, body and all: they are defined, and stay as written
    for (const auto& r : foldRegions(declared, python, true)) if (!r.name.empty()) written.push_back(leaf(r.name));
    for (const auto& f : contract) {
        bool found = std::find(written.begin(), written.end(), f.name) != written.end();
        if (found) continue;
        for (auto& l : v) {
            const auto first = l.find_first_not_of(" \t");
            if (first == std::string::npos || l.compare(first, 2, "@@") != 0 || callAt(l, f.name) == std::string::npos) continue;
            l = l.substr(0, first) + "@@ " + f.signature;   // the contract's signature, whatever the model wrote
            found = true;
            break;
        }
        if (found) continue;
        const std::size_t at = insertionLine(v, f.scope, python);
        const bool inScope = at < v.size();
        v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), "@@ " + (inScope || python ? f.signature : qualified(f.signature, f.name, f.scope)));
    }
    return joined(v);
}

std::vector<ContractFunction> undefinedContract(const std::string& text, const std::string& path, const std::vector<ContractFunction>& contract)
{
    std::vector<std::string> defined;
    for (const auto& r : foldRegions(text, isPythonPath(path), true)) if (!r.name.empty()) defined.push_back(leaf(r.name));
    std::vector<ContractFunction> missing;
    for (const auto& f : contract) if (std::find(defined.begin(), defined.end(), f.name) == defined.end()) missing.push_back(f);
    return missing;
}

std::string withStubFunction(const std::string& text, const std::string& path, const std::string& signature, const std::string& scope, const std::vector<std::string>& functions)
{
    const bool python = isPythonPath(path);
    auto v = linesOf(text);
    const std::size_t at = insertionLine(v, scope, python);
    const bool inScope = at < v.size();
    std::string name = signature.substr(0, signature.find('('));
    while (!name.empty() && name.back() == ' ') name.pop_back();
    { std::size_t b = name.size(); while (b > 0 && isIdent(name[b - 1])) --b; name = name.substr(b); }
    const std::string sig = inScope || python ? signature : qualified(signature, name, scope);
    const auto expanded = linesOf(expandSkeleton("@@ " + sig + "\n", path).text);
    std::vector<std::string> block;
    block.push_back("");
    for (const auto& fn : functions) { for (const auto& l : linesOf(fn)) block.push_back(l); block.push_back(""); }
    bool taking = false;
    for (const auto& l : expanded) { if (!taking && trimmed(l).rfind(sig, 0) == 0 && (trimmed(l).empty() || trimmed(l).back() != ';')) taking = true; if (taking) block.push_back(l); }
    if (inScope) block.push_back("");
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), block.begin(), block.end());
    if (!python && text.find("<stdexcept>") == std::string::npos) {
        std::size_t last = v.size();
        for (std::size_t i = 0; i < v.size(); ++i) if (v[i].rfind("#include", 0) == 0) last = i;
        v.insert(last == v.size() ? v.begin() : v.begin() + static_cast<std::ptrdiff_t>(last) + 1, "#include <stdexcept>");
    }
    return joined(v);
}

std::vector<MemberDeclaration> memberDeclarations(const std::string& text, const std::string& path)
{
    std::vector<MemberDeclaration> out;
    const auto ext = path.rfind('.') == std::string::npos ? std::string{} : path.substr(path.rfind('.'));
    if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".hpp" && ext != ".h" && ext != ".c") return out;
    const auto v = linesOf(text);
    struct Open { std::string name; int depth; std::size_t line; };
    std::vector<Open> classes;   // the classes open at this line, innermost last, with the brace depth their members are at
    std::string pending;         // a `class X` line whose `{` is on a later line
    std::size_t pendingLine = 0;
    int depth = 0;
    const auto without = [](std::string s, const char* word) {
        const std::string w{word};
        for (std::size_t at = s.find(w); at != std::string::npos; at = s.find(w, at)) {
            const bool whole = (at == 0 || !isIdent(s[at - 1])) && (at + w.size() >= s.size() || !isIdent(s[at + w.size()]));
            if (whole) s.erase(at, w.size()); else ++at;
        }
        return trimmed(s);
    };
    for (std::size_t i = 0; i < v.size(); ++i) {
        const std::string t = trimmed(v[i]);
        const bool classLine = (t.rfind("class ", 0) == 0 || t.rfind("struct ", 0) == 0) && (t.empty() || t.back() != ';' || t.find('{') != std::string::npos);
        if (classLine) {
            std::size_t b = t.find(' ') + 1, e = b;
            while (e < t.size() && isIdent(t[e])) ++e;
            pending = t.substr(b, e - b);
            pendingLine = i;
        }
        if (!classes.empty() && depth == classes.back().depth && !classLine && !t.empty() && t.back() == ';' && t.find('{') == std::string::npos) {
            // A member line that ends in `;`: a function declaration when an identifier stands before its first `(` and nothing before that says otherwise.
            const auto paren = t.find('(');
            std::size_t e = paren;
            while (e != std::string::npos && e > 0 && t[e - 1] == ' ') --e;
            std::size_t b = e;
            while (b != std::string::npos && b > 0 && (isIdent(t[b - 1]) || t[b - 1] == '~')) --b;
            const std::string prefix = paren == std::string::npos ? std::string{} : t.substr(0, b);
            int close = -1, level = 0;
            if (paren != std::string::npos) for (std::size_t k = paren; k < t.size(); ++k) { if (t[k] == '(') ++level; else if (t[k] == ')' && --level == 0) { close = static_cast<int>(k); break; } }
            const std::string tail = close < 0 ? std::string{} : t.substr(static_cast<std::size_t>(close) + 1);
            const bool function = paren != std::string::npos && b < e && close > 0 && prefix.find_first_of("=(<") == std::string::npos && prefix.find("operator") == std::string::npos &&
                                  t.rfind("friend ", 0) != 0 && t.rfind("using ", 0) != 0 && t.rfind("typedef ", 0) != 0 && t.rfind("return", 0) != 0 && t[0] != '/' &&
                                  tail.find('=') == std::string::npos;   // `= default`, `= delete`, `= 0`: nothing to define
            if (function) {
                MemberDeclaration m;
                m.className = classes.back().name;
                m.name = t.substr(b, e - b);
                std::string ret = prefix;
                for (const char* word : {"virtual", "static", "explicit", "inline", "[[nodiscard]]"}) ret = without(ret, word);
                std::string after = tail.substr(0, tail.find(';'));
                for (const char* word : {"override", "final"}) after = without(after, word);
                m.signature = (ret.empty() ? std::string{} : ret + " ") + m.className + "::" + m.name + withoutDefaults(t.substr(paren, static_cast<std::size_t>(close) - paren + 1)) + (after.empty() ? std::string{} : " " + after);
                m.scope = namespaceBefore(v, classes.back().line);
                out.push_back(std::move(m));
            }
        }
        for (const char c : v[i]) {
            if (c == '{') { ++depth; if (!pending.empty()) { classes.push_back({pending, depth, pendingLine}); pending.clear(); } }
            else if (c == '}') { --depth; while (!classes.empty() && depth < classes.back().depth) classes.pop_back(); }
            else if (c == ';' && !pending.empty()) pending.clear();   // a forward declaration
        }
    }
    return out;
}

std::vector<MemberDeclaration> undefinedMembers(const std::string& text, const std::string& path)
{
    std::vector<std::string> defined;
    for (const auto& r : foldRegions(text, false, true)) if (!r.name.empty()) defined.push_back(r.name);
    for (const auto& l : linesOf(text)) {
        const std::string t = trimmed(l);
        if (t.rfind("@@", 0) != 0) continue;
        const auto paren = t.find('(');
        std::size_t b = paren == std::string::npos ? 0 : paren;
        while (b > 0 && (isIdent(t[b - 1]) || t[b - 1] == ':' || t[b - 1] == '~')) --b;
        if (paren != std::string::npos) defined.push_back(t.substr(b, paren - b));
    }
    std::vector<MemberDeclaration> missing;
    for (auto& m : memberDeclarations(text, path)) {
        const std::string q = m.qualified();
        const bool has = std::any_of(defined.begin(), defined.end(), [&](const std::string& d) { return d == q || (d.size() > q.size() + 2 && d.compare(d.size() - q.size() - 2, 2, "::") == 0 && d.compare(d.size() - q.size(), q.size(), q) == 0); });
        if (!has) missing.push_back(std::move(m));
    }
    return missing;
}

std::vector<std::string> overloadedMembers(const std::string& text, const std::string& path)
{
    std::vector<std::string> seen, twice;
    for (const auto& m : memberDeclarations(text, path)) {
        const std::string q = m.qualified();
        if (std::find(seen.begin(), seen.end(), q) == seen.end()) seen.push_back(q);
        else if (std::find(twice.begin(), twice.end(), q) == twice.end()) twice.push_back(q);
    }
    return twice;
}

std::string withMemberDeclarations(const std::string& declared, const std::string& path)
{
    auto v = linesOf(declared);
    std::vector<std::string> added;
    for (const auto& m : undefinedMembers(declared, path)) {
        if (std::find(added.begin(), added.end(), m.qualified()) != added.end()) continue;
        added.push_back(m.qualified());
        const std::size_t at = insertionLine(v, m.scope, false);
        const bool inScope = at < v.size() || m.scope.empty();
        std::string signature = m.signature;
        if (const auto c = signature.find(m.className + "::"); !inScope && c != std::string::npos) signature.insert(c, m.scope + "::");
        v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), "@@ " + signature);
    }
    return joined(v);
}

std::string prototypeScope(const std::string& text, const std::string& name)
{
    const std::string proto = prototypeOf(text, name);
    if (proto.empty()) return {};
    const auto v = linesOf(text);
    for (std::size_t i = 0; i < v.size(); ++i) if (signatureOfLine(v[i]) == proto && trimmed(v[i]).back() == ';') return namespaceBefore(v, i);
    return {};
}

std::string prototypeOf(const std::string& text, const std::string& name)
{
    for (const auto& l : linesOf(text)) {
        const std::string t = trimmed(l);
        if (t.empty() || t.back() != ';' || t[0] == '/' || t.rfind("return", 0) == 0) continue;
        const auto at = callAt(t, name);
        if (at == std::string::npos || at == 0 || t.find('=') < at || t.find(')') == std::string::npos) continue;
        if (t.substr(0, at).find('(') != std::string::npos) continue;   // a call inside another expression, not a declaration
        return signatureOfLine(t);
    }
    return {};
}

}  // namespace sub0squad
