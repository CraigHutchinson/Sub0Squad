#include "sub0squad/canvas.hpp"

#include "sub0squad/outline.hpp"

#include <algorithm>
#include <sstream>

namespace sub0squad {

const std::string Canvas::empty_;

namespace {

std::vector<std::string> split(const std::string& t)
{
    std::vector<std::string> v;
    std::istringstream in(t);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& l : v) s += l + "\n";
    return s;
}

struct Span { int from = 0, to = 0; std::vector<std::string> repl; bool insert = false; };   // 1-based inclusive; an insertion is to == from - 1

}  // namespace

void Canvas::addFile(const std::string& path, std::string text)
{
    files_[path].revisions = {std::move(text)};
    log_.erase(std::remove_if(log_.begin(), log_.end(), [&](const CanvasLogEntry& e) { return e.path == path; }), log_.end());
}

bool Canvas::has(const std::string& path) const { return files_.count(path) != 0; }
const std::string& Canvas::text(const std::string& path) const { const auto it = files_.find(path); return it == files_.end() ? empty_ : it->second.revisions.back(); }
int Canvas::revision(const std::string& path) const { const auto it = files_.find(path); return it == files_.end() ? 0 : static_cast<int>(it->second.revisions.size()); }
std::string Canvas::textAt(const std::string& path, int rev) const
{
    const auto it = files_.find(path);
    return it == files_.end() || rev < 1 || rev > static_cast<int>(it->second.revisions.size()) ? std::string{} : it->second.revisions[static_cast<std::size_t>(rev) - 1];
}

TxResult Canvas::apply(const Transaction& tx)
{
    TxResult r;
    auto fit = files_.find(tx.path);
    if (fit == files_.end()) { r.error = "no such file '" + tx.path + "' on the canvas"; return r; }
    File& file = fit->second;
    const int current = static_cast<int>(file.revisions.size());
    r.revision = current;
    if (tx.baseRevision < 1 || tx.baseRevision > current) { r.error = "base revision " + std::to_string(tx.baseRevision) + " does not exist (the file is at revision " + std::to_string(current) + ")"; return r; }
    if (tx.ops.empty()) { r.error = "the transaction has no operations"; return r; }

    // 1. Resolve every operation to a span in the numbering of the BASE revision.
    const std::string& baseText = file.revisions[static_cast<std::size_t>(tx.baseRevision) - 1];
    const auto baseLines = split(baseText);
    const int baseCount = static_cast<int>(baseLines.size());
    std::vector<Span> spans;
    for (const auto& op : tx.ops) {
        Span s;
        if (op.kind == CanvasOp::Kind::replaceSymbol) {
            bool found = false;
            for (const auto& reg : foldRegions(baseText, isPythonPath(tx.path))) {
                const std::string leaf = reg.name.substr(reg.name.rfind(':') == std::string::npos ? 0 : reg.name.rfind(':') + 1);
                if (reg.name == op.symbol || leaf == op.symbol) { s.from = reg.first; s.to = reg.bodyTo; found = true; break; }
            }
            if (!found) { r.error = "no function named '" + op.symbol + "' in revision " + std::to_string(tx.baseRevision) + " (use LINES a-b)"; return r; }
        } else if (op.kind == CanvasOp::Kind::insertAfter) {
            if (op.from < 0 || op.from > baseCount) { r.error = "insert after line " + std::to_string(op.from) + ": the file has " + std::to_string(baseCount) + " lines"; return r; }
            s.from = op.from + 1; s.to = op.from; s.insert = true;
        } else {
            if (op.from < 1 || op.to < op.from || op.to > baseCount) { r.error = "lines " + std::to_string(op.from) + "-" + std::to_string(op.to) + ": the file has " + std::to_string(baseCount) + " lines"; return r; }
            s.from = op.from; s.to = op.to;
        }
        s.repl = split(op.text);
        spans.push_back(std::move(s));
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.from < b.from || (a.from == b.from && a.to < b.to); });
    for (std::size_t i = 1; i < spans.size(); ++i)
        if (spans[i].from <= spans[i - 1].to) { r.error = "two operations overlap at line " + std::to_string(spans[i].from); return r; }

    // 2. Rebase through the edits made since the base revision. Each logged edit is in the numbering of the revision before it, which is where our span is
    // by then: an edit wholly before the span shifts it, one wholly after leaves it, anything else touched our lines and is a conflict.
    for (auto& s : spans) {
        int shift = 0;
        for (const auto& e : log_) {
            if (e.path != tx.path || e.revision <= tx.baseRevision) continue;
            const int lo = s.from + shift, hi = s.to + shift;
            if (e.to < lo) shift += e.delta;
            else if (e.from > hi) continue;
            else { r.error = "lines " + std::to_string(s.from) + "-" + std::to_string(s.to) + " were changed by " + (e.by.empty() ? std::string{"another edit"} : e.by) + " in revision " + std::to_string(e.revision) + " (lines " + std::to_string(e.from) + "-" + std::to_string(e.to) + "): read the current text and redo this edit"; return r; }
        }
        s.from += shift; s.to += shift;
    }

    // 3. Apply from the bottom up to the current text; all or nothing.
    auto lines = split(file.revisions.back());
    const int curCount = static_cast<int>(lines.size());
    int firstLine = curCount + 1, lastLine = 0, delta = 0;
    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
        if (it->from < 1 || it->to > curCount || it->to < it->from - 1) { r.error = "internal: the rebased span is outside the file"; return r; }
        lines.erase(lines.begin() + (it->from - 1), lines.begin() + it->to);
        lines.insert(lines.begin() + (it->from - 1), it->repl.begin(), it->repl.end());
        firstLine = std::min(firstLine, it->from);
        lastLine = std::max(lastLine, it->to);
        delta += static_cast<int>(it->repl.size()) - (it->to - it->from + 1);
    }
    file.revisions.push_back(join(lines));
    log_.push_back({tx.path, tx.by, current + 1, tx.baseRevision, firstLine, lastLine, delta});
    r.ok = true;
    r.revision = current + 1;
    return r;
}

TxResult Canvas::revertTo(const std::string& path, int revision, const std::string& by)
{
    TxResult r;
    auto it = files_.find(path);
    if (it == files_.end() || revision < 1 || revision > static_cast<int>(it->second.revisions.size())) { r.error = "no such revision"; return r; }
    const int current = static_cast<int>(it->second.revisions.size());
    const std::string old = it->second.revisions[static_cast<std::size_t>(revision) - 1];
    const int oldCount = static_cast<int>(split(it->second.revisions.back()).size()), newCount = static_cast<int>(split(old).size());
    it->second.revisions.push_back(old);
    log_.push_back({path, by, current + 1, current, 1, std::max(1, oldCount), newCount - oldCount});
    r.ok = true;
    r.revision = current + 1;
    return r;
}

}  // namespace sub0squad
