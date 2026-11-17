#pragma once
/// @file doc_layers.hpp
/// The layered document (docs/LAYERED_DOCUMENT.md): one document seen at several abstractions, every layer bound to the same STABLE block identities rather
/// than to line numbers. A block is a semantic part of a file (a function or method, as `foldRegions` finds them); its id is assigned when the block first
/// appears and carried through edits by reconciliation (signature, then name, then content similarity), so a layer item made against one revision still
/// names the right code after a SYMBOL replace, a whole-file rewrite or a rename. Lines are resolved only when a layer is rendered. Text outside every
/// block falls back to a path and line, valid for the revision it was found in.
///
/// Slice 1 builds the block index and the ISSUES layer (compiler errors and failing tests, parsed from a gate's verdict). The layer item, its summary and
/// its detail are layer-neutral, so the review and prose layers reuse them. Pure data, no I/O.

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sub0squad {

/// One semantic block of a file, with its stable identity and where it stands now.
struct Block
{
    std::string id;                    ///< "b7": stable for the life of the index; never reused
    std::string path;
    std::string name;                  ///< as `foldRegions` names it (may be qualified: `Parser::term`)
    std::string signature;             ///< the signature line, whitespace collapsed
    int first = 0, last = 0;           ///< the lines it spans now (1-based, inclusive, signature to closing line)
    std::set<std::string> tokens;      ///< identifiers and numbers of its body: what content similarity compares
    std::set<std::string> literals;    ///< the contents of its string and character literals ("%", "asin"): what issue localisation matches
};

/// How a block's identity was carried through the last update of its file (for the log and the tests).
/// kept: same signature (logged only when its text changed); resigned: same name, new signature; renamed: similar body, new name; revived: a retired id
/// back; added: new; retired: gone, its items orphaned; merged: gone, its body now in another block that its items follow.
enum class Carry { kept, resigned, renamed, revived, added, retired, merged };

/// One line of the index's history: what happened to which id.
struct BlockEvent
{
    Carry carry = Carry::kept;
    std::string id, path, name, detail;   ///< `detail`: the old name of a renamed block, the old signature of a re-signed one, the survivor of a
                                          ///< merged one, the file a revived block was retired from, "edited" for a kept block whose text changed
    int turn = 0;                         ///< the turn it happened in (`setTurn`)
};

/// Every block of a document, with identities that survive edits. Feed it each file's new text after any change (`update` is cheap when the text is the
/// same); ask it where an id is now, or which block holds a line.
class BlockIndex
{
public:
    /// Reconcile the blocks of `path` with its new text. In order, a new block keeps the id of an unmatched old block of the same file that has
    ///   1. the same signature (a SYMBOL replace, a body edit, a move within the file),
    ///   2. the same name, when that name is unique on both sides (a signature change),
    ///   3. a body similar enough (token Jaccard >= 0.5; a rename),
    /// or else revives a RETIRED id of any file with the same signature or a near-identical body (a block deleted and written again, a move across files);
    /// or else gets a new id. An old block left unmatched is retired: when most of its body now lives in one block (token containment >= 0.6) it is
    /// recorded as MERGED into that block, whose id its layer items follow; otherwise its items are orphans.
    void update(const std::string& path, const std::string& text);
    /// The turn later events are stamped with.
    void setTurn(int turn) { turn_ = turn; }

    /// The live block of that id, or of the block it was merged into; nullptr when the id is retired without a successor (its items are orphans).
    [[nodiscard]] const Block* resolve(const std::string& id) const;
    /// The innermost live block of `path` that holds `line`, or nullptr (text outside every block).
    [[nodiscard]] const Block* at(const std::string& path, int line) const;
    /// The live blocks of `path`, in file order.
    [[nodiscard]] std::vector<const Block*> blocks(const std::string& path) const;
    /// Every live block of every file.
    [[nodiscard]] std::vector<const Block*> all() const;
    /// What happened to ids, in order.
    [[nodiscard]] const std::vector<BlockEvent>& events() const { return events_; }
    /// The index as JSON (`{"blocks":[{id,path,name,first,last}],"retired":[{id,name,mergedInto}]}`), kept beside the canvas for a later reader.
    [[nodiscard]] std::string json() const;

private:
    struct Retired { Block block; std::string mergedInto; };
    [[nodiscard]] std::string retiredJson() const;
    std::map<std::string, std::string> texts_;            ///< the text each path was last indexed at
    std::map<std::string, std::size_t> hashes_;           ///< by live id: a hash of its text, to tell an edited block from an untouched one
    std::map<std::string, std::vector<Block>> live_;      ///< by path, in file order
    std::map<std::string, Retired> retired_;              ///< by id
    std::vector<BlockEvent> events_;
    int next_ = 1;
    int turn_ = 0;
};

/// Where a layer item belongs: a block id when it lies in a block (resolved to lines at render time), else a path and line of the revision it was found in.
struct Anchor
{
    enum class Confidence { exact, suspect, none };
    std::string blockId;
    std::string path;
    int line = 0;
    Confidence confidence = Confidence::none;   ///< exact: the producer named a line in the block; suspect: inferred (a failing test's subject)
};

/// One item of one layer: an issue, a review note, a prose description. Layer-neutral: every layer renders through `layerSummary` and `layerDetail`.
/// An item relates to ONE OR MANY places (1-N): the same diagnostic at twenty call sites is one item with twenty anchors, so it is reported, read and
/// fixed once (a rename applies at every anchor).
struct LayerItem
{
    std::string layer;            ///< "issues", "review", "prose"
    int number = 0;               ///< 1-based within the layer as it stands now: what `=== READ: ISSUE n ===` names
    std::string kind;             ///< issues: "error", "warning", "test"
    std::string title;            ///< one line
    std::string detail;           ///< the producer's full text (of the first occurrence when there are several)
    std::vector<Anchor> anchors;  ///< where it belongs, the first the primary; empty: nowhere known
};

/// The issues in a verdict: every compiler diagnostic (`path:line:col: error: ...`, Python's `path:line: msg`) anchored exactly to the block holding the
/// line, and every failing test (`FAIL n - name: message`) anchored to its SUSPECT block: the block named like the test, else the block whose string and
/// character literals best match the tokens of the expression under test, rarer tokens weighing more. Diagnostics with the same message (line numbers
/// aside) are ONE issue with an anchor per place. `sources` (the test files, by path) lets a runner that names the test's line (`(eval_test.py:8)`)
/// but not its expression be localised from that line.
[[nodiscard]] std::vector<LayerItem> parseIssues(const std::string& verdict, const BlockIndex& index, const std::map<std::string, std::string>& sources = {});

/// The events of one id and of every id merged into it, in order: the identity history of a section.
[[nodiscard]] std::vector<BlockEvent> blockHistory(const BlockIndex& index, const std::string& id);

/// The interchange form (schema `layered-doc-1`, docs/LAYERED_DOCUMENT.md): the files at their revisions, the index, its history and every layer, one
/// JSON object a viewer reads without the program (sub0canvas). `revisions` gives each path's revision number.
[[nodiscard]] std::string layeredJson(int turn, const std::map<std::string, std::string>& files, const std::map<std::string, int>& revisions, const BlockIndex& index,
                                      const std::map<std::string, std::vector<LayerItem>>& layers);

/// A layer's summary, one line per item, each at most `lineChars`: "3 issues ...:\n 1. test modulo: ... - in term() (lines 40-52)". "" for no items.
[[nodiscard]] std::string layerSummary(const std::vector<LayerItem>& items, const BlockIndex& index, std::size_t lineChars = 170);
/// One item in full: the producer's text, then its anchor resolved now (the block's current numbered lines, or the line it was found at), or that its
/// block is gone (an orphan).
[[nodiscard]] std::string layerDetail(const LayerItem& item, const BlockIndex& index, const std::map<std::string, std::string>& files);
/// `path:line` for every item that resolves to a block, one per line: text a folded view reads as focus, so the anchored blocks stay open.
[[nodiscard]] std::string layerFocus(const std::vector<LayerItem>& items, const BlockIndex& index);

/// The item numbers a reply asks to read: `=== READ: ISSUE 3 ===` (also `ISSUES 3 5`), in order.
[[nodiscard]] std::vector<int> parseIssueReads(const std::string& reply);

}  // namespace sub0squad
