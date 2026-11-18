# Backlog

Work that is understood but not done, in three horizons. Updated 2026-11-16. Each item names where its design lives. The focus is the proving sandbox: every
change to how the squad works is an experiment on the local engine, with a hypothesis, a fixture and an A/B on the same seeds
([EXPERIMENTS.md](EXPERIMENTS.md)). What is done is in [STATUS.md](STATUS.md); the older backlog is in git ([ARCHIVE.md](ARCHIVE.md)).
**Resuming on a new machine?** [RESUME.md](RESUME.md) (2026-11-16) orders the clean-slate work into a priority list, starting with re-measuring speed and the
planner's success rate on the new host. The queue below and that list are the same work.

## Now: the sandbox request is "scientific" (owner, 2026-11-16: it had slipped)

The sample project is a SCIENTIFIC calculator (web first: "write a scientific calculator as a web page"). The clean-slate C++ runs used `write a calculator
in C++` and so built a four-function calculator: every result so far proves the machinery, not the scientific project. Done: `scripts/campaign.py`,
[CLEAN_SLATE.md](CLEAN_SLATE.md) and [RESUME.md](RESUME.md) say `scientific`; `validation/grade_blackbox.py --scientific` adds twelve cases (sqrt, powers,
sin, cos, tan, inverses, log) and fails the plain calculator 0 of 12. TASKS: run the scientific request through the planner and the squad (nested trig
expectations: the tester's wrong-literal failure at its hardest, which is also what E1 tests); make the planner raise ONE scope clarification for a broad
product word ("basic arithmetic only, or scientific functions, and degrees or radians?") through the inbox that already asks for the platform; decide
whether degrees mode is part of "scientific" or an owner answer. Runs wait for the llama.cpp configuration work in its own session.

## Now: the experiment queue

The queue, with hypotheses, arms, measures and commands, is [EXPERIMENTS.md](EXPERIMENTS.md) section 8. In order:

1. **E1, the specialist stance and the contest channel.** Built 2026-11-16 (`--stance specialist`, the CONCERN block, `replay --accepted`, stance in
   `scenario run` and `scenario compare`). To do: the replay on seed 1000 with the known-wrong tests, then the paired replays and a campaign pair
   ([EXPERIMENTS.md](EXPERIMENTS.md) section 9). Needs the owner's engine.
   **The planner arm is the owner's wording A/B** (2026-11-16): the planner's line "The workers are small local models: each sees ONLY its own work card, so
   every card must be exact and short" (A, today) against B "Each worker operates as a dedicated specialist executing a single, self-contained work card; make
   every card atomic, precise, and fully self-sufficient", C "Workers execute in strict isolation with visibility limited to their assigned card, so scope each
   card to a distinct, self-contained unit of work with exact specifications", D "Treat each worker as a focused specialist working in isolation on a single
   assignment: define every work card with surgical precision and zero reliance on outside context", E "Workers are isolated specialists that see only their
   assigned card; make every card self-contained, atomic, and exact". Prompts as DATA (a registry of named variants a stage reads, so a variant is not a
   rebuild; `--stance` is the first such switch); same seeds and engine per variant, at least 10 seeds each; measure valid-plan rate and turns to valid, reply
   tokens, items per plan and slot fill, cards passing the plan checks first time, and for workers attempts to accepted. Hypotheses, not assumptions: "atomic"
   may raise the item count (good for slots, bad for reply size); dropping "short" may lengthen replies and cause cut-offs; "isolated specialists" may change how
   much the model invents beyond the card. The same applies to the over-allocation paragraph, the examples rule, the edit syntax and the repair messages.
2. **E2, the window plan.** Read the KV cost per token from the engine's start log; decide fewer larger slots for planning, a larger total, or a window per role
   ([CLEAN_SLATE.md](CLEAN_SLATE.md) section 6.1). Every role reads its window from the profile.
3. **E3, `plan-lint` into the planner's repair turn** and the slot goal in its prompt ([CLEAN_SLATE.md](CLEAN_SLATE.md) section 6.2).
4. **E4 and E5, the plan format.** Measure the format-only failure share on the planner records; then a prose plan with per-item format calls in parallel against
   the single reply; then a grammar or schema for the format step only; then a schema-guided tolerant parser in place of one mend per slip
   ([CLEAN_SLATE.md](CLEAN_SLATE.md) section 8). The grammar needs the owner's yes.
5. **E6, slot save and restore** (`--slot-save-path`, `/slots/{id}?action=save|restore`): check it on our build, then time the prefill saved and the disk and
   memory cost per slot.
6. **E7 to E10**: the executor comparison (decides the default executor), the decode curve against concurrency, a spec-check first turn, the clean-slate
   campaign of 20 seeds (the number the project is judged by).

## Next: what the experiments need

**Layered document** ([LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md); slice 2 done 2026-11-18, brief closed: [sessions/layered-document.md](sessions/layered-document.md))
- Measure the layer where it acts: off against adaptive on bugs-10 and other clipped cases, 10 runs per arm; inline detail against the summary alone
  (`SUB0SQUAD_ISSUE_INLINE=0` against 3). A pull request here with no library change.
- The backbone, sources (git, GitHub) and scale work is planned and briefed in Sub0Canvas (`docs/BACKLOG.md`, `docs/sessions/`). Each of its steps that
  changes what the squad sees reaches this repository as a pull request bumping the pin (Sub0Canvas `docs/INTEGRATION.md`); review and measure those
  here.

**Measurement** ([METRICS.md](METRICS.md))
- Accounting that reconciles with the engine (a blocker for every efficiency figure): the ledger records a fraction of a forked run's tokens (1.4k, 38k and 40k
  against 397k, 221k and 273k in the fork attempts' own records, runs v9 to v11). Per-session usage records with a `purpose`, a reconciliation check against the
  engine's counters, `--metrics` on the engine start line.
- Engine ticks as the load-independent clock; a scorecard per run (`sub0squad metrics WORKFLOW`, `metrics.json` in the archive, a portal panel), `metrics backfill`
  for archived runs, `metrics compare` with intervals and a regression gate in `block run`.
- Concern counts (raised, by kind, by role) in the campaign report and in `scenario run` results.

**The contest channel, after E1** ([PROMPTS.md](PROMPTS.md) section 4, [CONTEXT_AND_PLANNING.md](CONTEXT_AND_PLANNING.md) section 5)
- Route a concern to the owner of what it questions (tester, planner, keeper); a rule-based keeper first (accept, modify, reject with a reason); the answer delivered
  back to the worker. Agents raising requests through the inbox instead of guessing.
- The specialist wording for the architect (`decompose`, `rearchitect`) and the fork primer.

**Clean slate** ([CLEAN_SLATE.md](CLEAN_SLATE.md))
- Plan review: a distinct role checking contracts against their examples, argument order on test cards, a division finer than the budget, items with no acceptance;
  findings go to the planner as concerns.
- Expectation QA as an explicit gate step: tests against the contract's worked examples and a second independent reading, `ok` or `suspect: <line, why>`; a suspect
  routes to the owner of the expectation, never to the developer's retry loop.
- Planning in phases: design (rare, wide window) and execution-wave planning (frequent) as a looping stage list; `WorkItem::stage` and `Plan::extended` so a stage
  can create items ([PROCESS_DESIGN.md](PROCESS_DESIGN.md) section 13, limits).
- The campaign driver with no blueprint by default, the grader after every run, report columns for plan validity, repairs, review findings and reference verdicts.

**Portal: show the planning layers** (owner, 2026-11-16)
- A planning node ahead of the items (later the layers of [CLEAN_SLATE.md](CLEAN_SLATE.md) section 8: plan prose, plan form, work prose, work source), with
  state, turns and a transcript: `planner/live.txt`, `planner/prose.md`, `planner/turn-N.txt`, `planner/conversation.md`.
- Per planner turn: the problems raised, the mends applied (`mendPlan`, `repairJson`: today only on stderr, so also into `planner.json`), edit or whole plan, the
  document as it stood (`planner/doc/turn-N/`) and the diff between turns.
- The plan-lint scalars beside the plan; dispute edges (`reviseTests`), the gate exit class and the one-failing-test rung in the decision log view.
- The supplied-plan path (`plan.supplied`) and the planner's retries as separate nodes; model-built versus mechanically mended work; the grader's verdict beside
  a finished run. Concerns (the stance experiment) as a node badge.
- Done 2026-11-16, first slice: a Planning card (`#planning` in `web/portal.html`, `renderPlanning` in `web/portal.js`): request, plan prose, plan form
  (turns, conversation, live watch), plan, items; a row per planner turn; the mechanical mends; the plan-lint text; the re-runs. `planner.json` records
  `kinds`, `mends`, `prose`, `slots` and, for a supplied plan, `supplied`; `?wf=ID` shows an earlier workflow. Covered by `tests/portal_page_test.js`.
- Done 2026-11-16, second slice: planning is a node in the work graph (`planningItem`, `withPlanning` in `web/portal.js`), the root every item without a
  dependency waits on; clicking it opens the planning card. Not yet: the dispute edges and gate classes in the decision view, the model-built versus
  mended marker, the grade beside a finished run, a workflow selector in the page.

**Healing and repair** ([PROCESS_DESIGN.md](PROCESS_DESIGN.md) section 14, [INTERVENTIONS.md](INTERVENTIONS.md))
- Route a failing test whose exception message names one function to that section, with the assertion quoted (row 64 a).
- A mend per mechanical catalogue row (10, 12, 25); `process mends` to list the table; the portal's `healed` badge; a command to install a per-workflow mends table;
  end-to-end tests for Python `add-section` and `helper-never-defined` and for the put-back of removed sections.
- Repair by edits for developer and tester repairs outside the canvas executor; proving-ground cases for resuming an interrupted item from its persisted document.
- Idea, not adopted (2026-11-16): when ONE test fails and a repair left the developer's files unchanged, send that test back to its tester before the `think`
  rung (the wrong-literal case in w-000013 and w-000020 cost an extra attempt). With no oracle a right and a wrong test look alike, and `test_nearpass` pins the
  current order; the cheaper discriminator is expectation QA's independent check of the literal (or a worker's CONCERN under the specialist stance).
- Open catalogue rows: 19 (a lease class so structural jobs are not queued behind workers), 22 (a deterministic clock for the shed watcher), 25/26, 58 (the
  `test_shim` flake, not reproduced).

**Scenarios and blocks** ([SCENARIOS.md](SCENARIOS.md), [BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md))
- The remaining scenarios: interface gap, decompose, reviseTests, read-then-edit, integrate. The sandbox has a separate live length-continuation diagnostic on the unmodified `fix-op` plan ([runner and evidence](T0_ITERATIVE_SANDBOX.md#dedicated-reply-continuation)).
- Blocks 7 to 10: section integration seams, division, golden decision logs, process as data as a block.
- The fork executor inside a workflow through the daemon (`llm-fork --agent-exit 1` as the agent job) has no test.

**Tier 0 layers** ([T0_LAYERS.md](T0_LAYERS.md), design 2026-11-18: the T0 cases as a tree, quick / release / dig-down modes)
- Build it in the order of T0_LAYERS.md section 5: `requires` data and the DAG asserts; the tree in `gate.md`; the three composites staged and calibrated
  (edit-mix, build-mix, files-mix); `dig_t0.py` and `@quick`/`@release`/`T0_OUT`; then the switch of `GATE` and the release verdict.
- A failing composite reported as blocked by its failing requirements, not as a failure of its own (2026-11-18: wrong-approach fails while write-engine
  and replace-function fail on their own; `gate_t0.py` does not read `REQUIRES` yet).
- Half-written follow-up, parked until incomplete source blocks a later workflow. Some appends close the current function and leave required declarations or the public entry point missing. Cascade diagnostics can then send the worker back into the correct prefix. Try a hint that derives a short list of unresolved names and missing public definitions from the compiler diagnostics, header and visible contract. Keep the fixture, output scope and gate fixed, and leave the missing implementation to the worker. [T0 evidence](BLOCK_PROVING_GROUNDS.md).
- The near-pass repair attempt goes past `--max-local` in the t0 process: decide whether tier 0 keeps it.
- Token usage as a gate measure, T0 and up (owner, 2026-11-18): an expected budget per case, and the usage broken down by area, so a workflow that
  passes by going round the harness (rework, rereads, rewrites, restarts) shows up even when the tests pass. What exists: agent.json `tokensIn` and
  `tokensOut` per attempt, gate.md's out tokens per case, and the ledger's estimates (`tokens.work`, accepted against rework tokens, prompt overhead,
  harness bytes; `project status`). To build:
  - **Areas** (per turn, summed per attempt, case and run). Workflow and process: planning, rearchitect and card rewrites, attempts after the first,
    manager strategies. Harness, the prompt: system and card, the canvas view, verdicts and gate output, read answers, carried reads and reasoning,
    the issue layer, prefill and continuation context. Model, the output: the accepted deliverable, thinking (kept, and stopped or carried), replies
    stopped while streaming (wasted), continuations, rework (edits later undone, replaced or reverted), and read-only turns. Most of this is known at the
    point of sending (the message parts are built separately) and of the reply (the stream guard and the verdicts): record it as it happens, in
    agent.json, not estimated after.
  - **Expected usage per case**, worked out, not fitted: the reference solution's size in tokens (what an ideal worker writes), the prompt of an ideal
    path (system, card, the view at the case's file size, one verdict per turn) times the turns that path takes (a one-line fix: 1; skeleton: one per
    method), plus allowances for thinking. In make_t0.py beside each case (`expect`), derived from the case's own files where it can be.
  - **Watermarks**: a warning flag in gate.md when a run passes at more than k times its expected usage (k about 3 to start), or when one area's
    share is out of line (rework or wasted output over half of the output, reads over a set count): the case passed but the path was not the expected
    one. A warning first; a gate failure only once the expectations are calibrated over a few gates. The same flags for T1 and T2 runs (their plan has
    the same expected budget, built from its items).
- A gate-wide progress view in the portal (owner, 2026-11-18). Each tier run has its own squad home with its own daemon and portal (`up` in
  validation/tiers/lib.sh), so http://127.0.0.1:8780/ shows only the run in flight, drops between runs, and nothing shows the whole gate: cases × runs
  with result, attempts, turns and tokens (today `out/runs/t0/summary.md`), the run in flight, and the tree (T0_LAYERS.md) coloured by result. One way:
  a portal started once by `run_t0.sh` / `run_tier.sh` on the tier's output folder (summary.md and gate.json as its data, each run's home linked to
  open its live view), outliving the per-run homes; a run's own portal then binds port 0 so the two never collide. It is one case of a wider portal
  need (owner, 2026-11-18): several projects worked at once or one after another, all in one view. Design them together: one portal over many homes
  or workflows (a list of what ran and what is running, each opening its own live view), with a tier gate as one kind of grouping (cases × runs)
  beside a group of projects; the workflow selector already noted for the portal (today `?wf=` in the URL) is the same seam.
- Context compaction for continuations (owner, 2026-11-18). A cut-off reply is now continued as itself (the canvas sends its text, cut to the last complete
  line, back as the assistant's unfinished message: a prefill, measured on the 35B to continue cleanly at a line or word boundary and to stop at once mid-token).
  It needs the prompt, the partial and the reply's budget to fit the context, and falls back to the tail prompt when they do not (`prefilled` against `continued`
  in agent.json says how often). Compaction keeps it on the prefill path: fold the canvas view of a file the reply is replacing whole (FILE over a stub, or after a
  restructure), drop the issue and stance sections on a continuation turn, and size the reply budget to what is left. Measure first: how often the fallback is taken.
- Loops (owner, 2026-11-18; built in one reply, multi-turn left for real evidence). Code repeats itself by design, so there is no repetition penalty, DRY
  sampler or chunk hash on it: Gemini CLI's loop service turns its content check off inside code blocks for that reason, and keeps tool-call repeats and
  an LLM check for the rest. Within one reply a loop is caught after the fact (`detectRunaway`: verbatim whole lines only, at counts hand-written code does not
  reach: one line 10 times, a short cycle 5 times, a block of 7 to 80 lines 3 times, the same definition head a third time in one FILE), then rolled back to
  where the loop began (its first copy kept) and continued after an interception that names what repeated, says what is written once is done, and asks for a
  different angle. A second loop in a row starts afresh, unrolled (Gemini recovers once with feedback, then stops). Open: loops ACROSS turns (the same edit
  re-sent, the same SEARCH failing, A→B→A edits; OpenHands flags four identical action/observation pairs or three identical errors). Today's partial cover
  is the no-progress escalation (2 tested revisions without progress allow a restructure) and the restate guard. Deep-dive once a real run shows it: compare
  each transaction with the last few (the same edit bodies, the same canvas hash after applying), then roll the conversation back to before the repeat and
  intercept, as within a reply. Also when a case shows it (owner, 2026-11-18): an escalation ladder past the interception and retry, a penalty added
  dynamically but only over the scope of the loop (its repeated lines or block, e.g. a logit bias or DRY limited to those tokens), never globally: in normal
  turns the model is free to make its own judgement.
- Loops in the thinking (owner, 2026-11-18). Built: the reasoning stream is judged by the same verbatim rules (every 400 characters); a loop stops it where
  the loop began, the rest is carried named as a loop, and the next turn decides with thinking off: the ladder's "summarise and change strategy, fresh
  decoding state" step, without a penalty. Not built, for when a run shows loops it misses or a second loop after the decide turn:
  - Semantic loops (the same idea reworded): verbatim lines miss them. Cheap signals first: similarity of recent reasoning paragraphs (word shingles),
    no new identifiers or line numbers named for N paragraphs. Candidate-set recurrence (the same top-k next tokens repeating, reported to catch loops
    earlier than text) needs per-token `n_probs` from llama.cpp's stream, a cost on every token: measure it before adopting.
  - A scoped, adaptive penalty (nudge, then escape): llama.cpp takes `repeat_penalty` with a recency window `repeat_last_n`, `presence_penalty`,
    `frequency_penalty`, DRY (`dry_multiplier`, `dry_allowed_length`, `dry_penalty_last_n` as its window, `dry_sequence_breakers`) and `logit_bias`, all per
    request, but for the whole request: there is no switch between reasoning and answer within one. So a penalty scoped to thinking needs a turn that
    only thinks (continue the reasoning, then answer in a separate turn with no penalty): the decide-turn split already exists. The experiment: four arms
    (none / fixed scoped / adaptive scoped / summarise-and-restart) on real loops AND on repetition-heavy work (proofs, table-driven tests), scored on
    correctness, tokens and premature stops.
- New leaves, none built yet (evidence: `out/runs/t0-101e19c/gate.md` and `out/runs/wa-scope-v1..v4-partial`):
  - Continuation seams on a long FILE with recorded replies (model-free replay of cut-off replies through `joinContinuation` and the line-boundary resume: lost
    spaces, stray fences, SEARCH/REPLACE of the tail, file restarts). Parent: write-engine.
  - Continuation re-emits functions already written (redefinition): a replayed continuation that repeats a finished function must not leave two copies. Parents:
    write-engine, skeleton.
  - Scope-stop compliance: under an edits-first scope the model must stop when told it is rewriting; `refused` 7 to 11 a run in wa-scope-v2. Parent: replace-function.
  - Escalation trigger: restructure allowed after 2 tested revisions without progress or 2 replies stopped for growing into a rewrite (replay of `turn_control`).
    Parent: replace-function.
  - Skeleton decomposition: skeleton-fill (one stub method), skeleton-expr (the expression layer: expr, term, unary), skeleton-primary (number, name, call);
    skeleton passed 1 of 3 with 80 turns and 48k tokens. Parent: skeleton.
  - Thinking-stop behaviour in attempts after a card rewrite: wa-scope-v3 runs ended with 25 to 60 reply tokens, 17 to 18 aborted turns and 3 thinking stops. Parent:
    replace-function (a replay of attempts 2 and later).
  - A grammar-precedence leaf: only `unary_minus_binds_looser_than_power` (`-2^2` is -4), the test that recurs in the failed rewrites. Parent: replace-function.
- From the tier 1 smoke (b155485, forced past an incomplete gate, `out/runs/t1/qwen35-20261118-150840`): 10 of 22 tests failing after attempt 1, unchanged
  through attempts 2 to 5. Two defects in one 447-line calc.cpp: `peek()` returns a character only for OP tokens while `(` `)` `,` are their own token types
  (every function call and parenthesis fails), and `^` is tokenized but has no grammar level. What T0 does not test, as leaves:
  - **fix-folded** (the main gap): a defect inside a function the view folds (a file of about 450 lines at ctx 16384; every T0 file is shown whole). T1
    showed: the same READ ranges asked turn after turn and attempt after attempt (31-102 and 135-364, at least 6 times), reads used up and turns spent on
    refusals, a SYMBOL edit whose SEARCH copied the fold marker (`... (72 lines folded: 31-102; tokenize)`), an empty SYMBOL edit, a long-search stop.
  - **fix-contract**: two functions that disagree on a representation (a token kind against a character); either side may be fixed, the tests name neither.
  - Harness, not a case: a READ of lines already answered in this attempt is the multi-turn loop of the Loops entry (only the latest answer stays in
    view, so the worker reads again). Built (2026-11-18): what was read stays open in the view (it joins the view's focus); where the window has no room
    (ctx 6144) it is answered again from the file as it stands, above a view folded further; a repeat on an unchanged revision is named as one.
    Open (owner, 2026-11-18, just noted): should the repeated-read guard have a forced override, a way for the worker to insist it needs those lines
    again (e.g. a `FORCE` on the READ, answered without the note)? Today a repeat is still answered and only counts against the read allowance.
- Fold markers that carry a status (owner, 2026-11-18; A/B, to iterate): `... (16 lines folded: 37-52; term)` could also say what the worker knows of it,
  e.g. `unchanged since you read it (turn 2)`, `changed since you read it`, `not read`, `stub` / `no content` (a body that only raises or returns a
  neutral value). Measured as an arm against today's markers on fix-folded and the T1 plan: reads per attempt, repeated reads, turns, pass rate. The
  markers are rendered by Sub0Canvas (`foldedView`), so the status needs a per-region annotation passed in (by name or line range).
- Measure what folding saves (owner, 2026-11-18): the trade is not free. A fold marker costs a line, and a read costs a turn plus the answer; folding a
  three-line body saves little, and folding a body the worker then reads costs more than showing it. Per run: the view's characters folded against
  whole, prompt tokens per turn, reads and repeated reads, and the share of folded regions later read. Then tune the fold threshold (today a body of two
  lines or more folds) and try folding only what is far from the focus. A model-free first pass: render the T1 calc.cpp at ctx 6144 / 8192 / 16384 and
  count the characters each view and each read answer takes.
  - Length is not the measure; what the extra code is, is. Analysed 2026-11-18 (T0 write-engine run 1, attempt 1, `canvas/src_calc.cpp.r2` against the reference
    `validation/reference/calculator-cpp/src/calc.cpp`): the 587-line file was two engines (a class parser cut at line 328, then a continuation that wrote a second,
    free-function engine instead of finishing it: a seam, fixed by the new-block join). Part 1 alone: 2 661 code tokens, cut before pow/min/max and `evaluate`, against
    1 759 for the complete reference (about 1.7 times when finished; lines exaggerate it, 2 times, through formatting). The extra was not added expertise: the same
    feature set and checks as the reference, and no more; per-function copies of the arity check and the degree conversion (20 copies against one shared check and
    `toRad`/`fromRad`); dead code (an unused `Token` struct, an unused `get()`, `parse_primary` defined and never called); 14 lines of precedence reasoning left in
    comments; a hand-written number lexer four times the reference's `strtod`, with a defect (`std::stod` throws `std::out_of_range`, not `std::runtime_error`, on
    `1e999`); and a grammar that cannot work (`parse_unary` and `parse_power` call each other with no way down to a number: endless recursion), less specific error
    messages ("wrong argument count" against "sin takes one argument"). So the fix is quality, not brevity: tell the worker its reply limit and ask for one place per
    rule, no dead code, no reasoning in comments (done in the canvas turn tail, 2026-11-18); and measure it in quality_t0: repeated blocks, unreferenced functions and
    types, comment-reasoning lines, code tokens against the reference, never a line count alone. Parents: write-engine (where it shows), write-function (cross-cutting).
  - fix-missing-check, 19 turns and 0 of 3 first attempts for one missing check (against 3 turns for the other single edits), and bugs-3 at 1.29 defects a turn against 3.0
    for bugs-6: a leaf that separates "does not know where to add" from "adds it wrongly". Parents: fix-missing-check, bugs-3.
  - write-file, 1 of 3 first attempts and 22 turns for a 43-line app: a smaller leaf (argument parsing only) to tell the file length from the content. Parent: write-file.
- Gate judging from run data the summary rows hold but the gate ignores: attempts to pass (wrong-approach passed on its 5th attempt), aborted turns and thinking stops;
  the WEAK state of T0_LAYERS.md section 3.2.

**Getting started and run hygiene**
- `project submit --task calculator-cpp` should consult the harness registry (`harnessWithBlueprint`) instead of stopping at `needs a plan`.
- `up` warns when an older daemon is running; `up` starts the portal by default in the scripts (`scripts/start-campaign.sh`) and prints its address.
- The daemon writes `run-info.json` at workflow start and the portal shows it. No `v*` tag exists yet, so the version is 0.0.0 plus the commit count.
- Wizard, not built: the plan shown and editable before it runs, a blueprint picker ([PROCESS_DESIGN.md](PROCESS_DESIGN.md) section 16). Building and
  supervising `llama-server` moved to Sub0LlmEngine's backlog (`docs/BACKLOG.md` there); offering and fetching models to Sub0LlmModelRegistry's providers
  (`python -m sub0llmregistry fetch`, which asks before any download). The wizard calls both.
- Planning questions not yet asked: the bar for done and the engine (they need the process and engines replaceable after submission; section 15), choices from the
  harness registry instead of the calculator's three.

**The engine and models from Sub0LlmEngine and Sub0LlmModelRegistry** (owner, 2026-11-16; both are ready to use, the squad does not use them yet; session brief: [sessions/engine-registry-integration.md](sessions/engine-registry-integration.md))
- Start, stop and query the engine through `python -m sub0llmengine start|status|stop` instead of `scripts/start-campaign.sh` building its own `llama-server`
  line (`LLAMA=`, its flags and model path). The campaign reads the slot count, context per slot and endpoint from `status` (the `squadCtx` key is the context to
  plan per request), so E2 (the window plan) takes its KV cost per token from the engine's facts, not the start log.
- Name models by registry alias (`qwen36-35b-a3b`, `qwen38-27b-q4_0`), resolved by `sub0llmregistry`; record the registry id and the engine's build label in
  `run-info.json` and the campaign report, so every result names its exact weights and build.
- Choose the engine configuration from evidence: the runtime explorer's *Export selection + evidence* proposal (`python -m sub0llmengine selection`) is the
  configuration a campaign starts with, kept with the run; an A/B on the engine is an experiment like any other (same seeds, both selections recorded).
- Take the bench lock (`out/bench.lock`) into account: a campaign must not run while Sub0LlmEngine benches, and benches refuse while the engine serves.
- **Dynamic context by default** (owner, 2026-11-16): a request plans against a soft target (`squadCtx`), not a hard ceiling,
  keeps going past it while the shared KV pool has room, and reacts when it runs out, so one agent can take more while
  others are idle. What llama-server does (upstream `c7518db`, `tools/server/server-context.cpp`), which the reactions must
  follow:
  - With unified KV a slot's own limit is the whole pool (`llama_n_ctx_seq`), unless `--kv-unified-per-slot N` caps it.
    Reaching it ends that request with `finish_reason: "length"` (truncated). Reaction: compact or summarise the
    agent's context and continue, or split the task.
  - When the pool is full, the server first clears idle slots (saved to the host prompt cache with `--cache-idle-slots`),
    then halves the batch. Idle capacity is reclaimed automatically, which is the case this design wants.
  - When it is still full with every slot busy, it fails **every** active request ("Context size has been exceeded.")
    and clears their contexts (an upstream TODO says it should end only the largest). One greedy agent can therefore
    cost all running work. Reactions: retry with back-off, re-prefill from the host prompt cache; and avoid the case:
    admission control on pool headroom (start or grow a request only with room left), a ceiling per slot above the
    fair share (`--kv-unified-per-slot` at about half the pool), and an upstream fix to end only the largest sequence.
  - Early warning into the agent's context, **only when the level changes** (owner, 2026-11-16), not in every message:
    the engine's pool watermark (`status --json` `pool`: used / reclaimable / free, levels `ok`, `notice` 70 %,
    `warning` 85 %, `critical` 95 % used) is read by the squad, which tells an agent once when the level crosses a band,
    up (worse) or down (better), with exact figures at that moment: `context: shared pool now 9.4k of 64k free
    (warning, was notice); you hold 12.1k`. Rules:
    - Every note costs tokens in the pool it warns about, so near exhaustion notes must be rarer, not more frequent:
      one note per crossing per agent, never a repeat at the same level, and nothing while the level is unchanged.
    - Hysteresis so a level near a threshold does not flap: go up at the threshold, come back down only 5 points below
      it (for example `warning` from 85 % up, back to `notice` below 80 %); a level must hold for one engine poll
      before it is announced.
    - Timely: the squad polls the engine between turns (and on a cadence while generations run) and attaches a pending
      note to the agent's next message; a crossing to `critical` reaches every active agent at its next turn.
    - Precise and actionable: the figures at the crossing and one instruction for the level (`warning`: finish the
      current step and compact; `critical`: stop and hand back a summary). `ok` after a warning says the room is back.
    - Squad-side action does not wait for the note: at `critical` the squad stops admitting or growing requests.
    - Place the note at the end of the newest message, never in the system prompt or earlier turns, so the cached
      prefix is kept.
  - Fixed budgets stay available as a mode for experiments (same seeds must mean the same limits). The watermark line is off or fixed in experiments, since it depends on the other agents.
  - **Where the squad is today** (owner, 2026-11-17, after the 35B C++ calculator run on an engine started with `-c 65536 -np 4 -kvu`, so the
    context is one shared pool and not 16k per slot): the squad still treats the engine line's `ctx=` as a fixed window per request. The session
    and canvas agents fold the file view to fit `ctx - maxTokens` (`llm_agent.cpp`, `canvas_agent.cpp`), the planner sizes its reply from it
    (`planner.cpp`), and every reply has a fixed cap (`--max-tokens`, the fork's 1,500-token skeleton). With a pooled cache these are guesses that
    leave room unused while other slots idle, and the run showed the cost of a fixed cap: with reasoning on, the skeleton spent its whole 1,500 on
    thinking twice and wrote nothing (fixed for the skeleton since: reasoning gets the `think` budget, `fork.cpp` `kThinkingMaxTokens`). Steps:
    take `ctx` as the target above, read the pool (`/slots`, `/metrics` with `--metrics`) to size views and replies, and treat `finish: "length"` as
    "grow or compact", not as the attempt's failure.
- Later: workload-adaptive configuration (a prefill-tuned engine for the planner's long first prompt, then the serving configuration with the KV kept, measured in
  Sub0LlmEngine's reconfiguration spike) once the engine offers it as a request-level switch. Known model faults to avoid meanwhile: Qwen3.8-27B UD-IQ3_XXS is wrong with
  CUDA offload (use `qwen38-27b-q4_0`), and Qwen3.8-Flash-Next differs on the first long prefill after load (Sub0LlmEngine `docs/issues/`).

## Later

**Many experts, one result: injected faults.** Once the setup works end to end: a seeded-fault campaign, one defect per role and fault class (a wrong test literal,
a swapped argument, a contract contradicting its example, a missing export, a wrong dependency, a false review finding, a bad lesson), each scored **raised**
(who noticed), **routed** (did it reach the owner), **resolved** (by whom, in how many turns and tokens) and **contained** (did the project finish with no silent
workaround; bending correct work to fit a wrong artefact is scored worst). Faults are data (`registry/faults/*.json`), injected by `--inject` generalised to
every role. Needs expectation QA, the routed CONCERN channel and the habit scores of [METRICS.md](METRICS.md) section 4a.

**Workflow**
- The V, remaining: attribution of a failed integration by a model; review against the card; send-up proposals against a parent's interface; bubble-up to a
  rearchitect of the parent when the round-trip budget is spent. Contract amendments from a leaf to its coordinator, versioned contracts, dependents marked stale.
- Leaf drafts as first-class outputs for siblings and the integrator; capacity reserved for structural jobs; a cost rule for dividing instead of the size rule.
- Parallelism inside a node: areas with interface skeletons, redundant forks on idle slots, prefix reuse across forks (E6).
- A project-manager pass on idle capacity filing proposals; a model-backed overseer for the `owner` class; a reporter step for non-web deliverables.
- The knowledge register and the dreamer (rule-based first, then a model on idle slots, preemptible); a project-level register that outlives workflows.
- Shed jobs requeued without costing an attempt; memory-aware engine leases; reuse of accepted outputs across workflows by content hash.
- The keyed history store and the Sub0Log trace stream (`history-1`, `export`, `compare`).
- Skills fetched when needed (format texts injected at the turn that needs them, `NEED <name>`); a retry shows only the failing tests plus the names of the rest.
- A debugger worker (the failing case under an inspector: expected and actual, divergence, trimmed stack); QA scan on idle capacity; browser smoke test; web lint.
- The frontier stub has no reference for a planned item: a real reference path or a stated "the stub cannot help here".

**Portal**
- Node tree, still open: collapse a finished container, zoom and pan, a mini-map, the V lanes and round markers, edge bundling, focus on the active frontier, a
  unit test of the layout (crossings and containment on a fixture plan).
- "Copy as prompt" on the briefing; node detail with card, last prompt and gate output side by side; engine and memory gauges; cached tokens as a figure;
  quotas and the frontier switch as governance requests.
- A canvas pane: claims, presence, notes ([CANVAS.md](CANVAS.md) slices 1 to 4).
- Remote viewing (proposed, not agreed; loopback-only by design): an opt-in, read-only flag with a token, or a tunnel. Needs the owner (STOP AND ASK). Today:
  `ssh -L 8780:127.0.0.1:8780 USER@HOST`.

**Platform and CI**
- The platform library ([PLATFORM.md](PLATFORM.md)): slices 0 to 4 built 2026-11-16 (the system-header check; one process launcher; process identity
  and host facts, `ProcessId` out of the public headers; files, with one copy of the read and write helpers; sockets and the instance lock), each with
  Windows backends. Slice 5 (the daemon's and the shim's wait loops, the log follower, the pidfd watch) is done, and the program runs natively.
- `std::format`/`std::print` and `contains`/`starts_with` ([DECISIONS.md](DECISIONS.md) 25): piloted 2026-11-16 on `replay.cpp` and `sandbox.cpp` (the
  specifier mapping checked byte for byte against `printf`, rounding cases included). Left: about 380 `printf`-family calls in 35 files, 28 `snprintf`,
  77 `find(...) != npos`. One layer per change, kernel upward, with the goldens as the guard; a layer is done when `git grep printf` is empty in it.
- Windows and macOS builds, after the platform library (about 3,000 lines for Windows: [PLATFORM.md](PLATFORM.md) section 6). The platform layer's
  Windows backends are built and tested natively (MinGW-w64); slices 4 and 5 are done the same way, and the program runs natively under MinGW-w64; next is the domain layer under MSVC. The HTTP stack on Boost.Beast and Asio
  behind the seam ([DECISIONS.md](DECISIONS.md) 18).
- CI for the squad: build, tests, ASan and UBSan, TSan, clang-tidy, and a GCC job (the GCC build was made clean on 2026-11-16).
- Split the largest files (`test_workflow.cpp`, `client.cpp`, `portal.cpp`).
- A larger sandbox project, the knowledge store (design only:
  [KNOWLEDGE_STORE_SCOPE.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/KNOWLEDGE_STORE_SCOPE.md), archived).
