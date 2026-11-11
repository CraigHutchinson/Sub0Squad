# The canvas: a live shared document the squad edits section by section

Status: design only, 2026-11-11 (owner idea; sequenced after the 35B baseline and the forked-session work). Nothing here is built.

## The idea

The project and its code live in one shared document. Agents edit it **section by section**, each section is visibly *held* by whoever is working in it, and
any worker can see who is where, observe a section, ask the holder for something, or leave a note, instead of working blind from a snapshot and colliding at
merge time. The human sees the same surface: a live picture of what each worker is doing and where.

## Why it follows from what exists

* **Sections already exist**: `foldRegions` (docs/FOLDED_VIEWS.md) gives function-level regions of a file; a section is a region (or a whole small file, or a card
  or contract paragraph). The folded view is the canvas's zoomed-out rendering; a READ request is "expand this section".
* **Ownership already exists, coarsely**: a plan's items own disjoint files, and a divided item is a coordinator over parts. Today a collision is impossible
  because the allowlists never overlap. The canvas is what lets that restriction relax (several workers in one file) without losing safety.
* **Messaging already exists, for people**: the inbox (`request raise|answer`) carries questions, requirements and proposals to the owner. The canvas adds
  worker to worker notes with the same record shape.
* **Evidence already exists**: the journal and per-attempt conversations record who did what; the canvas view is a projection of them plus live claims.

## Model

| concept | meaning |
|---|---|
| **section** | an addressable region: `file#symbol`, `file:a-b` at a revision, or a card section (`item/contract`). Identity survives edits by name, not by line number. |
| **claim** | a time-limited lease a worker takes on a section before editing it (`write`) or to watch it (`observe`). Claims are journaled decisions with an expiry, like every other decision (docs/PROCESS_DESIGN.md: only decisions are journaled; the rest is derived). |
| **revision** | every accepted edit to a section creates a new revision; an edit names the revision it was made against, so a stale edit is detected and rebased or rejected, never silently merged over someone else's change. |
| **note** | a short message attached to a section or addressed to a worker: `heads-up` (do not tread here), `question`, `contract-change` (an interface you rely on is about to change), `done`. Notes are append-only. |
| **presence** | derived: the open claims and the last event per worker. Shown to workers in their view (`parseUnary: held by dev-parser, attempt 2, editing`) and to the human. |

## What a worker sees and can do (the tool surface for a small model)

* The folded view marks sections: `... (41 lines folded: 7-47; parseUnary)  [held: dev-parser]`.
* `=== READ ... ===` as now. `=== CLAIM: path SYMBOL name ===` before editing a section that someone else may touch. `=== NOTE: path SYMBOL name | text ===` to leave
  a message. `=== ASK: dev-parser | text ===` to request contact; the answer arrives as a note on the next turn (workers do not block on each other: a worker
  that is waiting yields its slot, the same rule the V uses for dependencies).
* An edit to a section held by another worker is refused with the holder named and the note channel offered, not silently queued.

## Rules that keep it safe

1. **The gate is still the judge.** Claims and notes coordinate; acceptance remains tests, build and review of the merged result. A canvas never makes a worker's claim of success count.
2. **No worker waits on a claim without a deadline.** Claims expire; an expired claim is released by the daemon and journaled, so a stuck or crashed worker cannot freeze a section.
3. **Small and bounded.** At most a few notes per section per attempt, a character cap per note, and notes are shown only for the sections in the worker's view,
   so the canvas does not become prompt bloat for an 8k-token window.
4. **Decisions only in the journal.** Claims, releases and notes are journaled; the canvas view is derived from them on demand and can be rebuilt after a restart.
5. **Off by default** (the same rule as every new capability), enabled per plan or per process stage.

## Fit with the V model and forks

* A **fork** (`WORK_GRAPH.md` section 23) is the first real user: the primer writes the skeleton, each function is a section, each session claims its own. The
  parent coordinator sees all claims and can run its integration gate as sections close.
* **Up/down transfer**: a `contract-change` note on an interface section is the signal that today needs a `sendDown`; a worker that reads such a note re-checks
  the tests against the new contract before continuing.
* **Review**: a cross-module reviewer (docs/BUILDING_BLOCKS.md, "widen before retrying") is a worker with `observe` claims over every section of a level.

## Transactional edits: what they fix now, before any second worker exists (owner question, 2026-11-11)

The real runs show the limits of "the conversation carries the files and the worker rewrites or search-replaces them": stale line numbers, a window that
overflows when it holds a draft, an edit reply and a view at once, a whole file sent back as one search block, and no way to tell a bad edit from a bad
approach. A revisioned canvas fixes each of them with one idea: **the document is the state, and an edit is a transaction against a revision.**

| limit seen | canvas answer |
|---|---|
| line numbers go stale when several edits apply | a transaction names its base revision; its numbers are rebased through the edits made since, or it is refused with the conflicting edit named (built: `Canvas::apply`) |
| the window holds files, the previous reply and the gate text together (catalogue row 30) | the conversation stops carrying files: the card, the gate text and a folded view of the current revision are rebuilt each turn; the reply is only a transaction |
| the model rewrites the file as one big SEARCH block | `replaceSymbol` replaces one function by name (no line arithmetic, the unit a small model writes well); `replaceLines` and `insertAfter` for the rest; a whole-file reply is a deliberate, separate operation |
| a half-valid edit leaves a broken file | all operations apply or none; a rejected transaction changes nothing and says which operation failed and why |
| a compile slip costs a whole attempt (a stray `();`, an unterminated string) | **commit gating by the cheap check**: apply the transaction to a scratch copy, run the harness's precheck (a one-second `-fsyntax-only` or byte-compile), and commit only when it passes; the worker gets the error and the located lines back in the same turn, several small verified edits per attempt instead of one large unverified one |
| an attempt that goes wrong cannot be undone | every revision is kept; `revertTo` restores one as a new revision; the log says who changed which lines |
| two workers in one file | the same rebase and conflict rule, plus claims on sections (below) |

This is why the canvas is worth building before there is a second worker: **slice 0 is a single-worker canvas per attempt**. It replaces the ad hoc
`baselineFiles` and `currentFiles` maps, the edit applier and the numbered views with one revisioned store, and it is what the later slices (claims, notes,
several workers) sit on. The core is built and unit-tested (`src/canvas.cpp`: apply, rebase, conflict, atomicity, symbol replace, revert); it is not wired
into `llm_agent` yet. Wiring waits for the current fixes (numbered drafts, windowed repair views) to show on a real run whether a whole-file search block is still
the model's habit, because that decides whether `replaceSymbol` or a transaction-only reply format is the right default.

## Slices

0. **Single-worker canvas per attempt** (core built): wire `llm_agent` to a `Canvas`; replies are transactions; the harness precheck gates each commit; folded views are rendered from the current revision.
1. **Claims and presence, read-only for workers**: the daemon derives held sections from a new `claimed` / `released` decision pair; the folded view shows
   `[held: ...]`; the portal shows who is where. No refusal yet. Measures: how often two workers would have touched the same section.
2. **Notes**: `NOTE` and `ASK` requests, delivered on the next turn; a cap per section.
3. **Enforcement**: edits to a held section are refused with the holder named; stale edits are rebased by section revision.
4. **Live human view**: the portal's work graph gets a canvas pane (sections as cells, colour by holder, last note), with the owner able to leave a note a worker will read.

## Open questions

* Section identity across renames and moves (by symbol name works for functions; a rename needs an explicit `moved` note).
* Whether the human's view should be a true shared editor (typing into a section) or a read and annotate surface; the second needs no conflict machinery and is the first target.
* How much a 35B-class worker actually uses `ASK` unprompted; the first measurement is whether notes get read and change what the next edit does.
* Interaction with the budget: each claim and note costs tokens in the view; the cap above is a guess to be tuned from traces.
