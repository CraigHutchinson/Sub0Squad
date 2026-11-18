#include "sub0squad/turn_control.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace sub0squad {

namespace {

/// The complete lines of `text` (a last line without its newline is still being written and is left out).
std::vector<std::string> completeLines(std::string_view text)
{
    std::vector<std::string> v;
    std::size_t pos = 0;
    for (;;) {
        const auto nl = text.find('\n', pos);
        if (nl == std::string_view::npos) break;
        std::string line(text.substr(pos, nl - pos));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        v.push_back(std::move(line));
        pos = nl + 1;
    }
    return v;
}

std::string trimmed(const std::string& l)
{
    const auto a = l.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return l.substr(a, l.find_last_not_of(" \t") - a + 1);
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

/// The offset after a `  12| ` number column, or npos.
std::size_t numberColumn(const std::string& line)
{
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    std::size_t d = i;
    while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
    if (d == i || d >= line.size() || line[d] != '|') return std::string::npos;
    return d + (d + 1 < line.size() && line[d + 1] == ' ' ? 2 : 1);
}

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> v;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}

}  // namespace

Runaway detectRunaway(std::string_view reply, const std::map<std::string, std::string>& drafts, const RunawayLimits& lim)
{
    const auto lines = completeLines(reply);

    // 1. A SEARCH block that goes on and on: the worker is copying the file to change a few lines of it.
    if (lim.maxSearchLines > 0) {
        int inSearch = -1;
        for (const auto& l : lines) {
            if (startsWith(l, "<<<<<<< SEARCH")) inSearch = 0;
            else if (startsWith(l, "=======") || startsWith(l, ">>>>>>> REPLACE") || startsWith(l, "=== ")) inSearch = -1;
            else if (inSearch >= 0 && ++inSearch > lim.maxSearchLines)
                return {"long-search", "Your reply was stopped: its SEARCH block copied more than " + std::to_string(lim.maxSearchLines) +
                                           " lines of the file. Do not copy the file. Name the lines to replace instead (the numbers are in the draft shown):\n"
                                           "=== EDIT: <path> LINES a-b ===\n<only the new text for those lines>\n=== END EDIT ===\nKeep each edit to the few lines that change."};
        }
    }

    // 2. The same line again and again, or the same short block of lines.
    if (lim.maxRepeats > 0 && !lines.empty()) {
        const std::string last = trimmed(lines.back());
        if (last.size() >= 8) {
            int run = 0;
            for (auto it = lines.rbegin(); it != lines.rend() && trimmed(*it) == last; ++it) ++run;
            if (run >= lim.maxRepeats)
                return {"repeat", "Your reply was stopped: it repeated the line `" + last.substr(0, 80) + "` " + std::to_string(run) + " times. Reply again without repeating yourself: only the edit that is needed."};
        }
        for (std::size_t k = 2; k <= 6; ++k) {   // a cycle of k lines, repeated
            const std::size_t need = k * static_cast<std::size_t>(std::max(4, lim.maxRepeats / 2));
            if (lines.size() < need) break;
            bool cycle = true, substantial = false;
            for (std::size_t i = 0; i < need - k && cycle; ++i) cycle = lines[lines.size() - 1 - i] == lines[lines.size() - 1 - i - k];
            for (std::size_t i = 0; i < k; ++i) if (trimmed(lines[lines.size() - 1 - i]).size() >= 8) substantial = true;
            if (cycle && substantial)
                return {"repeat", "Your reply was stopped: it repeated the same " + std::to_string(k) + " lines over and over. Reply again without repeating yourself: only the edit that is needed."};
        }
    }

    // 3. A whole-file block that reproduces the draft: a rewrite where an edit was asked for.
    if (lim.restateMinLines > 0 && !drafts.empty()) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!startsWith(lines[i], "=== FILE:")) continue;
            std::string path = lines[i].substr(9);
            if (const auto close = path.rfind("==="); close != std::string::npos) path.erase(close);
            path = trimmed(path);
            const auto d = drafts.find(path);
            if (d == drafts.end()) continue;
            const auto draft = linesOf(d->second);
            int same = 0, total = 0;
            for (std::size_t j = i + 1; j < lines.size() && !startsWith(lines[j], "=== "); ++j) {
                const std::size_t at = j - i - 1;
                ++total;
                if (at < draft.size() && lines[j] == draft[at]) ++same;
            }
            if (total >= lim.restateMinLines && static_cast<double>(same) >= lim.restateShare * static_cast<double>(total) && draft.size() > static_cast<std::size_t>(total) + 5)
                return {"restate", "Your reply was stopped: it was writing " + path + " out again unchanged (" + std::to_string(same) + " of its first " + std::to_string(total) +
                                       " lines are the draft's). Do not rewrite the file. Reply with only the lines that change:\n=== EDIT: " + path + " LINES a-b ===\n<the new text for those lines>\n=== END EDIT ==="};
        }
    }

    // 5. Reasoning written as comments inside a block: comment lines and nothing else, one after another (`//`, or `#` that is not a preprocessor line).
    if (lim.maxCommentRun > 0) {
        int run = 0;
        for (const auto& l : lines) {
            const std::string t = trimmed(l);
            const bool comment = startsWith(t, "//") ||
                                 (startsWith(t, "#") && !startsWith(t, "#include") && !startsWith(t, "#define") && !startsWith(t, "#pragma") && !startsWith(t, "#if") &&
                                  !startsWith(t, "#endif") && !startsWith(t, "#else") && !startsWith(t, "#elif") && !startsWith(t, "#undef") && !startsWith(t, "#!"));
            run = comment ? run + 1 : (t.empty() ? run : 0);
            if (run >= lim.maxCommentRun)
                return {"comment-run", "Your reply was stopped: it was reasoning in comments (" + std::to_string(run) + " comment lines in a row). Decide first, then "
                                       "write only the code, with at most a short comment where it helps. Reply again with the edit."};
        }
    }

    // 4. The numbered view echoed back: lines that carry the view's number column are a copy of what was shown, not an edit.
    if (lim.restateMinLines > 0) {
        int echoed = 0;
        for (const auto& l : lines) {
            if (startsWith(l, "=== ")) { echoed = 0; continue; }   // counted per block: one block of that many numbered lines is a copy
            if (numberColumn(l) != std::string::npos) ++echoed;
            if (echoed >= lim.restateMinLines)
                return {"view-echo", "Your reply was stopped: it was copying the numbered view back (the `N| ` numbers are not part of the file). Reply with only the lines that change, without the number column:\n"
                                         "=== EDIT: <path> LINES a-b ===\n<the new text for those lines>\n=== END EDIT ==="};
        }
    }
    return {};
}

Runaway detectEditOverrun(std::string_view reply, const std::map<std::string, std::string>& locked, int maxEditLines, double searchShare, int minFileLines)
{
    enum class In { none, wait, search, body } in = In::none;
    std::string path;
    int count = 0;
    for (const auto& l : completeLines(reply)) {
        if (startsWith(l, "=== EDIT:")) {
            std::istringstream h(l.substr(9));
            std::string kind;
            h >> path >> kind;
            if (path.size() >= 3 && path.compare(path.size() - 3, 3, "===") == 0) path.resize(path.size() - 3);
            in = locked.count(path) == 0 ? In::none : kind == "SYMBOL" || kind == "LINES" || kind == "AFTER" ? In::body : In::wait;
            count = 0;
            continue;
        }
        if (startsWith(l, "=== ")) { in = In::none; continue; }
        if (in == In::none) continue;
        if (startsWith(l, "<<<<<<< SEARCH")) { in = In::search; count = 0; continue; }
        if (in == In::search && startsWith(l, "=======")) { in = In::body; count = 0; continue; }
        if (startsWith(l, ">>>>>>> REPLACE")) { in = In::wait; continue; }
        if (in == In::wait) continue;
        ++count;
        if (in == In::search) {
            const auto fileLines = static_cast<int>(std::count(locked.at(path).begin(), locked.at(path).end(), '\n'));
            if (fileLines >= minFileLines && count >= 10 && static_cast<double>(count) >= searchShare * static_cast<double>(fileLines))
                return {"rewrite-search", "Your reply was stopped: its SEARCH copies most of " + path + ", so it would replace the file whole, which exists and is not "
                                          "a stub. Improve it with edits instead: SYMBOL for a whole function (replaced, or added when it is new), a few functions a "
                                          "reply; a new design goes in function by function. The harness allows a restructure once edits stop making progress."};
        } else if (count > maxEditLines) {
            return {"big-edit", "Your reply was stopped: one edit of " + path + " grew past " + std::to_string(maxEditLines) + " lines, a rewrite in one step. Keep "
                                "each edit to one function: a long function is split into short helpers, each its own SYMBOL edit (a helper before the function that "
                                "calls it), a few per reply; the next turn shows the file with them in place."};
        }
    }
    return {};
}

std::string trimOverlap(std::string_view partial, std::string_view continuation, std::size_t minChars)
{
    const std::size_t most = std::min(partial.size(), continuation.size());
    for (std::size_t n = most; n >= minChars && n > 0; --n) {
        if (partial.compare(partial.size() - n, n, continuation.substr(0, n)) != 0) continue;
        // Indentation alone is not evidence of a repeat: the next line may simply start with the same spaces.
        if (continuation.substr(0, n).find_first_not_of(" \t\r\n") == std::string_view::npos) break;
        return std::string(continuation.substr(n));
    }
    return std::string(continuation);
}

std::string continuationText(std::string_view partial, const std::string& reply)
{
    std::vector<std::string> lines;
    {
        std::istringstream in(reply);
        for (std::string l; std::getline(in, l);) lines.push_back(l);
    }
    // A header repeated at the very start (the model began the block again) and a fence around the text are not content.
    std::size_t first = 0;
    while (first < lines.size() && (startsWith(lines[first], "```") || startsWith(lines[first], "=== FILE:") || startsWith(lines[first], "=== EDIT:"))) ++first;
    std::size_t last = lines.size();
    while (last > first && (trimmed(lines[last - 1]).empty() || startsWith(lines[last - 1], "```"))) --last;
    std::string body;
    for (std::size_t i = first; i < last; ++i) body += lines[i] + "\n";
    if (first == 0 && last == lines.size() && !reply.empty() && reply.back() != '\n' && !body.empty()) body.pop_back();   // nothing stripped: keep the reply's own ending
    return trimOverlap(partial, body, 8);
}

namespace {

std::string rtrimmed(const std::string& l) { return l.substr(0, l.find_last_not_of(" \t\r") + 1); }
std::string ltrimmed(const std::string& l) { const auto a = l.find_first_not_of(" \t"); return a == std::string::npos ? std::string{} : l.substr(a); }
bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

/// The space a token boundary lost (seen from the 35B: `return` + `std::min(...)`, `a.size()` + `== 0)`): the continuation of a cut line began without the
/// space the next token carried. Restored only where a space is certain: after a keyword before a word, and between a word and a comparison or assignment.
bool lostSpace(const std::string& fragment, const std::string& piece)
{
    if (fragment.empty() || piece.empty() || std::isspace(static_cast<unsigned char>(fragment.back())) || std::isspace(static_cast<unsigned char>(piece.front()))) return false;
    const char a = fragment.back();
    if (isWordChar(a) && isWordChar(piece.front())) {
        std::size_t s = fragment.size();
        while (s > 0 && isWordChar(fragment[s - 1])) --s;
        static const char* const kKeywords[] = {"return", "if", "else", "while", "for", "throw", "case", "new", "delete", "const", "auto", "not", "and", "or",
                                                "in", "is", "def", "elif", "class", "struct", "using", "typename", "co_return", "yield", "lambda"};
        const std::string word = fragment.substr(s);
        return (s == 0 || !isWordChar(fragment[s - 1])) && std::any_of(std::begin(kKeywords), std::end(kKeywords), [&](const char* k) { return word == k; });
    }
    if (!isWordChar(a) && a != ')' && a != ']' && a != '"' && a != '\'') return false;
    static const char* const kOperators[] = {"==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "= ", "? "};
    return std::any_of(std::begin(kOperators), std::end(kOperators), [&](const char* op) { return piece.rfind(op, 0) == 0; });
}

}  // namespace

std::string joinContinuation(std::string_view partialView, const std::string& reply)
{
    const std::string partial(partialView);
    std::vector<std::string> c;
    {
        std::istringstream in(reply);
        for (std::string l; std::getline(in, l);) { if (!l.empty() && l.back() == '\r') l.pop_back(); c.push_back(std::move(l)); }
    }
    const bool replyEndsLine = !reply.empty() && reply.back() == '\n';
    // The block being written: its header, and the first line of its body.
    std::string openHeader, openFirst;
    for (std::size_t at = partial.size(); at != std::string::npos && at > 0;) {
        const auto h = partial.rfind("=== ", at - 1);
        if (h == std::string::npos) break;
        if ((h == 0 || partial[h - 1] == '\n') && (partial.compare(h, 9, "=== FILE:") == 0 || partial.compare(h, 9, "=== EDIT:") == 0)) {
            const auto eol = partial.find('\n', h);
            openHeader = rtrimmed(partial.substr(h, eol == std::string::npos ? std::string::npos : eol - h));
            if (eol != std::string::npos) {
                std::size_t b = eol + 1;
                while (b < partial.size() && partial.compare(b, 3, "```") == 0) b = partial.find('\n', b) == std::string::npos ? partial.size() : partial.find('\n', b) + 1;
                const auto e = partial.find('\n', b);
                if (e != std::string::npos) openFirst = rtrimmed(partial.substr(b, e - b));
            }
            break;
        }
        at = h;
    }

    // 1. What is not content at the start: fences, a bare `===` (seen from the 35B), a repeated header. A reply that is a block of its own is not a continuation:
    //    read requests (the worker gave up on the block), or the same block begun again from its first line (it restarted the file).
    std::size_t first = 0;
    for (; first < c.size(); ++first) {
        const std::string t = rtrimmed(c[first]);
        if (startsWith(t, "```") || t == "===" || (first == 0 && t.empty())) continue;
        if (startsWith(t, "=== READ:")) return reply;
        if (startsWith(t, "=== FILE:") || startsWith(t, "=== EDIT:")) {
            std::size_t next = first + 1;
            while (next < c.size() && startsWith(c[next], "```")) ++next;
            if (t == openHeader && !openFirst.empty() && next < c.size() && rtrimmed(c[next]) == openFirst && ltrimmed(openFirst).size() >= 4) return reply;
            // A different block that is not a SEARCH of the tail (T0 write-engine, 2026-11-18: a FILE cut at `if (val < 0`, continued by a SYMBOL edit of
            // evaluate, which was glued onto the cut line): the open block ends at its last complete line, and the new block follows it, whole.
            if (t != openHeader && !openHeader.empty() && !(next < c.size() && startsWith(c[next], "<<<<<<< SEARCH"))) {
                const auto nl = partial.rfind('\n');
                std::string closed = nl == std::string::npos ? std::string{} : partial.substr(0, nl + 1);
                closed += startsWith(openHeader, "=== FILE:") ? "=== END FILE ===\n" : "=== END EDIT ===\n";
                std::string rest;
                for (std::size_t i = first; i < c.size(); ++i) rest += c[i] + (i + 1 < c.size() || replyEndsLine ? "\n" : "");
                return closed + rest;
            }
            continue;
        }
        break;
    }
    std::size_t last = c.size();
    bool strippedEnd = false;
    while (last > first && (trimmed(c[last - 1]).empty() || startsWith(c[last - 1], "```"))) { --last; strippedEnd = true; }
    std::vector<std::string> body(c.begin() + static_cast<std::ptrdiff_t>(first), c.begin() + static_cast<std::ptrdiff_t>(last));
    const bool endsLine = replyEndsLine || strippedEnd;   // a continuation cut off again keeps its unfinished last line for the next join
    const auto joinLines = [&](const std::vector<std::string>& v, std::size_t from) {
        std::string s;
        for (std::size_t i = from; i < v.size(); ++i) s += v[i] + "\n";
        if (!endsLine && !s.empty()) s.pop_back();
        return s;
    };

    const auto nl = partial.rfind('\n');
    const std::string base = nl == std::string::npos ? std::string{} : partial.substr(0, nl + 1);
    const std::string fragment = nl == std::string::npos ? partial : partial.substr(nl + 1);
    std::vector<std::string> baseLines = completeLines(base);

    // 2. The rest written as a SEARCH/REPLACE of the block's own tail (seen from the 35B: SEARCH the last lines, REPLACE them completed): spliced in place.
    if (!body.empty() && startsWith(body[0], "<<<<<<< SEARCH")) {
        std::size_t i = 1;
        std::vector<std::string> search, replace;
        for (; i < body.size() && !startsWith(body[i], "======="); ++i) search.push_back(ltrimmed(rtrimmed(body[i])));   // indentation may be lost (35B: the cut line)
        for (++i; i < body.size() && !startsWith(body[i], ">>>>>>> REPLACE") && !startsWith(body[i], "=== END EDIT"); ++i) replace.push_back(body[i]);
        std::vector<std::string> tail = baseLines;
        if (!fragment.empty()) tail.push_back(fragment);
        bool matches = !search.empty() && search.size() <= tail.size();
        for (std::size_t k = 0; matches && k < search.size(); ++k) matches = ltrimmed(rtrimmed(tail[tail.size() - search.size() + k])) == search[k];
        if (matches) {
            std::size_t after = i < body.size() ? i + 1 : i;
            if (after < body.size() && startsWith(body[after], "=== END EDIT") && startsWith(openHeader, "=== FILE:")) ++after;   // closes the edit, not the file
            tail.resize(tail.size() - search.size());
            std::string out;
            for (const auto& l : tail) out += l + "\n";
            for (const auto& l : replace) out += l + "\n";
            return out + joinLines(body, after);
        }
    }

    // 3. Lines of the block written again at the start (it began a few lines before the cut): dropped.
    std::size_t skip = 0;
    for (std::size_t k = std::min({body.size(), baseLines.size(), std::size_t{20}}); k > 0 && skip == 0; --k) {
        bool same = true, substantial = false;
        for (std::size_t j = 0; j < k && same; ++j) {
            same = rtrimmed(body[j]) == rtrimmed(baseLines[baseLines.size() - k + j]);
            substantial = substantial || ltrimmed(rtrimmed(body[j])).size() >= 4;
        }
        if (same && substantial) skip = k;
    }

    // 4. The cut line: written again in full (as asked), or continued from the cut.
    if (fragment.empty()) return base + joinLines(body, skip);
    if (skip < body.size()) {
        const std::string& line = body[skip];
        const std::string fragTrim = ltrimmed(fragment), fragIndent = fragment.substr(0, fragment.size() - fragTrim.size());
        if (!rtrimmed(fragTrim).empty() && line.rfind(fragIndent, 0) == 0 && ltrimmed(line).rfind(rtrimmed(fragTrim), 0) == 0 && line.size() > fragment.size() &&
            line.find_first_not_of(" \t") == fragIndent.size())
            return base + joinLines(body, skip);
    }
    std::string piece = trimOverlap(partial, joinLines(body, skip), 8);
    if (lostSpace(fragment, piece)) piece.insert(0, " ");
    return partial + piece;
}

std::string continuationPrompt(std::string_view partialView, std::size_t tailChars)
{
    const std::string partial(partialView);
    const auto nl = partial.rfind('\n');
    const std::string base = nl == std::string::npos ? std::string{} : partial.substr(0, nl + 1);
    const std::string fragment = nl == std::string::npos ? partial : partial.substr(nl + 1);
    std::string tail = base.size() > tailChars ? base.substr(base.size() - tailChars) : base;
    if (base.size() > tailChars) if (const auto cut = tail.find('\n'); cut != std::string::npos) tail.erase(0, cut + 1);   // whole lines only
    std::string p = "Your reply was cut off at the token limit. It ended with these complete lines:\n```\n" + tail + "```\n";
    if (!trimmed(fragment).empty())
        p += "and then the unfinished line `" + trimmed(fragment) + "`.\nContinue from the start of that unfinished line: write it again in full, then the rest";
    else
        p += "Continue with the line that follows them, then the rest";
    return p + " of the block, and finish it. Only the remaining lines: no header, no fence, nothing already written, no explanation.";
}

bool cutOffInsideEdit(std::string_view reply)
{
    const auto open = reply.rfind("=== EDIT:");
    if (open == std::string_view::npos) return false;
    return reply.find("=== END EDIT", open) == std::string_view::npos;
}

}  // namespace sub0squad
