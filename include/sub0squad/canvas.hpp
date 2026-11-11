#pragma once
/// @file canvas.hpp
/// The canvas core (docs/CANVAS.md): files as revisioned documents edited by TRANSACTIONS. A transaction names the revision it was made against and
/// holds line operations; it applies entirely or not at all. A stale one is rebased through the edits made since (line numbers shift) or rejected with the
/// conflicting edit named. Every accepted transaction is logged, so any revision can be restored and "who changed what" is answerable. Pure data, no I/O.

#include <map>
#include <string>
#include <vector>

namespace sub0squad {

/// One operation of a transaction. Line numbers are 1-based and refer to the revision the transaction was made against.
struct CanvasOp
{
    enum class Kind { replaceLines, insertAfter, replaceSymbol };
    Kind kind = Kind::replaceLines;
    int from = 0, to = 0;      ///< replaceLines: inclusive range; insertAfter: `from` is the line to insert after (0 = at the top)
    std::string symbol;        ///< replaceSymbol: a function-like region by name; its whole text (signature to closing line) is replaced
    std::string text;          ///< the new lines (empty deletes for replaceLines)
};

struct Transaction
{
    std::string path;
    int baseRevision = 0;      ///< the revision the worker was shown
    std::string by;            ///< who made it (a worker or item id)
    std::vector<CanvasOp> ops;
};

struct TxResult
{
    bool ok = false;
    int revision = 0;          ///< the new revision when ok, else the current one
    std::string error;         ///< why it was rejected, naming the line, symbol or conflicting edit
};

/// One accepted transaction.
struct CanvasLogEntry
{
    std::string path, by;
    int revision = 0;          ///< the revision this transaction produced
    int baseRevision = 0;
    int from = 0, to = 0;      ///< the span it changed, in the numbering of the revision before it (an insertion has to == from - 1)
    int delta = 0;             ///< lines added (negative: removed)
};

class Canvas
{
public:
    /// Add or replace a file at revision 1 (a replaced file starts a new history).
    void addFile(const std::string& path, std::string text);
    [[nodiscard]] bool has(const std::string& path) const;
    [[nodiscard]] const std::string& text(const std::string& path) const;
    [[nodiscard]] int revision(const std::string& path) const;
    /// The text of an earlier revision (empty if unknown).
    [[nodiscard]] std::string textAt(const std::string& path, int revision) const;
    /// Apply a transaction: all operations or none. An edit made against an older revision is rebased when it does not overlap what changed since.
    [[nodiscard]] TxResult apply(const Transaction& tx);
    /// Restore an earlier revision as a new revision (history is never rewritten).
    [[nodiscard]] TxResult revertTo(const std::string& path, int revision, const std::string& by);
    [[nodiscard]] const std::vector<CanvasLogEntry>& log() const { return log_; }

private:
    struct File { std::vector<std::string> revisions; };   // revisions[i] is the text at revision i + 1
    std::map<std::string, File> files_;
    std::vector<CanvasLogEntry> log_;
    static const std::string empty_;
};

}  // namespace sub0squad
