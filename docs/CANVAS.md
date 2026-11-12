# The canvas: a live shared document the squad edits section by section

Status: slice 0 (a single-worker canvas per attempt) and forked sections are BUILT and tested without a model (2026-11-12, `src/canvas.cpp`, `src/canvas_agent.cpp`, `src/fork.cpp`; `test_canvas_agent`, `test_fork`, `test_blocks`); neither has run on a real model yet. Claims, notes and presence (slices 1 to 4) are design only. Section "As built" below records what was decided while building and why.

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
several workers) sit on. It is built: see "As built" below.

## Slices

0. **Single-worker canvas per attempt** (BUILT, `--executor canvas`): replies are transactions; the harness precheck gates each commit; folded views are rendered from the current revision.
0b. **Forked sections** (BUILT, `--executor fork` / `llm-fork`): a skeleton is revision 1; each stubbed function is a section owned by one session; the coordinator commits each section's transaction; ownership is enforced (the first, hard-wired form of a claim).
1. **Claims and presence, read-only for workers**: the daemon derives held sections from a new `claimed` / `released` decision pair; the folded view shows
   `[held: ...]`; the portal shows who is where. No refusal yet. Measures: how often two workers would have touched the same section.
2. **Notes**: `NOTE` and `ASK` requests, delivered on the next turn; a cap per section.
3. **Enforcement**: edits to a held section are refused with the holder named; stale edits are rebased by section revision.
4. **Live human view**: the portal's work graph gets a canvas pane (sections as cells, colour by holder, last note), with the owner able to leave a note a worker will read.

## As built (2026-11-12): one design for the canvas, folded views and forked sections

The three documents (this one, `FOLDED_VIEWS.md`, `WORK_GRAPH.md` section 23) describe one mechanism, and it is built as one: **a file is a revisioned document of
sections; a view is a rendering of a revision; a reply is a transaction on it; a fork is several workers each owning one section of the same document.**
Three **executors** run an item (`WorkflowSpec.executor`, `--executor session|canvas|fork`; a workflow option and a scenario option; `replay --fork` runs the fork executor on one node):

| executor | what the worker sees | what it replies | who checks, when |
|---|---|---|---|
| `session` (the baseline) | one conversation: the card, the numbered draft, then the latest reply and its failure | whole files or edits | the gate after each reply |
| `canvas` | every turn rebuilt: the card, the verdict of the last turn, the folded view of the CURRENT revision | one transaction (`SYMBOL`, `LINES`, `AFTER`; a whole `FILE` only for a new file) | the harness precheck before the commit, the gate after it |
| `fork` | the shared prefix (card and the numbered skeleton) plus "you own `f`" | one transaction: its own function | the precheck before each section's commit, the gate over the merged file |

### What a canvas attempt does (`src/canvas_agent.cpp`)

1. The best earlier draft becomes revision 1 (a first attempt has no document: the files are written whole and become revision 1).
2. Each turn the message is built from scratch: card, guidance, the verdict of the last turn (the gate failure, or why a transaction was refused, or the lines a
   read asked for), and the folded view of the current revision sized to what the window has left. **The conversation never carries a file or an earlier reply**,
   so the window cannot fill with stale copies (catalogue rows 6 and 30).
3. The reply is parsed as a transaction set (`parseCanvasReply`) and applied to a **scratch copy**. Anything wrong (a line out of range, two edits that
   overlap, an unknown function, a file the item does not own, a block that names no form) refuses the WHOLE reply with the reason; the document is untouched.
4. **Commit gate:** the harness precheck (`Harness::precheck`: `-fsyntax-only` for C++, `ast.parse` for Python, `node --check` for JavaScript) runs over the
   scratch copy. A failure refuses the transaction and returns the located error in the same turn. The first three refusals of an attempt cost no repair turn.
5. A commit is a new revision (kept on disk: `attempt-N/canvas/<file>.rK`, log in `canvas.json` with who changed which lines), then the item's tests run.
6. **A commit that makes more tests fail than the best revision is reverted** (as a new revision by `harness`) and the worker is told what its change broke.
   "Never discard progress" (`WORK_GRAPH.md` section 22) is now a property of the document, not of the prompt.
7. **Reads are bounded:** two read-only replies are answered free; from the third each costs a turn; the answer to the second says the next reply must be an
   edit. A read that names only a path returns the outline (every body folded), never the file.
8. **Turn control** (rows 34 to 36): the reply is judged while it streams (`detectRunaway`: a SEARCH block over 40 lines, the same line 10 times or a short
   block over and over, a whole-file block that reproduces the draft, the numbered view echoed back) and cancelled by closing the connection, then re-prompted
   with the specific instruction (twice at most per attempt). A reply cut off at the token limit is continued by appending, with the repeated start trimmed
   (`trimOverlap`: the longest suffix that is a prefix, at least 8 characters, never indentation alone).

### What a forked attempt does (`src/fork.cpp`)

1. **Primer:** one session writes the skeleton: every function declared, each body one statement that raises `not implemented: <name>`. It must pass the
   precheck (one retry with the error) and then becomes revision 1. A skeleton that does not parse delegates nothing (exit 2).
2. **Sections:** every stub inside a named function is a section. Each session gets the same prefix (system block, card, the numbered skeleton), so a server
   that keeps a prompt cache prefills it once, and is told which function it owns.
3. **Width (throughput by default, `planParallel`):** as many sections at once as there are idle slots. One at a time needs a stated reason
   (`--sequential-reason`), which is recorded. Below a memory floor forks are shed first (`--mem-floor-mib`), also recorded. A run is never wider than the engine.
4. **Commit:** each section's reply must be ONE `replaceSymbol` on its own function. The coordinator applies it against revision 1; the canvas rebases it
   through the other sections' commits (line numbers shift; a real overlap is a conflict and is counted). An edit to another section is refused with the holder
   named. The precheck gates each commit, so a section that does not compile never reaches the merged file; the section gets the error back (two retries).
5. **Merge and integration:** the item's gate judges the merged file. A failure goes back first to the sections it names (whole-word match) and to any that
   never committed, then to all; each in a fresh small session that shows the current revision with its own function open and the others folded.
6. `fork.json` records the width and its reason, conflicts, lost edits (must be 0: every committed function is in the merged text), refusals, tokens per phase
   (skeleton, sections, integration) and the idle-slot share.

### Decisions taken while building, and why

* **Reply forms were kept, not replaced.** The canvas reads the forms workers already use (`=== EDIT: path LINES a-b ===`, `AFTER n`, SEARCH blocks, `=== FILE ===`) and
  adds one (`=== EDIT: path SYMBOL name ===`). A transaction-only syntax would have been a second language for a small model to learn; the recorded-reply cases
  (`tests/data/replies/`) show every form maps onto a transaction. The session executor now applies edits through the same applier.
* **A transaction's base revision is the revision shown, set by the harness, not written by the model.** A worker cannot be trusted to copy a number; the harness knows
  which revision it rendered.
* **Addressing a function by name needs every function, not only the foldable ones.** `foldRegions(..., all = true)` returns one-line and two-line functions too;
  without it a stub (one line of body) could not be replaced by name. Folding still uses the default (bodies worth hiding).
* **The number column is stripped only when every line of a block has it.** The older rule stripped it line by line, which would corrupt code that legitimately
  starts with `1| x`.
* **Two insertions after the same line keep the order they were given in.** The model-based test found the applier's sort was unstable for that case; it is stable now.
* **A fork's ownership check is the first claim.** Slice 1's `claimed` / `released` decisions are not built; within one fork attempt the coordinator holds the map
  section to owner, which is enough to refuse an edit outside a section and name the holder. Journaled claims are needed only when sections outlive an attempt.
* **Fork sessions are fresh, small conversations, including the integration round.** `WORK_GRAPH.md` 23 continued each fork's own conversation on failure; that
  grows a context per round and shows a stale skeleton. A round now shows the current revision with the owner's function open: the same view rule as the canvas.
* **Challenged and changed: "wire the canvas, keep the session loop as legacy".** Both are executors of one registry of three; the session loop is the baseline that
  block 3 and block 5 are compared against with the same seeds, not a deprecated path. It becomes removable when the canvas reaches the block 3 bar on a real model.
* **Not decided here (owner):** which executor is the default. It is `session` until a real-model comparison exists.

### What is not built or not proven

* Nothing here has run on a real model. The commands are in `docs/BLOCK_PROVING_GROUNDS.md` ("Real-model measurements").
* The C++ skeleton prompt asks for unnamed unused parameters so the stubs compile under `-Werror`; whether a 35B does that reliably is unknown. If it does not,
  the precheck's first retry tells it the exact warning.
* A fork needs functions. A file that is mostly data or one large function gets no parallelism from it (the canvas executor still applies).
* Testers' items do not fork (a test file's sections are test cases, and the tester gate judges the file as a whole); in a workflow with `--executor fork`
  testers run on the canvas.
* The fork executor inside a workflow (`llm-fork --agent-exit 1` as the agent job) is NOT tested: `llm-fork` itself is (`test_fork`), a
  workflow that uses it through the daemon is not.
* Claims across attempts, notes, presence, the portal pane (slices 1 to 4).

## Open questions

* Section identity across renames and moves (by symbol name works for functions; a rename needs an explicit `moved` note).
* Whether the human's view should be a true shared editor (typing into a section) or a read and annotate surface; the second needs no conflict machinery and is the first target.
* How much a 35B-class worker actually uses `ASK` unprompted; the first measurement is whether notes get read and change what the next edit does.
* Interaction with the budget: each claim and note costs tokens in the view; the cap above is a guess to be tuned from traces.
