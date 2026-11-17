# The layered document: one canvas, many views, bound by stable block identities

Status: design, plus slice 1 BUILT and tested without a model (2026-11-17: `include/sub0squad/doc_layers.hpp`, `src/doc_layers.cpp`, `test_doc_layers`, a block in
`test_canvas_agent`). Slice 1 is the block index and the **issues** layer. The review, prose and code-as-layer views are design only. The measured effect on
tier 0 is in "Slice 1 on tier 0" below. The visualiser is its own project, **sub0canvas**, which is also to hold the canvas tooling itself ("Direction" below).

## Why

Tier 0 (docs/BLOCK_PROVING_GROUNDS.md, "Model tiers and the tier 0 gate") measured a worker that fixes **one defect per turn** even when the verdict lists
several: fix-two-bugs took two turns in two runs of three, with both failures shown in its first message. That cost is linear in the number of defects, and
it got worse with scale:

* the verdict is clipped (1,200 to 1,500 characters), and the harness's own excerpt keeps at most **six** failures, so with ten defects the worker is never
  told about four of them;
* a failing test names no line, so the folded view has no reason to keep the guilty function open, and the worker must first find where the defect is;
* everything about the document is a line number, and line numbers move with every edit.

Another session is changing the prompt to ask for every fix in one reply. This design is the structural answer: the document is held as **layers** that are
indexed together. The worker sees a short summary of a layer first and asks for detail by number. Every layer item is bound to a **stable identity** of the
code it is about, not to a line.

## The model

| concept | meaning |
|---|---|
| **document** | the canvas: files at revisions (docs/CANVAS.md). Unchanged by this design. |
| **block** | a semantic part of a file: a function or method, as `foldRegions(text, python, all=true)` finds them (namespaces and classes stay open so their members are blocks). Text outside every block is "loose" text. |
| **block id** | `b7`: assigned when the block first appears, carried through edits by reconciliation, never reused. Internal: the worker never sees ids, it names blocks by function name (`SYMBOL term`), and lines are resolved at render time. |
| **layer** | a named set of items about the document: `code` (the canvas itself), `issues` (compiler errors and failing tests), `review` (notes such as CONCERN blocks), `prose` (a description per function), and any other. Layers are open-ended: a layer is a name and a list of items. |
| **layer item** | one issue, note or description: `{layer, number, kind, title, detail, anchors[]}`. `number` is its place in the layer now (what `READ ISSUE 3` names). |
| **anchor** | where an item belongs: a block id (resolved to lines when rendered), else a path and line valid for the revision it was found in. Each anchor carries a confidence: `exact` (the producer named a line in it), `suspect` (inferred), `none`. |
| **1-N** | an item has one OR MANY anchors. The same diagnostic at twenty call sites is ONE issue with twenty anchors: it is reported once, read once, and the fix (a rename) applies at every anchor. |

## Block identity

### How an id survives edits

After any change to a file, the index reconciles the file's new blocks with its old ones (`BlockIndex::update`). The same reconciliation handles a SYMBOL
replace, a SEARCH/REPLACE snippet, line operations and a whole-file rewrite, because it looks only at the before and after texts and never at the
operation. A new block takes, in order:

1. the id of an unmatched old block of the same file with the **same signature and name**;
2. the id of the old block with the **same name**, when that name is unique on both sides (a changed signature: `resigned`);
3. the id of the old block with the **most similar body** (token Jaccard >= 0.5; a rename);
4. a **retired** id from any file with the same signature or a near-identical body (Jaccard >= 0.8): a block deleted and written again, or moved to another
   file (`revived`);
5. otherwise a new id (`added`).

An old block left unmatched is **retired**. If most of its body now lives in one live block (token containment >= 0.6), it is recorded as **merged** into
that block, and its items follow the survivor. Otherwise its items become orphans.

| edit | rule that carries the id | what happens to items |
|---|---|---|
| SYMBOL replace, body edit, a move within the file | 1 (signature) | unchanged; lines re-resolved |
| parameters changed | 2 (unique name) | unchanged |
| rename, same body | 3 (similarity) | unchanged |
| split (`f` becomes `f` + `helper`) | `f` keeps its id by 1 or 2; `helper` is added | stay on `f` |
| merge (`g` folded into `f`) | `g` retired, merged into `f` | `g`'s items resolve to `f` |
| delete | retired, no successor | orphaned (see below) |
| delete, then written again (FILE first, SYMBOL each function later) | 4 (revived) | come back |
| move to another file | 4, once both files are updated | come back |
| whole-file rewrite | 1 to 4 per block: signatures first, then names, then bodies | as above, block by block |

Only a kept block whose text changed is logged (`kept`, detail `edited`), so the history of an id is what happened to it, not every re-index.

### Recovery after a whole-file rewrite

A whole-file rewrite is not a special case. Signatures carry most blocks; names carry re-signed ones; body similarity carries renames. The tests cover a
rewrite that moves `factorial` to the top, changes `call`'s parameters and renames `term` to `multiplicative` in one step: every id survives.

The order is deliberate. A signature is exact and cheap. A unique name is next best, because a model rarely renames and re-signs at once. Similarity is last,
because two short functions can look alike. The thresholds are named constants in `src/doc_layers.cpp`, and bodies of fewer than three tokens are never
compared.

### Orphans

An item whose block was retired without a successor is an **orphan**. It is never silently re-pointed. Its summary says "a function that no longer exists",
and its detail says the issue may be stale. For the issues layer this rarely lasts: issues are re-derived from each verdict, so an orphan lives one turn at
most. For a persistent layer (review, prose), an orphan is shown to whoever owns the layer: a review note on a deleted function is resolved or re-anchored by
a person or a workflow, never by a guess.

### Line ranges are the fallback

Loose text (includes, globals, a stray statement after a cut-off file) has no block. An item there is anchored by **path and line** in the revision it was
found in (`At src/calc.cpp:2 (outside any function)`). That is exact for issues, which are re-derived each turn. For a persistent layer, a line anchor should
also keep the **text of its line** (a quote selector, see "Prior art"), so it can be re-found after edits by search rather than by number. That is not built.

### Scope of an index

Today an index lives for one attempt, and each turn's state is written to `layers/turn-N.json`. Carrying ids across attempts (and across sessions) needs the
index persisted next to the canvas and reloaded with the draft it describes. The JSON has everything needed for that; the loader is not built.

## How a worker addresses an edit (and alternatives)

Today a worker sees `N| text` and edits by line, by SYMBOL or by SEARCH text. The canvas maps its line numbers through every edit since the revision it was
shown (a stale transaction is rebased or refused). The worker therefore sees line numbers while the architecture owns the mapping. That is a strong start, and
the block index generalises it: a layer item never stores a line. Alternatives, weighed:

| scheme | the model writes | pros | cons |
|---|---|---|---|
| **view-local line numbers, mapped** (today) | `LINES 41-43` against the revision it saw | what models know best; rebasing makes stale numbers safe | every edit shifts later lines; two edits in one reply must use the same view's numbers |
| **block-relative lines** | `term:3` (line 3 of `term`) | an edit in one function never moves another's addresses; matches folding | needs the block index (now built); still shifts within a block |
| **hierarchical / fractional line ids** | `41.1`, `41.2` for lines inserted after 41 (Dewey order, or CRDT positions such as Logoot/LSEQ) | an insertion never renumbers anything; ids are stable forever | unfamiliar to models; ids get long under churn; must be shown in the view |
| **content anchors** (SEARCH/REPLACE) | the text itself | no numbers at all; robust to shifts | the model must copy text exactly; ambiguous when text repeats; costly for large spans |
| **per-line hash ids** | `#a3f:` prefixes shown per line | stable while a line is unchanged; checkable | noisy view; a changed line gets a new id; tokens per line |
| **relative offsets ("RoPE-like")** | `+2 after <anchor>`, positions relative to a named block or anchor line | small numbers; shift-free outside the anchor's block | still needs an anchor scheme underneath; offsets inside a changing block move |
| **symbol-only** (SYMBOL edits) | `SYMBOL term` + the whole function | no numbers; one edit per function; ids map directly to blocks | rewrites a whole function for a one-character fix (tokens) |

Recommended next step: **block-relative lines** for READ and EDIT (`=== EDIT: path SYMBOL term LINES 3-4 ===`). The worker already sees functions as units
(folding, SYMBOL), the index makes the block a stable frame, and lines inside one function are few and move only when that function is edited. Hierarchical ids
and hash ids are the option for multi-worker canvases, where two workers editing one file must never invalidate each other's addresses.

## The summary and the query

### What the worker sees

A failing gate's verdict becomes the gate's count line plus the issue summary, one line per issue:

```
GATE FAIL: 3 of 16 tests failed
3 issues (`=== READ: ISSUE n ===` shows one in full with its code):
 1. test modulo: CHECK_NEAR failed: ev("10%3") is 3.33333 but 1 is 1 - likely in term() (lines 40-52)
 2. test factorial: CHECK_NEAR failed: ev("5!") is 24 but 120 is 120 - likely in factorial() (lines 57-62)
 3. error src/calc.cpp:125: 'terms' was not declared in this scope - in 3 places: expr(), call()
```

A compile or parse failure gets the same summary, then the compiler's own words (its source lines and carets are worth their characters). Every failure is
listed: the gate now writes `failures.txt` with every failing test unclipped, beside the clipped `gate.txt`, and the summary is built from it. The anchored
blocks are added to the view's focus, so a folded view keeps them open.

### The query

`=== READ: ISSUE 3 ===` (also `=== READ: ISSUES 1, 4 ===`) is answered like any read: no turn used, within the read allowance. The answer is the issue's full
text, then each anchor resolved **now**: the function's current numbered lines (up to three blocks, the rest named), the lines around a loose anchor, or the
fact that its block is gone:

```
=== ISSUE 1 (test) ===
FAIL 6 - modulo: CHECK_NEAR failed: ev("10%3") is 3.33333 but 1 is 1
Likely in src/calc.cpp, term(), now at lines 40-52:
  40|     double term()
  ...
```

The keyword is the layer's name made singular: `ISSUE` now, `NOTE` or `PROSE` later. One parser and one renderer serve every layer.

### Localisation (where a failing test points)

A failing test names no line. Its **suspect** block scores:

* 3 points when a word of the test's name is the block's name (`factorial` -> `factorial()`);
* 1/k points for each token of the expression under test held in the string or character literals of k blocks (`10%3` -> `'%'` lives only in `term()`).
  Rarer tokens weigh more.

The expression comes from the quoted text of the failure message (the C++ check helper prints `ev("10%3")`), and from the test's own function in the given
test sources, found by name. Python's unittest prints only `test_modulo: 30.0 != 1`, and the line it names is a shared assertion helper. Ties give up to three
suspects (an item with several anchors). The confidence is always `suspect`: it is a lead, not a verdict. A defect in a helper the suspect calls
(`fromRad()` behind `call()`) is pointed at the caller. Following calls is a later refinement.

### Prompt cost

Per issue, a summary line is at most 170 characters, typically 110 to 150: about the size of the raw failure line it replaces, plus the function and lines.
For tier 0's ten defects that is about 1.4k characters (about 400 tokens), against a clipped verdict of 1.2 to 1.5k that held only six. Detail costs nothing
until asked, then about the size of one function. The summary header is about 60 characters. The block index costs no prompt tokens: ids are never shown.

## Layers: how they are added, and who produces each

A layer is a producer that turns its source into `std::vector<LayerItem>`, anchored through the shared `BlockIndex`. Everything else is shared:
`layerSummary`, `layerDetail`, `layerFocus`, the READ keyword, and the interchange JSON. Adding a layer takes three steps:

1. write the producer (text in, items out, anchors from `index.at(path, line)`, a name match or `suspects`);
2. give the loop a place to call it, and add its items to the `layers` map written per turn;
3. let `parseIssueReads` generalise to the layer's keyword (one line), and decide where its summary sits in the turn message.

| layer | producer | lives in | anchors | lifetime |
|---|---|---|---|---|
| code | the canvas | `src/canvas.cpp` | the blocks themselves | the document |
| **issues** (built) | the gate's verdict (`failures.txt`, `gate.txt`) and the cheap check's output (precheck) | `parseIssues`, called from `src/canvas_agent.cpp` | exact (diagnostics), suspect (tests) | re-derived every verdict |
| review | the worker's CONCERN blocks (`takeConcerns`, `src/stance.cpp`), later a reviewer persona | a producer beside `recordConcerns` | by function names mentioned in the concern, else the file | persistent until resolved |
| prose | the design stage: the architect's or planner's description per function (the skeleton's `@@ signature` lines, a contract paragraph) | `src/architect.cpp` / `src/plan.cpp` | by function name, so prose exists before the code does: an anchor to a name not yet written resolves once the block appears | persistent; revised by design |
| (any) | a workflow over other layers (see below) | a workflow stage | derived | as its sources |

Prose sits **above** the code: a worker can be shown the prose summary of a file (one line per function) instead of its folded code, and drill into a
function's code from its prose (and from code to its prose). That view is what sub0canvas demonstrates first.

## Relations between layers, and collapsing duplicates

Items relate 1-N within a layer (one issue, many places) and across layers (a prose description anchors one block, a review note may anchor three). Two
workflows follow:

* **collapse**: issues with the same message (line numbers aside) are already one item with many anchors. A workflow can go further and merge items that are
  the same defect seen differently (a compile error and the test it breaks) into one report, keeping each source as an anchor;
* **batch fix**: an item with N anchors and an obvious mechanical fix (a rename, a regex replacement) can be applied at every anchor in one transaction,
  which the worker proposes once and the canvas applies N times. That is repeat-rename's tier 0 case. Not built.

## History and traceability

Each part is recorded:

* the canvas log: who changed which lines of which file at each revision (`canvas.json`, docs/CANVAS.md);
* the index's events: what happened to each id, stamped with the turn (`kept`/edited, `resigned`, `renamed`, `revived`, `added`, `retired`, `merged`);
  `blockHistory(index, id)` gives one section's identity history, including the ids merged into it;
* the layers per turn: `layers/turn-N.json` holds the files, blocks, events and every layer at that turn.

So "what happened to `term()`, and what is attached to it?" is answered by its id: its events, the canvas revisions whose spans overlap its lines, and every
item in every layer whose anchors resolve to it.

## Formats

There are two audiences, and they get two formats:

* **the model** never reads JSON. It sees line-oriented text: the numbered view, the one-line summaries, `=== READ: ISSUE n ===`. That text is the
  domain-specific format, small and hard to get wrong, in the same family as the existing `=== EDIT ===` blocks. It is what token efficiency is measured on.
* **tools** (the viewer, a later loader, a CI report) read the interchange form, schema `layered-doc-1`, which is written by a serializer and never by a
  model. JSON is used because the program already has a writer and parser (simdjson), and a browser reads it natively:

```
{"schema":"layered-doc-1","turn":N,
 "files":{"<path>":{"revision":n,"text":"..."}},
 "blocks":[{"id","path","name","signature","first","last"}],
 "retired":[{"id","path","name","mergedInto"}],
 "events":[{"turn","carry","id","path","name","detail"}],
 "layers":{"issues":[{"id","number","kind","title","detail","anchors":[{"blockId","path","line","confidence"}]}]}}
```

`events` is the index's whole log up to this turn (cumulative; each event carries its turn, so a reader takes `turn == N` for one turn's changes).
`retired` is the set retired as of this turn. An item's `id` is stable across turns while its `number` is not: `issues/test/<name>` for a failing test,
`issues/<kind>/<hash of the message without numbers>` for a diagnostic. An anchor's `line` is in the revision given in `files` for that turn.

Considered: **YAML** (more readable, fewer tokens, but its indentation and implicit typing make it error-prone, and that matters only if a model writes it);
**BAML** (a typed schema-and-prompt language: worth it if a model is ever asked to *produce* layer items, such as a reviewer writing notes, because the parser
is lenient and typed); a **compact text DSL** or a token-oriented notation for tabular data (fewest tokens, the right choice for any layer data shown to a
model in bulk); **SARIF** for the issues layer (the standard static-analysis interchange: results with several locations, rules, and fingerprints for
matching results across revisions). Decision for now: JSON for tools, the line DSL for the model, and a SARIF export of the issues layer as a cheap later
step for CI viewers.

## Prior art

None of these was found by a fresh search for this document. They are known systems that solve parts of the same problem, listed so the design borrows
rather than reinvents:

* **SARIF**: results with multiple locations, related locations, and `partialFingerprints`, which identify the same finding across revisions (the issues
  layer's 1-N anchors and cross-revision identity).
* **Language Server Protocol**: diagnostics with ranges, related information and document symbols (the issue record and the block outline). Positions
  are line/character, so clients re-map them on every edit, which is the problem block ids avoid.
* **LSIF / SCIP / Kythe**: code-intelligence indexes with stable symbol monikers across files and versions (block ids, at a much larger scale).
* **Unison**: definitions are identified by a hash of their content, and names are metadata (the content-hash option for ids; a rename never changes
  identity).
* **GumTree and AST differencing**: matching of tree nodes across versions by top-down hash and bottom-up similarity, detecting moves, renames and
  updates (the reconciliation rules here are a lightweight, line-level version).
* **git blame -M/-C, diff move detection**: tracking lines and blocks moved within and across files.
* **IntelliJ smart pointers / Roslyn tracking spans**: references to code elements that survive edits, re-resolved on access (anchors resolved at render
  time).
* **W3C Web Annotation** (and Hypothesis): anchors with a text-quote selector (exact text plus prefix and suffix) and a position selector, re-anchored
  fuzzily after edits (the line-anchor fallback for persistent layers).
* **Gerrit / GitHub review comment porting**: comments carried to later patch sets, marked "outdated" when their lines change (orphans, honestly shown).
* **Literate programming (WEB, noweb, org-babel) and notebooks**: prose and code in one document, prose first (the prose layer and the prose-centred view).
* **CRDT sequence identifiers (Logoot, LSEQ, RGA)**: position ids that never shift on insertion (the hierarchical-id option for multi-worker editing).

## Visualisation: sub0canvas

The viewer is its own project, **sub0canvas** ([github.com/CraigHutchinson/sub0canvas](https://github.com/CraigHutchinson/sub0canvas), public), built by its own session. It reads `layers/turn-N.json` and the canvas log. It centres
the view on any layer (code, prose, issues, review or others; nothing about the set is hard-coded), drills down from prose to code and back, shows a block's
identity history and the revisions that touched it, and steps through the anchors of a 1-N item. The owner decided (2026-11-17) that the C++ index and layers move into
sub0canvas as a library (`sub0canvas::layers`, pure, with block discovery passed in), with Sub0Squad consuming it pinned to a tag, and one golden set of turn
files tested on both sides. That move was made on 2026-11-17: outline, edits, canvas and doc_layers, with test_edits and test_doc_layers, are the `sub0canvas::sub0canvas` library (sub0canvas v0.2.0, imported from Sub0Squad d54758f), consumed here through CPM; `include/sub0squad/{outline,edits,canvas,doc_layers}.hpp` forward to it. Changes to that code are made in sub0canvas first, then the pin is bumped here. The viewer's further schema requests (anchor columns, a proposed `fix` per item,
`mergedFrom` for unified reports, a `revision` on loose anchors, a `layerMeta` map) are listed in its repository and are not built here yet.

## Direction: sub0canvas as the squad's backbone (owner, 2026-11-17)

The full design (mechanisms, the store, adapter interfaces, the revision-aware index, the format extension, how the squad's pieces map, consumers, slices
with checks, open questions) is in sub0canvas: [docs/BACKBONE.md](https://github.com/CraigHutchinson/sub0canvas/blob/main/docs/BACKBONE.md). This
section and the next are the owner's direction it was written from.

**Scope.** The canvas tooling (the canvas core, folding, the block index and layers, their build and their tests) becomes the **sub0canvas** project: a
tool for agentic workflows that works on its own, outside Sub0Squad. Sub0Squad is its first and most integrated consumer. Sub0Squad keeps what is
squad-specific: the attempt loop, the gates and harnesses, the prompts. The code in this repository is the starting point, moved, not rewritten. Other
consumers keep the design honest: a text editor over layers; a collaborative multi-user document editor with review, comments and explainers; a prose-first
reader of a codebase.

**The idea.** The canvas is not only the document a worker edits. It is where the squad's agents meet. Each role reads and writes layers and reacts to
what others write:

* a **planner** writes the plan or prose layer (a description per function, a backlog item per piece of work) and **tags** items, for example
  `ready-for-implementation`;
* a **scheduler** watches for that tag, as an inbox: a tagged item, with the context its anchors give it, becomes a job delegated to a worker, and the item
  records who holds it (the claims of docs/CANVAS.md);
* a backlog written inside a project (docs/BACKLOG.md, issues in a file) is just another layer. A job that reads it can flag items for delegation the same
  way, so **orchestration is a layer** too: what is planned, held, done and verified, anchored to the parts of the document it concerns;
* an **owner** of an area (a person or an agent) watches that area: a failing test anchored to "their" code is an event they want to hear about, the way a
  worker sees its issues;
* **monitors** are freeform: by area (blocks, paths), by kind (failing tests, concerns), by layer, by tag, by event (a block merged away, an item left
  orphaned).

**The mechanisms, kept few and general** (how they are used is left open; use cases are built on them, not into them):

| mechanism | what it is | built on |
|---|---|---|
| layers and items | a named list of items with kind, title, detail, anchors, a stable `key` | slice 1 |
| anchors, 1-N | to block ids, loose lines, other items (item-to-item links), and names not yet written | slice 1, plus item links |
| tags and state | free-form tags and a small lifecycle per item (open, held, done, verified, orphaned) | new |
| events | an append-only log of every change: item written, tag added, block retired, claim taken | the index's events plus the canvas log |
| subscriptions | a standing query over layers, anchors, tags and events, delivered as an inbox (a summary first, detail on request, as `READ ISSUE n`) | the summary and READ machinery |
| claims | a time-limited hold on an item or a block, with an expiry (docs/CANVAS.md) | design |
| views | any layer at the centre, with drill-down to what it anchors (sub0canvas's viewer) | sub0canvas |
| sources | adapters that feed documents, items and events (the squad's canvas and gate, local git, GitHub), anchored in their own terms and resolved by the core ("Sources" below) | new |

**Path.** Integrate today: move the slice-1 library and the canvas core into sub0canvas, consume it from Sub0Squad, and keep the canvas loop working
unchanged. Then iterate towards the backbone: tags and state, subscriptions as inboxes, item links, claims. Planning and designing the backbone is a major
item of work of its own, for a later session. It must start from what the squad already does (the planner's plan, the workflow's items and decisions, the
journal, concerns, mends), and show which of them become layers and which stay where they are.

## Sources: an adapter abstraction, and the GitHub project view (owner, 2026-11-17)

**The use case.** Point the canvas at a GitHub repository and see the project around the code, not only the code:

* the files at a commit (the code layer), and the **change history** of each block across commits: who changed `term()`, in which commit and PR, and
  what it was called before;
* the **open pull requests** as a pending layer: each PR's diff is anchored to the blocks it touches, so a block shows "2 open PRs change this", and two
  PRs that touch one block are a conflict seen before either merges;
* **comments inside PRs** (review comments on lines, review summaries) anchored to the code they discuss. From the code you see the discussion, and from a
  comment you jump to that code as it is now, after later commits. A comment whose block was rewritten or deleted is shown as outdated or orphaned, never
  silently moved;
* **issues and discussions** that reference code (permalinks such as `blob/<sha>/src/calc.cpp#L41-L50`, or a function named in the text) become items
  anchored to those blocks: "this function has 3 open issues and was last discussed in #12";
* **project board items** (GitHub Projects) with their status and fields, which map directly onto the backbone's tags and state;
* **CI check runs and code-scanning results** (SARIF), which go into the issues layer beside the local gate's verdict and collapse with it when they are the
  same failure.

This is the backbone's use case seen from outside the squad: the same layers, anchors, tags, events and subscriptions, fed by a source the squad does not
own. Doing it well needs the design to stop assuming that the document is the Sub0Squad canvas.

**The abstraction.** Everything that feeds the canvas is an **adapter**. An adapter declares which of these capabilities it has. The core implements none
of them for a particular source.

| capability | what the adapter provides | examples |
|---|---|---|
| **document** | the revisions (an id, parents, author, time, message), the files at a revision, and optionally the diff between two revisions | the Sub0Squad canvas (a revision per transaction), a local git repository, GitHub's API at a ref, a plain directory, an editor's buffers |
| **items** | items for a layer, each with a namespaced key, kind, title, detail, state and tags, and **source anchors** (below), fetched since a cursor | the gate verdict, CONCERN blocks, prose from the design stage, PR review comments, issues, project items, check runs |
| **events** | a change feed since a cursor: polling with ETags, or webhooks/push, delivered into the event log that subscriptions read | GitHub's events and webhooks, the canvas log, a file watcher |
| **write** (optional) | an action back at the source: post or resolve a comment, set a project status, open a PR, apply a canvas transaction | GitHub's API, the canvas |

Five rules keep adapters small and the core trustworthy:

1. **Adapters never compute block ids.** An adapter anchors in its source's own terms: a **source anchor** is `{revision, path, line range, quote}`, where
   the quote is the anchored text (as in W3C Web Annotation's text-quote selector). The core resolves it to a block id by mapping that revision forward to
   the revision being viewed. One resolver serves every source, and a GitHub comment, a gate failure and a design note are anchored by the same rules.
2. **The index becomes revision-aware.** Today `BlockIndex` follows one line of revisions, a turn at a time. To resolve an anchor made at an older commit,
   or on a PR branch, the core replays the index along the revision graph: from the anchor's revision to the viewed one, or from the merge base for a branch.
   It keeps checkpoints so a replay is bounded. Each replay emits the usual events (`kept`, `renamed`, `merged`, `retired`...), so **change history at block
   level** is the index run over git history: what git blame -M/-C does for lines, done for functions. This is the main strengthening of the design: block
   identity across the history of a project, not only across one worker's turns.
3. **Keys are namespaced by source.** Examples are `github/pr/123/comment/456`, `github/issue/12`, `squad/issues/test/modulo`. Duplicates across sources
   (a CI failure and the local gate's failure of the same test) are merged by the collapse workflow into one item that keeps both sources as provenance.
   They are never merged silently.
4. **Cursors and snapshots make views reproducible.** An adapter returns an opaque cursor with each fetch. The core caches what it fetched, keyed by source
   and revision or cursor, so a view can be replayed offline and two people looking at "PR 123 at its head" see the same thing. Large histories are read
   incrementally: since the merge base, or the last N commits, with index checkpoints.
5. **Credentials and writes stay at the edge.** The adapter owns authentication (for GitHub, the user's `gh` login or a token from the environment). The
   core, the interchange files and the viewer never hold credentials. Interchange files of a private repository are private data. Every write is
   outward-facing and goes through the consumer's confirmation: an agent that wants to post a comment proposes it, and a person or a policy approves it.

**What changes in the design.**

* The Sub0Squad canvas becomes one document adapter among several, and the gate verdict, concerns and prose become item adapters. This is the boundary that
  lets sub0canvas stand alone: the library has no dependency on the squad, and the squad plugs in as adapters.
* `layered-doc-1` gains:
  * a `sources` map (name, adapter kind, capabilities, cursor);
  * a `source` on every item and event;
  * `revision` and an optional `quote` on anchors;
  * `state` and `tags` on items;
  * a `revisions` list with parents, for history views.

  Existing readers keep working: every addition is optional.
* The viewer gains a project view:
  * history along a block's identity;
  * PRs as pending overlays, with conflicts between PRs;
  * comment threads in the margin of the code they discuss;
  * an outdated or orphaned marker when the code has moved on.

**How GitHub maps onto the model.**

| GitHub | canvas |
|---|---|
| commit, branch, merge base | revision, and a line of revisions; the index replays along it |
| file at a commit | document file at a revision |
| PR | an item in a `pulls` layer, anchored to every block its diff touches (1-N), state open/draft/merged/closed, plus a pending revision line (its branch) |
| PR review comment, suggestion | an item in a `review` layer: a source anchor at the PR's commit, a thread as detail, resolved/outdated as state; a suggestion is a proposed `fix` |
| issue, discussion | an item in an `issues/github` layer, anchored by permalinks and named functions, otherwise unanchored (an orphan, shown as such) |
| project item and its fields | tags and state on the item it tracks |
| check run, code-scanning alert (SARIF) | an item in the issues layer; SARIF's own fingerprints become the key |
| webhook or event | an event in the log; subscriptions and inboxes read it |

**Slices**, each with a check:

| slice | what is built | check |
|---|---|---|
| G1 | a local git document adapter, and the revision-aware index replayed over history | the identity history of a known function across a real repository's last 200 commits, renames and moves included, against hand-checked truth |
| G2 | GitHub items, read-only: open PRs as a `pulls` layer, and PR review comments as source anchors resolved to blocks | each comment on this repository's recent PRs lands on the right block at main's head, or is marked outdated, against hand-checked truth |
| G3 | issues, discussions and project items, as items with tags and state | the backbone's tag and state mechanism is driven by a real project board |
| G4 | events and subscriptions over GitHub | an owner's subscription ("failing checks or new review comments on blocks under `src/canvas*`") delivers an inbox summary |
| G5 | write back through the consumer's confirmation | an agent proposes a review reply anchored to a block; it is posted only on approval |

**Prior art for this part.**
* **GitHub and Gerrit** already port review comments to new revisions and mark them outdated, but per line and per PR. The block index carries them across
  PRs and history.
* **GitLens** (blame, line history and PRs in the editor), **CodeStream** (discussions anchored to code across branches) and **Sourcegraph** (code
  intelligence across history; batch changes as 1-N edits) are the closest products.
* Plugin architectures such as **Backstage** and **Grafana data sources** are the model for adapters: capability-declared, credentials at the edge, a core
  that knows none of them.

## Out of scope (for now)

* review and prose producers (design above; slice 2 and 3);
* persisting the index across attempts and sessions;
* line anchors with text-quote selectors for persistent layers;
* block-relative or hierarchical edit addressing;
* following calls in localisation (a defect in a callee of the suspect);
* the batch-fix transaction for 1-N items, and workflows that merge items;
* nested blocks (a lambda inside a function, a class as a block of its own): `foldRegions` returns outermost function-like regions only;
* languages beyond brace-delimited and Python (the folding heuristics set the limit).
* source adapters and the revision-aware index (designed in "Sources" above; slices G1 to G5).

## Slice 1 as built

* `BlockIndex` (ids, reconciliation, history, JSON) and the issues layer (`parseIssues`, `layerSummary`, `layerDetail`, `layerFocus`, `parseIssueReads`,
  `layeredJson`), in `src/doc_layers.cpp`, pure data.
* The gate writes `failures.txt`: every failing test, where `gate.txt` keeps six (`Tap::all`, `src/harness_common.cpp`). The C++ and Python runners
  carry it through.
* The canvas loop (`src/canvas_agent.cpp`) shows the summary for every failing verdict (the starting files, a staged revision that does not compile, a
  failing commit, a reverted commit), answers `=== READ: ISSUE n ===` as a read, writes `layers/turn-N.json`, and records `issueLayer` and `issueReads` in
  `agent.json`. `SUB0SQUAD_ISSUE_LAYER=0` turns it off, so one build can be measured both ways. A job's environment is rebuilt by the shim, so the
  shim passes this switch through (`src/shim.cpp`). Without that, an A/B silently measures the default twice, which is what the first attempt at this
  measurement did. The verdict about an earlier attempt's draft (attempt 2 and
  later) is still the gate's text.
* The summary header names the query only. It does not tell the worker to fix everything in one reply: that instruction is the prompt session's, so the two
  effects can be measured apart.
* Tests: `test_doc_layers` (the identity rules, orphans, revival, a cross-file move, Python blocks, the issues layer including 1-N and loose anchors,
  rendering after a rewrite, the JSON) and the `test_canvas_agent` block "The issues layer" (summary, READ ISSUE, the per-turn JSON, and the switch off).

## Slice 1 on tier 0

Measured 2026-11-17 on build `0.0.0-151-g44db273` (slice 1 plus the shim switch on base 973b9e6; the branch was later rebased, and the later commits change
only the item key, JSON and docs). Engine `qwen35` (llama server on port 8092, ctx 16384, 4 slots), 4 jobs at once. One build, two arms, chosen per run by
`SUB0SQUAD_ISSUE_LAYER` (0 = the gate's clipped text as before, 1 = the issues summary). Every run's own `agent.json` records which arm it ran in, and every
run matches its arm. Three runs per case per arm, 24 runs in total, and all 24 passed. Defects per turn is the gate's measure: defects × passed runs ÷ turns
of the passed runs. Tokens are means per run. Seconds are not compared, because the runs shared the server with other sessions.

| case | defects | off: turns per run | on: turns per run | off: defects/turn | on: defects/turn | off: first attempt | on: first attempt | prompt tokens off → on | out tokens off → on | issue reads (on) |
|---|---|---|---|---|---|---|---|---|---|---|
| bugs-3 | 3 | 1 1 3 | 2 1 4 | 1.80 | **1.29** (worse) | 3/3 | 3/3 | 8,342 → 11,749 | 446 → 1,349 | 0 |
| bugs-6 | 6 | 4 5 3 | 4 3 2 | 1.50 | **2.00** | 3/3 | 2/3 | 20,074 → 15,128 | 1,073 → 1,518 | 0 |
| bugs-10 | 10 | 4 3 6 | 1 4 3 | 2.31 | **3.75** | 1/3 | 3/3 | 21,699 → 14,121 | 2,304 → 2,524 | 1 |
| fix-many | 5 | 1 1 1 | 1 3 1 | 5.00 | **3.00** (worse) | 3/3 | 3/3 | 5,106 → 8,550 | 590 → 621 | 0 |
| all | 72 | 33 turns | 29 turns | 2.18 | 2.48 | 10/12 | 11/12 | 13,805 → 12,387 (−10%) | 1,103 → 1,503 (+36%) | 1 |

What this shows, and what it does not:

* **Where the layer was meant to help, it helped.** On bugs-6 and bugs-10 the gate's excerpt keeps six failures and clips the rest. There, defects per turn
  rose by 33% and 62%, and prompt tokens fell by 25% and 35% (the summary replaces the clipped text and its repeats). bugs-10 also passed on the first
  attempt in all three runs, against one in three without the layer.
* **Where the old text already listed everything, it did not help.** bugs-3 and fix-many got worse: one extra turn in one run of fix-many, and in two runs of
  bugs-3. Their prompts grew, because the summary's localisation (the function's line range) is added to a verdict that was already short. Completion tokens
  rose in every case.
* **A quality regression, traced.** In bugs-10 with the layer on, run 2 changed 105 lines where 10 were needed, 45 of them collateral (quality_t0.py).
  Without the layer, the worst bugs-10 run changed 18 lines, 1 of them collateral. Traced in the run's replies, the 105 lines break down as:
  * **73 lines of re-indentation.** The worker's SEARCH text came back indented by 5 or 6 spaces where the file has 4. The edit applier matched it
    anyway, because its matching ignores whitespace, and then wrote the REPLACE text verbatim, so five whole functions moved right. This is an
    edit-applier finding, not a layer finding: a match that ignored indentation should carry the file's own indentation onto the REPLACE. It can happen
    in either arm.
  * **17 lines of an invented function.** `check_end_of_subexpression()` was added and never called. It came from the summary: issue 10
    (`ev("(1+2")` did not throw) was localised to "likely in 2 places: expr(), unary()", but the defect is the unchecked `)` in `primary()`. The
    worker followed the pointer, then spent a READ ISSUE and a fourth turn before fixing `primary()`. The localiser scores expression tokens against
    block literals, and punctuation such as `+` and `(` points at the wrong blocks. Rule for the next slice: a suspect needs a test-name match or a
    non-punctuation token, otherwise the issue says "location unknown". A wrong pointer costs more than no pointer.
  * **32 lines remain once indentation is ignored**, against 10 to 18 in the other five runs.

  So the summary does not invite a whole-file rewrite. It invited one wrong edit, and the applier's indentation turned three small edits into whole-function
  diffs.
* **The worker hardly asks for detail.** It read one issue in 12 runs (`=== READ: ISSUE n ===`). The summary alone carried the effect. The query costs
  nothing when it is not used, but its value is not shown yet.
* **The noise is as large as the effect.** Three runs per arm, and per-run turns vary from 1 to 6 within one arm (bugs-10 off: 4 3 6). The overall gain,
  2.18 to 2.48 defects per turn (+14%), is not significant at n = 3. An earlier build of the same slice (before the prompt session's changes, layer on only)
  measured bugs-3 1.29, bugs-6 2.0 and bugs-10 3.33 (two runs), which agrees with this arm.
* **A first attempt was void.** The switch was read in the agent's own process, but the shim rebuilds a job's environment, so every "off" run in that attempt
  ran with the layer on. The shim now passes `SUB0SQUAD_ISSUE_LAYER` through (commit "Shim: pass SUB0SQUAD_ISSUE_LAYER through"), and those runs are not
  counted here.

**After both fixes (re-run, build `0.0.0-162-g2f52498`).** The applier fix (origin/main ea1feb3: a REPLACE takes the matched lines' indentation) and the
localiser fix (an operator character points at a block only when one block holds it) were applied, and bugs-10 was run again with the layer on, three runs:

| bugs-10, layer on | turns per run | defects/turn | first attempt | changed/ideal lines per run (collateral) |
|---|---|---|---|---|
| before the fixes | 1 4 3 | 3.75 | 3/3 | 10/10; 105/10 (45); 14/10 |
| after the fixes | 3 5 3 | 2.73 | 2/3 | 10/10; 21/10 (6); 10/10 |

The collateral is gone: the worst run went from 105 changed lines to 21. Defects per turn moved from 3.75 to 2.73, which is still above the off arm's 2.31
and still within the noise at n = 3; the one run that needed a second attempt accounts for most of the drop. Read it as: the quality regression was the
applier and the localiser, and both are fixed.

Next, from this:
* **Switch the layer on adaptively.** Use the summary only when the gate's text would be clipped, or when more than about 3 failures are listed;
  otherwise send the gate's own text. bugs-3 and fix-many would then behave exactly as in the off arm. This is one more A/B arm.
* **Put the detail inline.** Show the first N issues (about 3) in full, with their code, under the summary instead of waiting for READ ISSUE, which was
  used once in 12 runs.
* Done since: the localiser no longer points on a shared operator, and the applier re-indents a REPLACE (origin/main ea1feb3); see the re-run above.
* **Measure with five runs per arm.** Read nothing into a gap smaller than about 30%. At n = 3 and temperature 0.7, this table is suggestive, not
  significant.

## Slice 2 as built

* **Three modes**, chosen per run by `SUB0SQUAD_ISSUE_LAYER` (passed through by the shim): `0` off (the gate's clipped text), `1` always on, and `2`
  adaptive, the default: the layer is used only when the gate's text would be clipped (longer than the verdict's clip, 1.2k or 1.5k characters) or the
  verdict parses into more than 3 issues (`kAdaptiveIssues`); otherwise the gate's text is sent unchanged, with the gate's own focus, exactly as in mode 0.
  The layers are written to `layers/turn-N.json` in modes 1 and 2 either way, and READ ISSUE is answered in both.
* **Inline detail.** Where the layer is used, the first issues are shown in full under the summary, each with its function's current code
  (`layerInline` in sub0canvas v0.2.1: the first `SUB0SQUAD_ISSUE_INLINE` issues, default 3, within 3,000 characters, the first always; a function
  shown for an earlier issue is named, not repeated). The rest are left to `=== READ: ISSUE n ===`. `SUB0SQUAD_ISSUE_INLINE=0` is slice 1's summary
  alone. For compiler diagnostics, the compiler's own words follow unless every issue is already shown in full.
* `agent.json` records `issueMode`, `issueInline`, `layerShown` (failing verdicts shown through the layer) and `layerPassed` (those adaptive mode left
  as the gate's text), beside `issueLayer` and `issueReads`.
* Tests: `test_doc_layers` (`layerInline`: the count, the budget, the empty cases, a function shared by two issues) and the `test_canvas_agent` block
  "The issues layer", now run in all three modes (mode 1 shows the issue in full in the verdict; mode 2 passes a short single failure through unchanged
  and still writes the layers).
* The A/B drivers are prepared in `out/runs/layers-slice2/` (`measure_slice2.sh`, three arms; `compare_slice2.py`, which also reads each arm's
  per-run lines and collateral from its `gate.md`).
