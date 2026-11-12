# Backlog

Work that is understood but not scheduled. Newest first within a heading; each item says where its design lives. The roadmap ([ROADMAP.md](ROADMAP.md))
holds milestones; this holds the things noticed on the way.

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
- **A `doctor` command** that checks the first-use prerequisites (binary version, engine reachable, memory guard, ports, cgroup limits) and says what to fix.
