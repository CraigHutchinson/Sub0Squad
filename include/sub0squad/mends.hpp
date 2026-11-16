#pragma once
/// @file mends.hpp
/// The self-mending ledger (docs/INTERVENTIONS.md, BUILDING_BLOCKS.md (archived: docs/ARCHIVE.md); catalogue rows 59 to 63): a KNOWN, MECHANICAL error is repaired by the harness
/// itself. `registry/mends.json` is a table of mends; each is a SIGNATURE (patterns matched against the FIRST error of a gate's output, capturing the name it
/// is about), what the signature requires before it applies, a named deterministic ACTION, and the fixture that proves it. The gate looks its failure up here
/// and reports the match in its result; the engine then records the decision `mend` (which is journaled) and starts the action as the next attempt, without
/// spending a rung of the ladder. The same signature is mended at most `maxPerItem` times per item: when it comes back after that, the ladder goes on.
///
/// Actions (a closed set; a table names one, it cannot define one):
///   add-section      the missing function becomes a section of the item's file (a generated stub with the contract's or the prototype's signature); every
///                    accepted section and the canvas are kept, ONLY the new section is written (one model session), then the gate runs again
///   silence-unused   the unused variable or parameter is marked `[[maybe_unused]]` where it is declared: a code edit by the harness, no model at all
///
/// What a signature may require: `contract` (the captured name is a function the item's contract obliges it to define), `declared` (the name is a member
/// function a class of the item's own file declares and nothing defines: catalogue row 71), `prototype` (the name is declared in the file), `location` (the
/// error names file, line and column). Mends are tried in table order; the first whose pattern matches AND whose requirement holds is the one reported.

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

struct Mend
{
    std::string name;                  ///< the signature's name: what the decision and the status show (`healed: NAME`)
    std::string errorClass;            ///< the catalogue's class letter (M: mechanical)
    std::vector<std::string> match;    ///< ECMAScript patterns, tried in order against the first error line (quotes normalised to '); group 1 is the name
    std::string requires_;             ///< contract | declared | prototype | location
    std::string action;                ///< add-section | silence-unused
    int maxPerItem = 2;
    std::string fixture;               ///< a gate output under tests/data/mends that this mend must match (a test holds the table to it)
};

/// A mend that matched a gate's output.
struct MendMatch
{
    std::string name, action, requires_;
    std::string symbol;                ///< the captured name (`calc::evaluate`, `hasExp`); for a captured declaration (`double f(int)`) the function's name
    std::string file;                  ///< where the error is, when the error line says (`src/calc.cpp:321:10: error: ...`)
    int line = 0, column = 0;
    std::string error;                 ///< the first error line, as matched
    int maxPerItem = 2;                ///< the mend's own limit, carried so the engine needs no table
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static std::optional<MendMatch> fromJson(std::string_view text);
};

/// The built-in table: the text of `registry/mends.json`, compiled in (a test holds the two equal).
[[nodiscard]] const std::string& builtinMendsJson();
[[nodiscard]] const std::vector<Mend>& builtinMends();
/// Parse a `mends-1` table. Every pattern must compile, every action and requirement must be known: the error names the mend.
[[nodiscard]] std::expected<std::vector<Mend>, std::string> parseMends(std::string_view json);
/// `base` with the mends of `extra` added; one with the same name replaces the built-in one.
[[nodiscard]] std::vector<Mend> mergedMends(std::vector<Mend> base, const std::vector<Mend>& extra);

/// The first error of a gate or compiler output, with typographic quotes and backticks turned into ': the first line that says `error` or
/// `undefined reference` (after a first error a compiler reports follow-on confusion; only the first is evidence). "" when there is none.
[[nodiscard]] std::string firstErrorLine(const std::string& output);
/// The first mend of `table` (in table order) one of whose patterns matches the FIRST error of `output`. Requirements are not checked here.
[[nodiscard]] std::optional<MendMatch> matchMend(const std::vector<Mend>& table, const std::string& output);

/// The `silence-unused` action on a C or C++ source: `[[maybe_unused]]` in front of the declaration that holds `name` at `line`:`column` (1-based; the
/// column of the name, as the compiler reports it). Returns the new text, or nullopt when the name is not there or the file is not C or C++.
[[nodiscard]] std::optional<std::string> silenceUnused(const std::string& text, const std::string& path, int line, int column, const std::string& name);

// ---- Known diagnostics (catalogue row 74) ----
//
// A compiler diagnostic a small model does not map to its remedy: run-v12's `evaluate` was refused three times with the same error and the model sent
// the same code three times. `registry/diagnostics.json` is a table of such diagnostics: a SIGNATURE (patterns matched against the first error), a HINT
// (one plain explanation and the remedy, attached to the retry prompt) and, where the fix is mechanical, a named deterministic EDIT the harness applies
// to the offending function itself. An edit is kept only when the function then passes the check it failed; otherwise the hint is all that is used.
//
// Edits (a closed set):
//   const-iterators   `.begin()` becomes `.cbegin()` and `.end()` becomes `.cend()` in the function's text

struct KnownDiagnostic
{
    std::string name;
    std::vector<std::string> match;    ///< ECMAScript patterns against the first error line (quotes normalised to ')
    std::string hint;                  ///< what the error means and what to write instead, in one or two sentences
    std::string edit;                  ///< "" (a hint only) | const-iterators
    std::string fixture;               ///< a refusal or gate output under tests/data/mends that this entry must match
};

/// The built-in table: the text of `registry/diagnostics.json`, compiled in (a test holds the two equal).
[[nodiscard]] const std::string& builtinDiagnosticsJson();
[[nodiscard]] const std::vector<KnownDiagnostic>& builtinDiagnostics();
/// Parse a `diagnostics-1` table: every pattern must compile, every edit must be known, every entry needs a hint.
[[nodiscard]] std::expected<std::vector<KnownDiagnostic>, std::string> parseDiagnostics(std::string_view json);
/// The first entry of `table` one of whose patterns matches the FIRST error of `output`.
[[nodiscard]] std::optional<KnownDiagnostic> matchDiagnostic(const std::vector<KnownDiagnostic>& table, const std::string& output);
/// Apply the named edit to one function's text. nullopt when the edit is unknown or changes nothing.
[[nodiscard]] std::optional<std::string> applyDiagnosticEdit(const std::string& edit, const std::string& functionText);
/// The first error of `output` WITHOUT its place (`path:line:col:`): what stays the same when the same mistake is made again on another line. The measure
/// of a section turn: a first error that CHANGED is progress, one that repeated is thrash (docs/METRICS.md section 4a).
[[nodiscard]] std::string errorKind(const std::string& output);

// ---- The first cause of a fork that left a section unwritten (catalogue row 75) ----

/// The sections a fork attempt never accepted, from its `fork.json` (`"failure":"sections-missing"`), as the comma-separated list the fork wrote; ""
/// when every section was accepted or the text is not such a record. Their stubs stand in the file, so every error about unused code is a CONSEQUENCE.
[[nodiscard]] std::string unacceptedSections(const std::string& forkJson);
/// Whether a first error is of the `-Wunused-*` class (set but not used, defined but not used, unused variable, parameter or function).
[[nodiscard]] bool isUnusedConsequence(const std::string& firstError);

}  // namespace sub0squad
