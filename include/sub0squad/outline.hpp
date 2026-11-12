#pragma once
/// @file outline.hpp
/// Folded views of source files for small models (docs/FOLDED_VIEWS.md): like an editor that collapses braced regions, a file is shown as its structure
/// (includes, class and namespace lines, function signatures) with long bodies folded to one line that names the folded line range, and the worker asks
/// for the lines it needs with a READ request. Language-light and deterministic: braces for C++, JavaScript and the like, indentation for Python.

#include <string>
#include <vector>

namespace sub0squad {

/// A foldable region: a function-like block. Line numbers are 1-based and inclusive; `first` is the signature line, `bodyFrom..bodyTo` the body: for braces `bodyTo` is the closing line, which stays visible, so a fold hides bodyFrom..bodyTo-1; for Python it is the last body line, all of it hidden.
struct FoldRegion
{
    int first = 0, bodyFrom = 0, bodyTo = 0;
    std::string name;   ///< the identifier before the first '(' of the signature line ("" when none)
};

/// The foldable regions of `text` (outermost function-like blocks only: namespaces, classes and structs stay open so their members show).
/// `python` selects indentation instead of braces. By default only regions worth folding are returned (a body of two lines or more); with `all` every
/// function-like block is, down to a one-line function, which is what addressing a function BY NAME needs (`replaceSymbol`, a section of a fork).
[[nodiscard]] std::vector<FoldRegion> foldRegions(const std::string& text, bool python, bool all = false);

/// The file as a numbered view (`  12| code`). Whole when it fits `budget` characters; otherwise bodies are folded
/// (`      ... (45 lines folded: 13-57; READ them to see)`), except regions that contain any of `focusLines` (lines named by a failure), which stay open
/// while the budget allows.
[[nodiscard]] std::string foldedView(const std::string& text, bool python, const std::vector<int>& focusLines, std::size_t budget);

/// The numbered view with EVERY function body folded except the functions named in `openSymbols` (by full or last name): the view of one section's
/// owner (a forked session sees its own function open and its neighbours as signatures), and with no names the outline of a file.
[[nodiscard]] std::string sectionView(const std::string& text, bool python, const std::vector<std::string>& openSymbols);

/// One request to read lines: `=== READ: path LINES a-b ===` or `=== READ: path SYMBOL name ===`. A request that names only the path (`from`, `to` zero
/// and no symbol) asks for the outline: a whole file is never returned to a read.
struct ReadRequest { std::string path; int from = 0, to = 0; std::string symbol; };
/// All READ requests in a reply, in order.
[[nodiscard]] std::vector<ReadRequest> parseReadRequests(const std::string& reply);
/// The numbered lines a request asks for (at most `maxLines`), or a short message when the path, lines or symbol do not exist.
[[nodiscard]] std::string answerRead(const ReadRequest& r, const std::string& text, bool python, int maxLines = 160);

/// Whether the file's language is indentation-based (by extension).
[[nodiscard]] bool isPythonPath(const std::string& path);

}  // namespace sub0squad
