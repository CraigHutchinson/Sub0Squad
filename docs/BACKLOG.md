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
- The remaining scenarios: interface gap, decompose, reviseTests, read-then-edit, integrate, length continuation.
- Blocks 7 to 10: section integration seams, division, golden decision logs, process as data as a block.
- The fork executor inside a workflow through the daemon (`llm-fork --agent-exit 1` as the agent job) has no test.

**Getting started and run hygiene**
- `project submit --task calculator-cpp` should consult the harness registry (`harnessWithBlueprint`) instead of stopping at `needs a plan`.
- `up` warns when an older daemon is running; `up` starts the portal by default in the scripts (`scripts/start-campaign.sh`) and prints its address.
- The daemon writes `run-info.json` at workflow start and the portal shows it. No `v*` tag exists yet, so the version is 0.0.0 plus the commit count.
- Wizard, not built: building and supervising `llama-server`, offering and fetching models (each a download, install or service: the owner's go-ahead), the plan
  shown and editable before it runs, a blueprint picker ([PROCESS_DESIGN.md](PROCESS_DESIGN.md) section 16).
- Planning questions not yet asked: the bar for done and the engine (they need the process and engines replaceable after submission; section 15), choices from the
  harness registry instead of the calculator's three.

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
- The platform library ([PLATFORM.md](PLATFORM.md)): slices 0 to 3 built 2026-11-16 (the system-header check; one process launcher; process identity
  and host facts, `ProcessId` out of the public headers; files, with one copy of the read and write helpers). Next: the 21 text-mode `std::ofstream` writes
  onto `platform::writeFile` as files are touched; slices 4 and 5 (sockets, the daemon's wait loop and its pidfd watch) when the experiments leave room.
- `std::format`/`std::print` and `contains`/`starts_with` ([DECISIONS.md](DECISIONS.md) 25): piloted 2026-11-16 on `replay.cpp` and `sandbox.cpp` (the
  specifier mapping checked byte for byte against `printf`, rounding cases included). Left: about 380 `printf`-family calls in 35 files, 28 `snprintf`,
  77 `find(...) != npos`. One layer per change, kernel upward, with the goldens as the guard; a layer is done when `git grep printf` is empty in it.
- Windows and macOS builds, after the platform library (about 3,000 lines for Windows: [PLATFORM.md](PLATFORM.md) section 6). The platform layer's
  Windows backends are built and tested natively (MinGW-w64); next are slices 4 and 5 with both backends each, then the domain layer under MSVC. The HTTP stack on Boost.Beast and Asio
  behind the seam ([DECISIONS.md](DECISIONS.md) 18).
- CI for the squad: build, tests, ASan and UBSan, TSan, clang-tidy, and a GCC job (the GCC build was made clean on 2026-11-16).
- Split the largest files (`test_workflow.cpp`, `client.cpp`, `portal.cpp`).
- A larger sandbox project, the knowledge store (design only:
  [KNOWLEDGE_STORE_SCOPE.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/KNOWLEDGE_STORE_SCOPE.md), archived).
