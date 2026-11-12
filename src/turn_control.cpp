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

bool cutOffInsideEdit(std::string_view reply)
{
    const auto open = reply.rfind("=== EDIT:");
    if (open == std::string_view::npos) return false;
    return reply.find("=== END EDIT", open) == std::string_view::npos;
}

}  // namespace sub0squad
