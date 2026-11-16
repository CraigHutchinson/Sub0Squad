# Backlog

> **Resuming? Read `docs/RESUME.md` first**: it orders everything below into a priority list for the next session (2026-11-16).

> **PRIORITY (owner, 2026-11-15): the clean-slate sandbox, `docs/CLEAN_SLATE.md`.** No hand-written plan and no reference implementation inside the harness: a planner role plans from the bare request, a plan review and expectation QA check it, and `validation/grade_run.py` judges the result from outside. This comes before further baseline campaigns and before the other items below.


Work that is understood but not scheduled. Newest first within a heading; each item says where its design lives. The roadmap ([ROADMAP.md](ROADMAP.md))
holds milestones; this holds the things noticed on the way.

## Planning in phases, windows and slots (owner direction, 2026-11-15; design in `docs/CLEAN_SLATE.md` section 6)

- **Measure the KV cost and give roles their own windows**; the planner and others budget against the window read from the engine profile.
- **Tell the planner the slot goal** (slots, window per slot, tokens per second) and feed `plan-lint` into its repair messages.
- **Split planning into design (rare, wide window) and execution-wave planning (frequent)**, a looping stage list: design, plan next wave, build, integrate, review, repeat; changes through the consolidation step.

## Portal

### Node tree presentation: grouping, ordering and connections (2026-11-10, from a screenshot of the live V run)

**Status (same day): first pass built** (`portal_page.cpp`, `buildTree` and `layoutScope`). A divided parent is a container (dashed box) holding its
parts, laid out recursively; units are layered by dependency and ordered by barycentre sweeps; placement is deterministic from the plan so a
status change never moves a node; edges are one curve per dependency between facing sides, containers drawn under the edges and the nodes over
them; ids are shown short inside a container; the activity icons are letters (D dividing, A card rewrite, I integrating); live text is clipped with
an ellipsis. Measured on the live V run (13 nodes, 2 containers): **0 edge crossings, 0 edges through a node, positions identical across
refreshes** (the screenshot below had many crossings). **Still open:** collapse a finished container to one line, zoom and pan and a mini-map,
the V lanes (descent and ascent) and per-coordinator round markers, edge bundling for wide fan-in, focus on the active frontier, a table-of-contents
for very large plans, and a unit-style test of the layout (crossings and containment on a fixture plan).

What the screenshot of `w-000001` (a divided workflow on the 9B) shows, and what should change:

- **Parts are not grouped under the node that divided them.** Nodes sit by dependency column and level, so the parts of different parents
  interleave (`ui-js.ui-edit`, `engine.lexer`, `ui-js.ui-detect`, `engine.functions` in one row), and a coordinator (`engine`, `ui-js`, drawn as
  "integrator") sits in the top row far from its parts. Wanted: a divided parent drawn as a **container** (a labelled box) holding its parts, the
  parts ordered inside it, and the coordinator as the box's frame or footer, so the V reads as nesting, with finished containers collapsible to one
  line (state, rounds, tokens, last verdict).
- **Edges cross and run through unrelated nodes.** The dashed "divided into" edges and the solid dependency edges are long splines that cross
  five or six times. Wanted: a layered layout with **crossing minimisation** (barycentre ordering within each layer), edges routed between
  layers (orthogonal or short splines), **bundling** for fan-in and fan-out (all parts into their coordinator, one tester into several
  developers), and the "divided into" relation shown by containment instead of an edge.
- **Order is by arrival, not by meaning.** Wanted: a stable, meaningful order (test items beside the thing they grade, parts in dependency
  order, siblings in the order the division listed them) that **does not jump between refreshes**; layout must be deterministic from the plan so the
  keyed patching keeps nodes in place, and a node that changes state changes colour, not place.
- **The V is not visible.** Descent on the left and ascent on the right (design: `docs/WORK_GRAPH.md` sections 7 and 13): lanes by phase (tests,
  implementation, integration), the current oscillation highlighted, round-trip count on each coordinator.
- **Small defects:** the activity icon for "dividing" (`÷`) reads as a plus at that size, so use a distinct shape or colour pair; the model badge
  overlaps long node ids and the live line is clipped at the node edge (`... decoding 549 tok (1.95k le`), so give the node room or wrap, and
  truncate with a tooltip; level labels should say what the level is.
- **Scale:** past about 30 nodes the whole graph cannot be read at once. Wanted: zoom and pan, a mini-map, focus on the active frontier (running
  and waiting nodes plus their neighbours), highlight of the dependency path on hover or selection.

Acceptance: a plan like the one in the screenshot (6 top-level items, 3 divided into 3 or 4 parts) renders with no edge crossing a node that is not an
endpoint and at most a few edge crossings; each divided parent is a container with its parts inside; two refreshes with unchanged data leave every
node where it was; a collapsed container shows its state and counts. Design notes first (compound layered layout; which library or a small
hand-written Sugiyama pass; how it works with the retained-mode patching in `portal_page.cpp`).

### Other portal items

- A "copy as prompt" button on the briefing; the briefing for any request, not only escalations (`WORK_GRAPH.md` section 17).
- The visual V and per-node verdict history; node detail with the card, the last prompt and the gate output side by side; engine and memory
  gauges (sections 8, 12).
- Quotas and the frontier switch as governance requests, shown in the inbox (section 14).

## Workflow

- **Agents raise requests themselves** (a reply block asking a question or reporting a blocker instead of guessing); impact analysis for a
  requirement change from the graph and keyed history instead of the scope the owner ticks (section 14).
- **The V, remaining:** attribution of a failed integration by a model rather than by file names; review (a model reading the combined result
  against the card); send-up proposals against a parent's interface; bubble-up to a rearchitect of the parent when the round-trip budget is
  spent (section 13).
- **Parallelism inside a node:** the plan phase emits areas with interface skeletons; serial area turns; redundant forks on idle slots; prefix
  reuse across forks with llama.cpp slot save and restore, to be checked on our build (section 15).
- **Project-manager pass** on idle capacity filing proposals into the inbox (section 11); model-backed overseer that proposes options for the
  `owner` class; harness facts per project type (C++ builds and tests) (section 17).
- **A reporter step** that decides the deliverables and writes the final report for non-web outputs (section 12).
- **Knowledge register and the dreamer** (`WORK_GRAPH.md` section 19): the register format and its readers (briefing, decompose prompt, worker prompt as an
  experiment, the project-manager pass); a rule-based dreamer first (overseer verdicts and accepted-after-failure transitions), then a model-backed
  one on idle slots, preemptible and lowest priority; a project-level register that outlives workflows. Recall (`sub0squad recall`, the portal search,
  the briefing's conversation tail) is built.
- **Shed jobs** are requeued without costing an attempt; memory-aware engine leases (KV cost per slot) (section 11).
- **Reuse accepted outputs across workflows** by content hash, so a rerun does not redo accepted nodes.
- **Keyed history store and the Sub0Log trace stream**: `history-1`, scope and visit keys, `export` and `compare` (section 10).
- A frontier-stub that has no reference for a plan's items is useless for escalation: either a real reference path or a documented "stub
  cannot help here" (the policy already skips it).

## Prompts and tokens (from [PROMPT_REVIEW.md](PROMPT_REVIEW.md))

- **Repair by edits** (search and replace blocks, whole file as fallback; architect corrections return only the corrected items): about 80 % of repair
  output was unchanged text.
- **A cost rule for dividing** (serial saving against division, parts' tests and integration) instead of the size rule alone.
- **Rolling conversation window**; **tidy the system block** (generated harness brief, persona-neutral prefix first); a retry includes only the failing
  tests plus the names of the rest.
- **Skills**: a registry of format and capability texts, injected at the end of the turn that needs them, listed by name otherwise, `NEED <name>` for
  optional ones, tolerant parsing; measure how reliably a 9B asks. A/B each change with seeded runs (tokens by kind, cached tokens, time, acceptance).
- Slot affinity and the cached-token figure in the trace are built (second build); show cached tokens as a portal figure.

## Platform

- Windows and macOS builds; the HTTP stack on Boost.Beast and Asio behind the seam (DECISIONS 18); Sub0Llm roofs PR #102 follow-ups.

## Feedback loops (see docs/FEEDBACK_LOOPS.md)
- Test oracle for tester output (reference implementation / independent attempt).
- Debugger worker: on request or repeated failure, runs the failing case under the inspector and returns expected/actual values, divergence point and trimmed stack; accepted-with-failures nodes always list which tests failed and how.
- Bar as data; one result shape for checks; checks recorded in node history.
- QA scan as a workflow step on idle capacity; browser smoke test; web lint profile; loop metrics.
- CI for the squad: build, tests, ASan/UBSan, TSan, clang-tidy.

## Run hygiene (2026-11-10)
- One build (`out/build/dev`, from main) and one daemon home per live run; no legacy builds or daemons kept.
- Finished run homes go to `out/archive/<name>.tar.gz` (journals, plans, traces, reports, accepted outputs; workspaces excluded) with a line in `out/archive/MANIFEST.md`.
- The family's `BareCpper/Version.cmake` (v0.4, via CPM in `cmake/dependencies.cmake`) stamps git describe, sha and dirty flag into every build (`sub0squad --version`, `sub0squad::version::version_full`). No `v*` tag exists in this repo yet, so the version falls back to 0.0.0 plus the commit count; tag a release to get a real one. Each run should write `run-info.json` with that sha (done by hand for run-v5): **todo: the daemon writes it itself at workflow start and the portal shows it**; and an `archive` command that does the tar + manifest.

## Co-evolving structure and leaves (see docs/BUILDING_BLOCKS.md)
- Contract amendment proposals from a leaf up to its coordinator (a delegated request), versioned contracts, dependents marked stale and kept as drafts.
- Leaf drafts as first-class outputs handed to siblings and the integrator; iterative multi-round integration.
- Capacity reservation for structural jobs (one slot) without starving leaves; re-run the structural step when enough leaf drafts have landed.

## Canvas (owner idea, 2026-11-11)

- A live shared document with section claims, presence, notes and worker-to-worker requests: design in `docs/CANVAS.md`; first slice (claims and presence, read-only) waits for the forked-session work and a real run of the folded views.

## Getting started (owner request, 2026-11-12)

- **An end-to-end first-use sample on the root README with minimal setup.** Today the README has Build and Licence but no first use, and the commands live scattered across `docs/RUNNING_PROJECTS.md`, `docs/HANDOFF.md` and the CLI help; the owner could not find how to start a job by hand. Wanted: one short path from a clean checkout to a finished project, copy-pasteable, each step verified by CI or a test so it cannot rot: (1) build; (2) one command that starts everything needed (daemon, portal, and an engine: either the mock agent or a local model, with the memory guard); (3) `sub0squad do "write a calculator in C++"` (triage picks the blueprint, the plan loads itself, no `project plan` step); (4) open the portal and watch a node's transcript live; (5) where the finished project and the report are; (6) how to stop. Plus a mock-agent variant that runs in a minute with no model at all, and a second sample that adds a new workflow (a registered process) to show the toolbox. Acceptance: a test runs the mock sample from a temporary checkout; the README commands are the commands the test runs.
- **Blueprint tasks should start without a hand-supplied plan.** `project submit --task calculator-cpp` ends at `needs a plan` because only triage on task text loads a known blueprint; submit should consult the harness registry (`harnessWithBlueprint`) so any registered blueprint starts at once.
- **BUILT, first slice (eighth pass; `docs/PROCESS_DESIGN.md` section 16, `src/wizard.cpp`, `test_wizard`): the checklist console of points 3, 4 (explain
  only), 5 (existing commands, `--yes`, `--script`; NOT a registered process, for the reasons given there) and 6 below. NOT built: points 1 and 2 (building,
  starting and supervising `llama-server`, offering and fetching models: each is a download, an install or a service, which need the owner's go-ahead),
  the plan shown and editable before it runs, a blueprint picker.**
- **`sub0squad wizard`: streamlined, guided onboarding (owner request, 2026-11-13).** From a clean checkout to a first finished project with the fewest decisions, in one
  interactive console, driven by the harness rather than by prose instructions. Wanted:
  1. **Build includes the engine.** llama.cpp is built by default as part of the build (a pinned revision, fetched through the same dependency mechanism as the Sub0 libraries, an
     option to use an existing build or system install), so `llama-server` exists after one build. Any download or install still needs the owner's explicit approval; say what
     will be fetched and its size first.
  2. **Engine management.** The wizard starts and supervises a `llama-server` for the chosen model: contained (a memory cap and no swap, run as a user service so an out-of-memory
     kill takes the engine and not the session), with context, slots and threads chosen from the host (`doctor` figures), stopped by `down`. It offers models that fit the host, names the
     minimum bar (the 35B, `docs/PROVING_GROUND.md`) and says plainly when the host cannot run it; model files are fetched only with approval (`fetch-model`: guarded, resumable, verified).
  3. **A guided flow shown as a checklist.** What is needed (binary, engine, daemon, portal, memory, ports, a model, a project) with each item's state, the next step highlighted,
     and a choice at each: start the portal (and open or print its link), pick a project (a blueprint from the registry, or free text that triage turns into a plan, with the plan shown and editable
     before it runs), pick the executor and frontier mode (off by default), start, watch, show where the results are, stop.
  4. **The model helps, within bounds.** Once the engine is up, the wizard can use it to make the flow interactive and flexible: turn a vague request into a plan the owner reviews, explain
     a `doctor` finding, propose the fix, and apply it **only after the owner confirms** (a command to run, a limit to change, a file to write: shown, then approved, never silent);
     it never downloads, installs, changes system or security settings, or sends anything off the machine on its own. The wizard works with no model at all (a fixed script of the same steps, the mock agent for a first run).
  5. **Harness driven, not a special case.** The wizard is a registered process (stages, checks and prompts as data, `docs/PROCESS_DESIGN.md`) like any other workflow, so adding a step or
     an experiment extends the toolbox instead of the code: each check is a `doctor` check, each action an existing command (`up`, `do`, `portal`, `fetch-model`). `--yes` and `--script FILE`
     give a non-interactive path, which is also how it is tested.
  6. **Testable.** A test drives the wizard end to end from recorded keystrokes against the fake engine and a temporary home (no model, no network, no real service), checking the checklist
     states, the confirmations, that nothing is changed without approval, and that the project it starts finishes with the mock agent. The README's first-use section points at the wizard as the
     shortest path and keeps the plain commands for scripting.
  Depends on: process-as-data, `doctor`, `up` and `down` (built), engine service management, the fake-engine test harness (built). Out of scope for a first slice: a graphical front end, remote hosts.
- **BUILT (eighth pass; `docs/PROCESS_DESIGN.md` section 15, the `clarified` process, `test_clarify`): the planning iteration below, for the two topics an
  answer can change (platform, executor); the other topics are settled as "not asked" with the reason. Open: the bar for done and the engine as questions
  (they need the process and the engines to be replaceable after submission), choices from the harness registry instead of the calculator's three.**
- **A planning iteration that asks clarifying questions (owner direction, 2026-11-13).** Today an underspecified request gets silent defaults: an unspecified calculator goes to the C++ blueprint,
  the executor is `session` until the real-model comparison exists, the frontier tier is off. These defaults are accepted for now. The planning step (a stage of a registered process, and the first step of the
  wizard) should instead ask the owner the few questions that change the outcome before any work starts, through the inbox (`request raise --kind clarification`, the answer comes back as data): which platform
  (C++, Python, web), what scope and size, which executor, which engine and model, what the bar is for done; ask once, remember the answers in the project's knowledge register, offer the current defaults as
  the first option so "just go" is one keypress, and never ask what the request or the harness already says. Acceptance: a test with a vague request shows the questions, an answered request shows none, and
  the defaults path needs no answer.
- **`up` should warn when an older daemon is already running** (a build id differs from the binary's): the submit-time refusal exists (`archive`, row 20); the warning belongs at start as well.
- **Portal on the local network (proposed, not agreed; the portal is loopback-only today by design).** The portal serves run files, prompts and transcripts and takes owner actions (raise, answer), and checks the
  Host header against loopback, so it must not simply bind a LAN address. If remote viewing is wanted: an explicit opt-in flag (never the default), read-only (the action endpoints off), a shared token in the URL or a
  header, the Host check extended to the chosen name, and the address printed by `up`; until then use an SSH tunnel (`ssh -L 8780:127.0.0.1:8780 USER@HOST`). Needs the owner's go-ahead before it is built.
- **A `doctor` command** that checks the first-use prerequisites (binary version, engine reachable, memory guard, ports, cgroup limits) and says what to fix.

## Metrics and ways of working (owner request, 2026-11-14; design in `docs/METRICS.md`)

- **Accounting that reconciles with the engine (first, and a blocker for every efficiency figure).** The ledger records a fraction of a forked run's tokens (1.4k, 38k and 40k against 397k, 221k and 273k in the fork
  attempts' own records for runs v9 to v11): per-session usage records with a `purpose`, ledger rows per session, a reconciliation check against the engine's counters, and `--metrics` on the engine start line.
- **Engine ticks as the load-independent clock**: counter snapshots by the daemon, decode ticks, concurrency factor, tick latency to quality, critical path.
- **A scorecard per run** (`sub0squad metrics WORKFLOW`, the report, `metrics.json` in the archive, a portal panel) and `metrics backfill` for the archived runs; **`metrics compare`** with confidence intervals and a regression gate in `block run`.
- **Independent quality capture** at integration (the reference suite the model never sees; seeded defects against the model's tests).
- **The fork experiment**: session, canvas and fork executors on the same task and seeds, judged by quality per token and ticks to the accepted whole; its result decides the default executor.

## Many experts, one result: QA on expectations and injected faults (owner request, 2026-11-15; evidence in `docs/WORKER_PROBE.md`)

- **The QA pass checks EXPECTATIONS, and the gate says so (next, after the `iter1` campaign).** Today QA looks at code (duplication, long functions, warnings). The first campaign showed that a failure can sit in an expectation: a tester's tests that contradict the contract (row 77), a contract that is thin or self-contradictory, a card that names a different interface than the tests. Make "are the expectations right?" an explicit, recorded gate step with its own verdict, not a side effect of the developer failing:
  tests against a reference or a second independent reading (the C++ oracle is the first cut), tests against the contract's examples, contract against the tests' imports and signatures, and a QA persona whose output is a list of expectation findings (`ok` or `suspect: <line, why>`). A suspect verdict routes to the owner of the expectation (the tester, the planner or keeper), never to the developer's retry loop; the developer is told that the expectation is under review rather than asked to satisfy it.
- **Fault injection at every initial step, once the setup works end to end (a backlog item, not before).** Every expert can make mistakes: the planner (a wrong or missing contract, an impossible acceptance), the architect (an over-large or invalid division), the tester (wrong expected values, tests that pass on a stub, a missing case), the developer (a semantic slip, an unused helper), QA and the dreamer (a wrong finding, a wrong lesson), the harness itself (a mislabelled gate). The squad's design says that together they correct each other; this proves it. A **seeded-fault campaign**: for each role and each fault class, inject one defect into that role's output at its step (a fixture library of faults per class: off-by-one in a test literal, a swapped argument, a contract that contradicts its own example, a missing export, a wrong dependency, a plausible-but-false review finding, a bad lesson in the knowledge register) and measure four things, per fault: **raised** (did any downstream expert notice and say so, and who), **routed** (did the concern reach the owner of the faulty step, via the CONCERN channel of `docs/CONTEXT_AND_PLANNING.md`), **resolved** (was the fault corrected, by whom, in how many turns and tokens), and **contained** (did the project still finish, with no silent workaround). A fault nobody raises is a hole in the process; a fault raised to the wrong role shows a routing gap; a fault "fixed" by bending a correct artifact (the engine changed to match a wrong test) is the worst outcome and is scored as such.
  Method: the faults are data (`registry/faults/*.json`: step, class, the edit, the expected detector), injected by the existing `--inject` machinery generalised from developer defects to every role; the results are a matrix (role x fault class x outcome) in the campaign report; the expert-pairing questions (who should have caught it: QA, the next role, the keeper) feed the review stages of the processes. Also inject faults into the experts' *reviews* (a false QA finding), to prove a reviewer can be overruled with evidence and does not hold the squad hostage.
  Pre-requisites: the oracle and expectation QA above, the CONCERN channel and keeper, the habit scores of `docs/METRICS.md` section 4a (raising, routing and resolving are the habits scored), the baseline campaign without injected faults for contrast.

## Repairs as edits, and context kept on disk (owner, 2026-11-15)

A repair is a refinement, not a restart. Today the planner (a) asks for the COMPLETE corrected plan (about 3500 tokens, 12 to 20 minutes at 3 to 5 tokens a second) and (b) drops a long refused reply and re-asks from scratch, because a 8192-token slot cannot hold prompt, plan, failures and a second plan. Both are workarounds for a small window, not a design.

- Keep the plan as a file; show the model the file (numbered) and the failures; accept small EDIT transactions as the developers already do. A repair then costs hundreds of tokens, and the model keeps what was right.
- Mechanical mends first (`mendPlan`, `repairJson`): no model call at all.
- Persist context: llama-server can save and restore a slot's KV cache to disk (`--slot-save-path`, `/slots/{id}?action=save|restore`). Not yet checked against our build. First measurement: save a slot after the planner's prompt, restore it, time the prompt processing saved, and record the memory and disk cost per slot. If it pays, a worker resumes its item without re-reading the card, and paused work survives a restart within a limited number of saved slots.

Done 2026-11-15: the planner's repair turn is now an edit. The first reply becomes a document (`plan.json` laid out one field per line, plus one file per scaffold file, kept under `planner/doc/turn-N/`); a repair turn shows the numbered document and the problems only (no carried history, so no restart hack), caps the reply at 2000 tokens and applies the reply as canvas transactions (`applyAsTransactions`, all or nothing). A reply that is a whole new plan replaces the document; an edit that does not apply leaves it unchanged and the problems stand. Block 1 has four repair cases (`tests/data/replies/repair-*.case`). Not yet: the same for developer and tester repairs outside the canvas executor, slot save and restore, and proving-ground cases for resuming an interrupted item from its persisted document. More cases come in a later plan pass over the V-model (owner: tests can be added iteratively).

## Portal access (owner, 2026-11-15)

- Local: `sub0squad portal` serves http://127.0.0.1:8780/ read-only and works beside a running daemon (seen showing w-000012 live). `sub0squad up --no-portal` leaves it off, so the portal was not up until started by hand. TASK: make `up` start the portal by default in the scripts (`scripts/start-campaign.sh` runs the engine and the campaign service only) and print its address.
- Remote (later): ngrok or similar for the portal. Needs the owner (an install, an account and a token; STOP AND ASK) and a design first: the portal is read-only and unauthenticated today, so a public tunnel needs authentication, a read-only guarantee, and no transcripts that carry the owner's text unless chosen. Listed with the opt-in LAN portal question.

## Plan format and discipline boundaries (owner, 2026-11-15)

Design and prior art are in `docs/PLAN_FORMAT.md`. TASKS, in order: (1) measure the format-only failure share on the existing planner records; (2) prose plan then per-item format calls, in parallel, with edit repair, against the single-reply planner on the same seeds; (3) pass a grammar or schema to llama-server for the format step only, and check content quality does not fall; (4) a schema-guided tolerant parser in place of one mend per slip.

## Portal: show the planning layers and this week's features (owner, 2026-11-16)

The portal's work graph starts at the plan's items; a clean-slate workflow's PLANNING (the longest and least reliable stage so far) is invisible, and so are the other new things. TASKS:
- A planning node (and, later, the layers of `docs/PLAN_FORMAT.md`: plan prose, plan form, work prose, work source) ahead of the items, with its state, turns used, and a transcript like any node: `planner/live.txt` (streaming), `planner/prose.md`, `planner/turn-N.txt`, `planner/conversation.md`.
- Per planner turn: the problems that were raised, the mends applied (`mendPlan`, `repairJson`: today only on stderr, so also write them into `planner.json`), whether the turn was an edit or a whole plan, and the document as it stood (`planner/doc/turn-N/`) with the diff between turns.
- The plan-lint scalars (slot fill, speedup, backfill rate, warnings) shown beside the plan; the dispute edges (`reviseTests`: developer to tester), the gate exit class (near pass, stuck, one failing test), and the new policy rung (one failing test goes back to its tester) in the decision log view.
- The supplied-plan path (`plan.supplied`) and the planner's retries (the plan stage now retries twice): a run's planner attempts as separate nodes.
- Mark which work was model-built versus mended mechanically, and show the grade (validation grader output) when one exists beside a finished run.

## A/B on prompt wording in the proving grounds (owner, 2026-11-16)

Wording is a variable like any other, and the wording of the system prompts has never been measured. The planner's line "The workers are small local models: each sees ONLY its own work card, so every card must be exact and short" is the first candidate. Owner's reframing (isolated specialists, self-contained, atomic, not "weak" or "short"), as variants to run against it:

- A (today): "The workers are small local models: each sees ONLY its own work card, so every card must be exact and short."
- B (focused, architectural): "Each worker operates as a dedicated specialist executing a single, self-contained work card; make every card atomic, precise, and fully self-sufficient."
- C (task-oriented): "Workers execute in strict isolation with visibility limited to their assigned card, so scope each card to a distinct, self-contained unit of work with exact specifications."
- D (team metaphor): "Treat each worker as a focused specialist working in isolation on a single assignment: define every work card with surgical precision and zero reliance on outside context."
- E (punchy): "Workers are isolated specialists that see only their assigned card; make every card self-contained, atomic, and exact."

Method: prompts as DATA (a registry of named variants the stage reads, so a variant is not a rebuild); the same seeds and the same engine for every variant; one block with a fixture and a judge per prompt role (planner, tester, developer, fork skeleton, repair turns). Metrics: valid-plan rate and turns to valid, reply tokens, items per plan and slot fill (the lint), the share of cards that pass the plan checks first time, and for the workers the attempts to accepted and the first-try pass rate. Hypotheses to test, not assume: "atomic" may raise the number of items (more, smaller cards: good for slots, bad for reply size and cut-offs); removing "short" may lengthen replies and cause more cut-offs; "isolated specialists" may change how much the model invents beyond the card. The same experiment applies to the other wording that shapes behaviour (the over-allocation paragraph, the examples rule, the edit syntax text, the repair messages). Needs the ordinary rules: one CPU-heavy job at a time, enough seeds per variant (at least 10) before any claim, and the results recorded with the build and the variant name.

Idea, not adopted (2026-11-16): when ONE test fails and a repair left the developer's files unchanged, send that test back to its tester BEFORE the `think` rung (the wrong-literal case in w-000013 and w-000020 cost one extra attempt of thinking). It needs a distinguishing signal: with no oracle, a right test and a wrong test look the same, and `test_nearpass` (right tests, a developer stuck on one) pins the current order. A cheaper discriminator is an independent check of the literal (the expectation QA step above).

Done 2026-11-16 (first slice of the portal task above): a Planning card (`#planning` in `web/portal.html`, `renderPlanning` in `web/portal.js`): request, plan prose, plan form (turns, conversation, live watch), plan, items; a row per planner turn (how the reply was taken, the problems raised, links to the reply and to the document it left); the mechanical mends; the plan-lint text; the planner's re-runs. `planner.json` now records `kinds`, `mends`, `prose`, `slots` and, for a supplied plan, `supplied`. `?wf=ID` shows an earlier workflow. Covered by `tests/portal_page_test.js`. Not yet: planning as a node in the graph, the dispute edges and gate classes, the model-built versus mended marker, the grade beside a finished run.
