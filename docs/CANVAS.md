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

1. **Primer:** one session DECLARES the structure (`@@ signature` lines, no bodies) and the harness completes it into the skeleton (see "After the first
   real run" below). It must pass the partial precheck (one retry with the reason) and then becomes revision 1. No usable skeleton delegates nothing (exit 2).
2. **Sections:** every stub inside a named function is a section. Each session gets the same prefix (system block, card, the numbered skeleton), so a server
   that keeps a prompt cache prefills it once, and is told which function it owns.
3. **Width (throughput by default, `planParallel`):** as many sections at once as there are idle slots. One at a time needs a stated reason
   (`--sequential-reason`), which is recorded. Below a memory floor forks are shed first (`--mem-floor-mib`), also recorded. A run is never wider than the engine.
4. **Commit:** each section's reply must be ONE `replaceSymbol` on its own function. The coordinator applies it against revision 1; the canvas rebases it
   through the other sections' commits (line numbers shift; a real overlap is a conflict and is counted). An edit to another section is refused with the holder
   named. The precheck gates each commit, so a section that does not compile never reaches the merged file; the section gets the error back (two retries).
5. **Assembly:** findings are routed to their owners, never sent to every section (see "Assembly convergence" below): a section never accepted is retried
   alone, an uncalled helper is removed by the harness, a compile error goes to the owner of its function, and only a file that compiles strictly is judged by
   the item's tests. Each targeted session is fresh and small: the current revision with its own function open and the others folded.
6. `fork.json` records the failure reason, the attempt it resumed, the budget, the width and its reason, conflicts, lost edits (must be 0), refusals,
   targeted sessions, functions the harness removed, tokens per phase and, per section, its declaration, calls, refusals and last refusal.

### After the first real run (run-v8, 35B, `--executor fork`; catalogue rows 44 to 46)

The first real use failed before any section ran, and the failure changed three things.

**The skeleton is the harness's job (row 46).** Asked for a skeleton with stubbed bodies, the 35B wrote the implementation (13,893 tokens, 27 minutes) and it
did not compile. An instruction is a request; a format is a constraint. So the primer no longer writes bodies at all: it DECLARES the structure
(imports, constants, type declarations, and one `@@ <signature>` line per function, placed where the function belongs) and the harness completes it
(`expandSkeleton`): every `@@` line becomes the function with a placeholder body that raises `not implemented: <name>`; a C++ file gets `<stdexcept>` and a
prototype for each file-scope function so sections can call each other in any order. There is no body for the model to get wrong. Three bounds make a
runaway cheap: the skeleton reply is limited to 1,500 tokens at the server, cancelled while it streams past 120 lines, and the whole attempt has a wall
budget (`--attempt-wall`, 1,500 s by default) after which no session starts. A `@@` line that is not a signature is refused with its line number.

**A file being written in sections is checked as partial (row 46).** A skeleton's stubs leave helpers uncalled and parameters unused, which `-Werror` rejects
in a finished file and must not reject in an unfinished one. The skeleton and every section commit go through the harness's PARTIAL precheck (C++: the same
compile without the unused-function, unused-parameter and unused-variable warnings); the merged file is judged by the full gate, unchanged.

**Division on compiled platforms is by sections, not by modules (row 45): the choice, and why.** A division into parts gives each part a tester, and in C++
that tester must compile against the part's interface. No item of a division owns the header that would declare it, so every part tester failed INTERFACE
MISMATCH however often it retried. Two answers were on the table: a CONTRACT stage (one item writes the headers of every part first) or dividing by
sections of one file under a skeleton. **Sections were chosen**, for three reasons:

1. *The skeleton already is the contract.* Its signatures are declared once, compile-checked before any body is written, and visible to every section. A
   contract stage would be a second mechanism for the same fact.
2. *It needs no architect.* run-v8's architect produced no valid division in four hours (and divided a tester into a project, row 44). Sections are found
   by the harness from the declared skeleton; nothing has to be validated beyond "it compiles".
3. *It is what the evidence asks for.* The item that failed (the C++ engine, one file of about 340 lines) is too long for a reply, not too wide for a
   module. Sections cut length; a division cuts breadth.

What is built: `Harness::divideBySections` (true for `cpp-cmake`); where the ladder would ask for a division on such a platform it records a `sections`
decision and the item's next attempt IS the fork executor (`RulePolicy`, `Action::sections`, `agentJob`), once per item; an item reported too big for one
reply goes there at once. Division into modules stays available where the platform tolerates a late interface (Python, JavaScript).

What this does NOT solve, stated so it is not forgotten: a compiled project that genuinely needs new modules (the knowledge store) needs the CONTRACT
stage. It is the right first stage for the process registry to gain once a stage can create items (`docs/PROCESS_DESIGN.md`, "Stages that create items"):
a `contract` item per division that writes declarations only, accepted by the partial compile check, on which the parts' testers and developers depend.

**Only a developer's item is divided (row 44).** `RulePolicy` offers `decompose` and `sections` for developers only; a tester that fails goes retry, then
the card is rewritten (`rearchitect`), then escalation where allowed, then fail. `Plan::amended` refuses any division of a tester's item and names a
developer part when the fragment holds one. ("Send the contract back" as a rung of its own, a request to the architect of the level above, is not built:
`rearchitect` is the nearest thing that exists.)

All of this is tested without a model (`test_fork`: the C++ calculator blueprint forked into eight sections through the real C++ gate; a model that ignores
the instruction; a bad `@@` line; a spent budget. `test_blocks`: `expandSkeleton` per language. `test_workflow`: the policy and the engine's `sections` rung).
None of it has been seen on the real model yet.

### Assembly convergence (run-v9, 35B, the first run in which the mechanism worked; catalogue rows 47 to 52)

run-v9 built its skeletons, wrote 8 to 16 sections per attempt with no conflict and no lost edit, and **no attempt converged**: five attempts each
ended with a merged file that did not compile, at the 1,500 s wall budget. Reading `fork.json`, `gate.txt` and the per-section conversations of
`dev-engine` (attempts 1, 2, 3, 6, 7) gave six findings. Each is now fixed at its source and fixture-tested without a model (`test_fork`, `test_blocks`).

**1. The sections did not run in parallel at all (row 47).** Every request of a daemon job was pinned to the job's one lease slot (`id_slot` from
`SUB0SQUAD_LEASE_INDEX`), and a fork job sends several requests at once, so they queued on one slot. The evidence is in the conversations: every call
decoded at the single-sequence speed (7.6 to 8.0 tok/s), every prompt reported the prefix "already cached on the slot" (one slot, the same one), and a
35-token reply took 52 s of wall time (11 s of work, the rest waiting for the three sessions ahead of it). `idleShare` was about 1e-7 because it
measured the client's threads, which were all busy waiting. So run-v9 measured the cost of coordination with none of its benefit. **Fix:**
`EngineProfile::slot`; a job that runs several requests uses `kAnySlot` (the server picks an idle slot, preferring the one whose cached prompt is most
similar, which keeps the shared prefix cached per slot). **Still to do:** `idleShare` should come from the engine's `/slots`, not from client spans; and the
daemon's lease accounts one slot for a job that now uses up to the idle ones.

**2. The compile check was weaker than the build (row 48).** `-fsyntax-only` stops before the passes that report a path with no return and a function
nobody calls, so a section passed its commit check and the BUILD was the first to object, for the whole file, with no owner. **Fix:** the C++ check is a
real compile to nowhere (`-c -o /dev/null`), strict for a finished file and without the unused-* warnings for a file still being written.

**3. A finding has an owner, and assembly routes it (rows 49, 50).** The old integration round sent the whole failure to every section ("the failure names
no function, so all go back"), which is why integration cost as many tokens as writing. Now:

| finding in the assembled file | who gets it | cost |
|---|---|---|
| a stub is still there (the section was never accepted) | that section alone, with why it was refused | one session |
| `defined but not used` (a helper nobody calls) | nobody: the harness removes the function and the prototype it wrote for it | no model call |
| any other compile error at `file:line` | the owner of the function that line is in, with only that error and that function open | one session |
| a compile error outside every function (in the declared structure) | no section can fix it: the attempt ends as `compile-outside-sections` and the next one starts a new skeleton | none |
| it compiles strictly and a test fails | the sections the failure names; all of them only when it names none | one session each |

Accepted sections are never re-run. **Two points differ from what was asked, on purpose.** (a) *An uncalled helper is removed, not sent to its
section.* The section that wrote it cannot make another section call it; deleting it is the only thing in its power, and the harness can do that
without a model. If the helper WAS needed, the tests fail and say so. (b) *A missing return stays an error at the section's own commit* rather than
being relaxed during assembly: it is a defect of one function, and its own commit is the cheapest moment to hand it back. What is relaxed during
assembly is only what an incomplete file implies (unused functions, parameters, variables).

**4. A commit is judged on its own function only (row 49).** A section was refused for errors in other functions (run-v9: `evaluate`, six times, for a
call whose other end it did not own). And an error can surface late: a function whose callee is still a stub that always throws looks complete to the
compiler until the callee is written, so the missing return appears during ANOTHER section's commit. A commit is now refused only for findings inside
its own function; findings elsewhere are left for assembly to route. The fixture reproduces the late case exactly.

**5. Why sections were refused, and the fix at the source (row 51).** Sixteen refusals in the five attempts:

| cause | count | fix |
|---|---|---|
| the same error repeated by a section that could not see its own reply ("your previous reply is not repeated") | 6 (one section) | a refusal shows the section its function as it would stand in the file, with the line numbers the errors use |
| the declaration drifted: a `Class::` qualifier dropped, so members were "not declared in this scope"; a qualified name written inside the class; a free function where a method was declared (also the cause of attempt 1's "used but never defined") | 5, and one whole attempt | **the declaration is the harness's**: `applySectionSignature` puts the skeleton's signature and indentation back on every reply, whatever the model wrote; a section supplies a body |
| helpers that do not exist (`parseNumberToken`) | 2 | the system block: call only what the file declares; a helper of your own goes INSIDE your function |
| ordinary slips (a `break` outside a loop, a redeclared variable, `&&` within `||` under `-Werror=parentheses`) | 3 | none needed beyond showing the section its own text |

**6. The budget, and what an attempt leaves behind (row 52).** Every attempt ran to 1,500 s whatever its size, and the next began from nothing. Now the
budget is a base plus an allowance per section (`--attempt-wall` 900 s, `--section-wall` 150 s: 16 sections get 55 minutes, 4 get 25); it is spent
only on sessions that are still needed; running out is the failure reason `budget` in `fork.json`, not a compile error; and **the next attempt resumes
this attempt's canvas** (`resumedFrom`) when it stopped short (`budget`, `sections-missing`, `compile`), with every accepted section kept and no new
skeleton. An attempt that had everything written and still failed its tests is not resumed: that structure had its chance.

Also from the logs: a streamed reply counted 0 tokens because a stream reports usage only when asked (`stream_options.include_usage`, now sent); the
harness's placeholder bodies now use every named parameter, so a stub is clean under the strictest warnings; and the writing phase's prompt prefix is one
snapshot for all its sections (a section is written against the declarations, not against whichever neighbours finished first).

**Not explained:** in the integration rounds of run-v9 only 306 prompt tokens were "already cached" (the system block) although the card follows it
unchanged; the cause was not found. With no slot pin and routed findings it matters less, but it is worth one look at the next run's conversations.

**What to look for in the next real run** (`fork.json` of the failing item): `width` requests really concurrent (per-call decode below the
single-sequence speed, aggregate above it); `failure` and `resumedFrom`; `targeted` far below sections times rounds; `mechanicalMends`; `refused` per
section and whether a section's second reply differs from its first.

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
* Whether a 35B keeps to the `@@` format is unknown. If it does not, the reply is cancelled at 120 lines or refused with the line that is wrong, once; a
  second failure ends the attempt in minutes, not in an hour.
* A signature with a default argument would be declared twice by the prototype the harness adds for a C++ file-scope function (a compile error the precheck
  reports); templates on the line above a `@@` line are not handled.
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
