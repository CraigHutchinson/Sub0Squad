# Plan, act, iterate per work item; delegation; the work graph and its information graph

Status: DESIGN, 2026-11-10. Owner direction after the first real runs of the calculator sandbox. Nothing here is built yet except where marked.

## 1. Why (evidence from the real runs)

- The `engine` item (one file, a lexer, parser, evaluator and function table) was too big for the model's per-call budget and speed. The first
  serial run reached 64 of 67 tests, regressed on a rewrite, and the ladder then decomposed it only *after* two failures, seven items and 85k
  tokens later. The decision to divide came from failure, not from looking at the size of the task.
- Concurrency (engine-slot leases, built and pushed) cannot help while the plan is one serial item; the modular plan made four items
  parallel, and then **memory** became the limit (section 5).
- A person (or this session) wrote the better plan by hand. The squad should be able to make that call itself.

## 2. Plan, act, iterate inside every work item

Each item runs a small loop instead of one blind generation:

1. **Plan (cheap, no code).** One short call returns a plan record: the units of work it sees (functions or modules with signatures), a size
   estimate in completion tokens, the risks, and a verdict `implement | split | delegate`. The harness also computes its own size estimate from the
   card (contract length, number of tests, files) and the engine's measured decode speed, so a verdict is checked against data, not trusted.
2. **Act.** `implement` runs the existing generate and repair turns. `split` and `delegate` do not generate the whole item: they create
   children (section 3) and the item becomes a *coordinator* that waits for them.
3. **Iterate.** After a failed attempt the loop measures again: size of the best code so far, failing fraction, tokens spent. The verdict
   can change (a near pass repairs; a stuck, large item splits; a stuck, small item goes to the tester dispute path). This replaces the fixed
   ladder order with a decision on measured state; the ladder stays as the guard-rail ceiling (attempt budget).

Rule of thumb for the size check: if the estimated completion exceeds a fraction of the card's token budget, or exceeds N minutes at the
engine's measured speed with a safety margin, the verdict must be `split`. The plan record, the estimate and the observed cost are written to
the ledger so the estimate can be calibrated.

## 3. Strong delegation rights

A worker may do more than answer. With a reply in a structured plan format it can:

- **write a skeleton**: the module with exported signatures, types and `TODO` bodies that throw, accepted by the harness as a real artifact;
- **delegate a function**: create a child item for one signature (`name`, `signature`, `contract`, `tests it must pass`), with the skeleton as the
  child's included file; the child can only touch its own declared file or region;
- **rearchitect and respond back**: a child (or the integrator) can return a *proposal* against the parent's contract ("this signature cannot
  be met; change it to X"); the parent re-plans, which counts against the attempt budget and is recorded, like the test dispute path.
- **sibling coordination** only through the graph (accepted artifacts and interfaces), never by editing each other's files.

Limits stay in the daemon: depth, total items, file allowlists per child, cards under the token budget, and the owner's quotas. Delegation is
data validated by the daemon, never commands (the same rule as plans today).

## 4. Two graphs

1. **The work graph (the process).** The DAG of items with parent and child edges (an item is a coordinator over children), dependency edges,
   owners (personas and tiers), states and decisions. Today: `plan-2` plus `amended` fragments (depth 2), staged pipelines.
2. **The information graph (the product and its knowledge).** Nodes are *artifacts and facts attached to work*: the contracts and signatures,
   accepted files, tests and their names, gate results, plan records and estimates, disputes, decisions with their inputs, metrics, and
   rationale text. Edges say *what was derived from what* (this function satisfies that signature; this failure disputed that test; this card
   was built from those artifacts). It is the shareable, inspectable record: the portal shows it, another squad or person can read a subgraph,
   and a worker's card is a *query* over it (the included files and notes are today a hand-built slice of it).

Both graphs live in the journal as typed records (offline readable, level-triggered, restart safe). The information graph needs one new record
family (`artifact-1`, `edge-1`) and a content store keyed by hash; the work graph already has its records. Sharing and collaboration then mean
exporting a subgraph with its provenance, and importing one as read-only context or as a delegated sub-project.

## 5. Capacity is memory as well as slots (measured 2026-11-10)

With four concurrent engine requests the Qwen3.6 snap server grew to 25.4 GB resident on a 29.6 GiB host and the machine swapped (8 GB swap
fully used, decode 0.6 tok/s instead of 7, a 55 minute run that did little). Slot count alone is therefore the wrong cap. The engine profile
needs a *memory cost per concurrent sequence* (KV cache per token times the context the prompts reach) and the lease must be limited by
`min(slots, floor((available - weights - reserve) / kvPerSequence))`, measured or declared, and checked by the same explicit guard used for
`submit --memory-mib`. Until then use `slots=2` for this snap and keep prompts short. (Open: how much of the growth is KV cache and how much is
prompt-processing buffers; the unified 262144-token KV of the snap is not ours to size, which is the case for our own `llama-server` start.)

## 6. Build order

1. Plan record and the size verdict for one item (harness estimate plus a short model call), `split` creating children through the existing
   `amended` path; ledger fields for estimate versus actual.
2. Skeleton and single-signature delegation (child item with a declared region); validator rules.
3. Memory-aware lease: engine profile gets `kvMiBPerSeq` and `weightsMiB`; guard before a lease is granted; status shows why a job waits.
4. Information graph records and the portal view; export of a subgraph.
5. Replace the fixed ladder order with decisions on measured state (keep the budget).

## 7. The V: descent, ascent and oscillation (owner direction, 2026-11-10)

Work does not only divide downward; it is validated upward. The graph is traversed as a **V**, and the traversal repeats until every layer is satisfied.

- **Descent (decompose).** The root item (the system or helicopter view) is planned and divided into classes or modules, which divide into
  functions; each level fixes the *interfaces* its children must meet (signatures, contracts) and the *tests* that define done at that level.
- **Leaf work.** Functions are implemented and gated against their own tests (what runs today).
- **Ascent (integrate and review).** Every node that has children gets a second job after all its children are accepted: an **integration
  gate** (its own level's tests run against the children together, plus rule checks: interfaces conform, no file outside the allowlist) and a
  **review** (a model or rules look at the combined result against the node's card). The ascent verdict is `satisfied` or `send down`.
- **Oscillation.** `send down` names the children (or new children) to rework and carries the integration failure as their feedback; those
  children re-enter descent, and when they are accepted the node integrates again. If a node cannot be satisfied because a child's contract
  is wrong, the verdict is `send up` (a proposal against the parent's interface), the parent re-plans, and the descent restarts below it.
  The process oscillates between levels until a fixed point: every node `satisfied` and the root integration passes.
- **Termination and cost control.** Each node has a round-trip budget (default 3 down-and-up cycles), the workflow has the global attempt and
  token budgets, and the owner's quotas apply; running out raises the problem one level (rearchitect and respond back) or fails the node with
  the evidence. Oscillation without progress is detected the way stagnation is today (same failing set twice).
- **State per node** (all journal records, so level-triggered and restart safe): `planned → dividing → waiting(children) → integrating →
  reviewing → satisfied | sentDown(n) | sentUp`, with the round-trip count, the verdicts and the feedback attached.

Mapping to what exists: the tester-then-developer pair is the leaf V (test-first down, gate up); `decompose` and `Plan::amended` are the
descent; the integrator is the root ascent; the missing pieces are per-node integration gates and reviews, the `send down` and `send up`
verdicts, and the round-trip budget.

## 8. Visibility: the portal (v0 built)

`sub0squad portal [--port 8780]` serves a read-only page on 127.0.0.1 (GET only) with `/api/state` JSON. It reads the same sources as
`sub0squad status`, so it works with the daemon down (from the journal). Today it shows the work graph by level and dependency, per-node
status, attempts, engine, tokens in and out and time, the workflow KPIs (work ratio, rework, decode speed), the latest decisions and the
daemon's jobs. Next: draw the V itself (descent on the left, ascent on the right, the current oscillation highlighted), per-node history of
verdicts, a node detail pane (card, last prompt, gate output), engine and memory gauges, and later the settings and quotas pages (M3).

## 9. Determinism levers (measured 2026-11-10 on the Qwen3.6-35B-A3B server)

llama.cpp's server accepts a per-request `seed` (default random) and `cache_prompt`. With the same prompt, sampling parameters and seed:

| Case | Result |
|---|---|
| 60-token reply, same seed, repeated, with and without prompt cache | identical every time; a different seed differs |
| same, while three other requests were being decoded | identical |
| 300-token reply, two identical requests at the same time, plus other load | **diverged** (the pair differed; a later run alone matched one of them) |

So a seed makes short replies reproducible even under load, but long replies are only reproducible when the **batch composition is the same**,
because batched matrix products change floating-point reduction order and a near-tie token flips. Two modes follow:

- **Deterministic (framework work):** seed per request derived from (run seed, item, attempt, turn), `cache_prompt` off, and the engine lease
  capped at 1 so requests are never batched with each other. Same run seed, same build and thread count: same outcome; change one dial
  (card text, budget, policy) and the difference is the dial.
- **Throughput (real runs):** seeds still recorded, many slots; outcomes are samples, so judge a change by several seeds, not one run.
The run seed, the per-request seed, the server build and the thread count belong in the ledger and the portal so a result can be replayed.

## 10. Keys, scoped history and the analysable record (owner direction, 2026-11-10)

Every node accumulates history at its layer, and a review on the way back up or down may concern only a **part** of a node's work (a
finer inner box). That needs one robust keying scheme used by the work graph, the information graph, the portal and the analysis tools.

**Keys (stable, hierarchical, never reused):**

| Key | Form | Meaning |
|---|---|---|
| node | `w-000001/calc.parse.expr` | an item; the dotted path is its position in the division tree |
| visit | `<node>@k2` | the k-th pass of the V through that node (descent is k1; each send-down or send-up that revisits it increments k) |
| step | `<node>@k2/integrate#a1.t0` | phase (`plan`, `implement`, `integrate`, `review`), attempt, turn |
| scope | `<node>@k2:src/calc.js#parsePrimary` or `...:L40-88` | the sub-region of a node's artifact a step concerns; carries the artifact hash it was cut from, so a verdict on "this box" stays attached to the exact text it judged even after the file changes |
| artifact | `sha256:<hex>` | content-addressed blob (contract, tests, code, prompt, reply, gate output, review): immutable, shared, deduplicated |
| fact | `<node>@k2/…` + kind | a verdict, estimate, seed or metric attached to a step |

**Edges (typed, directed, each carrying the key of the step that created it):** `dividesInto`, `dependsOn`, `satisfies` (code to the
signature in the contract), `testedBy`, `derivedFrom` (artifact to the artifacts it was built from, including the exact context included in a
card), `reviews(scope)`, `sentDown(scope, feedback)`, `sentUp(proposal)`, `disputes`, `supersedes` (a rewrite replaces an older version).
A card for a worker is a query over this graph (a node's contract, its siblings' interfaces, the scoped feedback addressed to it), which is
what makes a scoped send-down cheap: the child receives only its box and the verdict about it.

**Live view versus whole history.** The portal projects the same store two ways. The *live* view is the active frontier: running steps, the
current V position (which layer, descending or ascending), the oscillation count and the nodes waiting on children, with finished subtrees
collapsed to one line (state, rounds, tokens, last verdict). Any collapsed node expands to its full history, and a time slider replays the graph
as it stood at any step, so the live page stays small yet can summarise the entire project to the current point.

**Representation and tooling.** Three layers, each with one job:

1. `journal-1` (exists): the small, authoritative, replayable state (jobs, workflows, decisions). Level-triggered recovery depends on it, so it
   stays the source of truth.
2. The **history store** (new): the keyed records and edges above as typed NDJSON segments plus the content-addressed blob directory, written
   by the daemon when steps end. Offline readable, append-only, versioned (`history-1`).
3. The **trace stream** (new, high rate): per-request and per-turn events (prompt and completion tokens, timings, decode speed, seed, engine
   and server build, cache hit, slot) as typed records. This is where **Sub0Log** fits: it is a structured diagnostic stream with typed
   records, no producer formatting, no allocation on the log path and a log that survives a hard kill, with `sub0log-cat --format jsonl` as the
   export. Proposed use: a Sub0Log subsystem per squad component (daemon, agent, gate, engine client) carrying the step key in each record, so a
   trace line joins to the history store by key. It does not replace the journal (replay semantics and ordering guarantees belong there).
   A spike should check that Sub0Log can be linked into the shim and agent processes (header-only) and that its jsonl carries our keys.

**Analysis.** `sub0squad export <workflow> [--format ndjson]` writes the keyed records, edges and a manifest (plan, settings, seeds, builds);
`sub0squad compare <a> <b>` diffs two runs by key (which nodes diverged first, tokens per node, verdict differences), which is how a change to
a card, a budget or the policy is judged. With a run seed (section 9) a deterministic replay isolates the changed dial. The same files feed a
spreadsheet, a notebook or the portal's history tab; policies can be replayed offline against recorded contexts, as the journal already allows.

**Build order addition:** (a) the key types and `history-1` writer, populated from what the daemon already knows (jobs, decisions, ledger),
so the portal and `export` work on existing runs; (b) scope and visit keys when per-node integration and review exist (section 7);
(c) the Sub0Log trace spike; (d) `compare`.

## 11. Orchestration at the top, a project-manager pass, and memory safety (owner direction, 2026-11-10)

**Front-load parallelism.** The first calculator plan put the whole page behind the engine (`eval-tests -> engine -> ui`), so the last and
a large stage ran alone and a failure there (the page, decomposed after two failures) cost the most. The orchestrating level must shape a plan
for width and small packages in its early phases: independent tester items together, then many small developer items, with a dependency only
where an artifact is really needed. The page now shows how: inject the dependency (`mount(document, evaluate)`) so the page script is graded
against a fake and the markup against file checks, and neither waits for the parser.
`sub0squad plan-lint FILE` (built) audits a plan before it runs: levels, width, critical path in estimated tokens and rough minutes, and
warnings for a large last stage, a narrow plan and multi-file items. Measured on the two plans: the original (widest 2, critical path 76 % of
the work) against `docs/calculator-web-parallel.plan.json` (widest 5, critical path 31 %, about 14 minutes against 44 run serially at one
request). Estimates are rough and complexity based; the lint exists to make shape visible, not to predict.

**A project-manager pass, run when there is capacity.** A recurring audit job that the daemon schedules when engine slots are idle and the
ready queue is thin, or after a node divides or finishes:

- Inputs: the work graph, node histories, the ledger and the live metrics (idle slots, tokens per node, repeated failures, decisions).
- Outputs, as *proposals* the daemon validates like amended plans: add items that have become visible (a child's discovered sub-task), pull
  forward work that no longer needs its dependency, split a node whose estimate or history says it is too big, merge trivial ones, tighten
  or relax budgets, reorder the ready queue by critical path.
- Limits: only pending nodes change (accepted work is untouchable unless reopened by a verdict), every change is journaled with its reasons
  and shown in the portal, and anything beyond the depth, item or token budgets goes to the owner. It runs only while capacity is free and
  yields to real work.
- First version: rule based (plan-lint rules applied to the live graph, repeated-failure and idle-slot triggers); a model-backed version later.

**Memory safety (incident 2026-11-10).** Memory pressure killed the owner's Claude session while the squad ran: the kernel's out-of-memory
killer fired at 12:41 and took the 35B `llama-server` (18 GB, at that time `oom_score_adj` 200 inherited from the desktop app), and the
session died as well. A Firefox content process held 21.6 GB at the time; the portal page had been open in Firefox, and the page was not
ruled out (below). The squad must shed its own processes before anything else is lost:

1. **OOM priority (built).** The shim sets `oom_score_adj` for each job's process group: 600 for jobs, 900 for servers (raising needs no
   privilege). The kernel's last resort now prefers squad processes over the owner's session and the daemon.
2. **Shed before the kernel does (built).** The daemon watches `MemAvailable` while work exists (every 5 s, every 1 s within twice the
   reserve; fully idle it still blocks with no wake-ups). Below `--mem-shed-mib` (default 1536) it cancels the newest plain job, one per
   check, and admits nothing new until memory recovers; servers are shed last, only when available memory falls below half the reserve. A
   shed job records `shed: memory pressure (...)`. Tested with an injected memory figure.
3. **Still to do:** a shed job currently counts as a failed attempt for the policy (it should be requeued without costing an attempt);
   memory-aware engine leases (KV cost per slot); the shed decision should prefer low-value work (a long-running near-done job is worth more
   than a new one) and consult the node history.

**The portal and Firefox.** The page cannot be proved innocent in Firefox, and a stalled server is the one mechanism found: the page used to
start a request every second whether or not the previous one had returned, and the server answered one request at a time while waiting on
the daemon, which is slow when the host is swapping. Changes (built): the HTTP loop serves a cached state refreshed by the sampler thread,
the page keeps one request in flight per endpoint with a 5 s timeout, does nothing while the tab is hidden, and repaints only when its markup
changed. Measured in the built-in browser (Chromium): 300 forced refreshes left the DOM at 359 nodes and the JS heap at a few MB; with the
server stalled, 100 poll attempts started 2 requests. A 21.6 GB process is far beyond what this page's data (a few hundred KB) can explain,
so the Firefox process may be another tab; the way to settle it is `about:processes` in Firefox, which shows the page for each process.

## 12. Workflow packages, deliverables and the model badge (portal, 2026-11-10)

**Packages.** Every workflow has a package the portal can open: the request (`taskText`), the plan and its reasons, the engines and seed, the
deliverables, and every file the run wrote (prompts, replies, gate outputs, per-attempt outputs, accepted outputs, the merged project), shown
grouped per node, and per node in its drawer. Files open in a viewer (read only, capped at 400 KB, confined to the workflow's sandbox: relative
paths, no `..`, symbolic links resolved and checked). `/api/files?wf=` lists, `/api/file?wf=&p=` reads.

**Deliverables are a manifest, not a guess.** The integrator writes `deliverables.json` (`deliverables-1`: id, kind, title, path, how to open
it, how to run it) and `report.md` next to the plan; the portal shows them first and links the page. The web harness decides by rule (the page,
the report, the source tree). For C++ and other outputs the decision is an agent's: a **reporter** step after integration looks at the merged
result and the plan and writes the manifest and the final report (binaries and how to run them, libraries and their headers, documents, test
and benchmark reports, what was not done), with the harness supplying the default rules and the daemon validating that every path exists and
stays inside the workflow.

**Generated sites are another origin.** A page the models wrote is served from the portal's next port (default 8781 for the portal on 8780): a
different origin, so it cannot read the portal's API (which sends no CORS headers), and its responses carry `default-src` limited to itself and
`connect-src 'none'`. The portal port refuses `/site/` and the site port serves nothing but `/site/<workflow>/...`. Checked in the browser: the
calculator ran on port 8783, 2+3 gave 5, and a `fetch` to the portal's API from the page failed. (An attempt with `Content-Security-Policy:
sandbox` was dropped: the browser pane blocked the page's own sub-resources from the opaque origin.)

**Which model.** Each engine's `/props` and `/v1/models` give the file, quantisation, parameters, size, training context, build and slots. The portal
shows a short badge (`9B Q4_K_M`, `35B-A3B Q4_K_M`) on every node that ran, in the nodes table, the engine panel and the KPI row, with a tooltip of
the details and a click-through drawer; `frontier stub` and `mock` have their own badges so a multi-model workflow reads at a glance. A node's
badge comes from the engine profile that served its attempts (the ledger records the name), or from the engine of its running job.

**Rates.** `/slots` counts whole tokens, so a one-second difference at 5 to 8 tok/s reads as a whole number. The rate is now taken over the last
five samples (a resolution of 0.2 tok/s).

**Refresh model (portal).** The page used to rebuild whole regions every second, which reset scroll positions (the open file viewer jumped to the
top), collapsed open `<details>`, lost selections and restarted animations. It is now retained-mode: every region is a keyed list of elements
that are patched in place (`reconcile`) and every piece of markup is compared with what is already shown before it is written (`setInner`), so
only what changed is touched. Events are delegated once instead of re-attached. The detail drawer is three stacked regions (what the node is,
what is live, the lists that change with files, jobs and decisions) and the file viewer is never rewritten by a refresh. Measured on the live 9B
run over 6 s: no mutation in the drawer, the viewer scrolled to 200 px stayed at 200 px in the same element, an open group of files stayed open
when its list grew, the page scrolled to 400 px stayed there, and only the clock, the sampled tiles, the engine rows, the graph nodes that
changed and the jobs table were touched (the work-package card, the nodes table and the decisions table saw no mutation at all).

## 13. The V, first build (2026-11-10)

Built (rule based, web harness): when an item is divided, its parent **stays in the plan as a coordinator** (`persona: integrator`, the same id so
whatever depended on it still does, no files of its own, its parts as dependencies, its original tests as its gate). It has no agent step.

- **Ascent.** When its parts are accepted, a gate job runs the coordinator's tests over the merged outputs of everything below it (a round per
  gate job). Pass: the node is accepted and its dependents proceed.
- **Send down.** A failing round is recorded as `sendDown` and the daemon picks the part the failure points at (a developer part, at any depth,
  whose file names appear in the failure output; otherwise the most complex part), writes the failure as that part's feedback, and voids its
  acceptance (the same mechanism as a test dispute). The part is redone (its next attempt starts with the feedback), is gated by its own tests, and
  the coordinator integrates again.
- **Oscillation budget.** Three send-downs per coordinator; the fourth failure fails the item with the evidence (the bubble-up to a
  rearchitect of the parent is not built).
- **Scoped parts.** A division is refused when a developer part is graded by the parent's own test file while another developer part exists: that
  part could only pass when every other part's code did too (the page item that failed in the first modular run). Each part needs its own tests.
- Not built yet: attribution by a model (today by file names), review (a model reading the combined result against the card), send-up proposals
  against a parent's interface, per-level integration for plans written by hand without division, the visual V in the portal, and the keyed
  history of section 10.

Tests: `Plan::amended` keeps the coordinator, keeps dependents on the parent id and refuses the unscoped part; the engine integrates, sends down
once, redoes the part and integrates again (two coordinator gates, one send-down, the part's agent run twice).

## 14. Intent, requests and the layers of orchestration (owner direction, 2026-11-10)

A project starts as words ("write a calculator web app with scientific functions") and its plan, requirements and decisions *evolve* from them
while it runs: work raises questions, later requirements and clarifications arrive, platform and runtime facts surface and have to propagate.
That needs more than one plane of control. The layers, from the top, each with its own authority, rate of change and record:

| Layer | Question it answers | Owner | Record |
|---|---|---|---|
| Intent | what is wanted, and what changed since | the owner | requirement register (REQ-n: the prompt, then each requirement or clarification, with scope and who/when) |
| Architecture | what shape satisfies it: modules, interfaces, platform, constraints | architect (a frontier model or the owner; a model later) | decisions (ADR-style, with the requirement they serve), interface contracts in the cards |
| Plan / orchestration | in what order, how wide, how small, with which engines | project manager (rule based now, a model when idle capacity exists) | the work graph, the plan-lint view, budgets |
| Execution | do the work and verify it | the squad (the V) | jobs, ledger, node histories |
| Governance | who may decide what, and what is held until then | the daemon, from policy and the owner's switches | the **requests inbox** and its decisions |

**Requests are the connective tissue.** Anything below can raise a request to the layer above (or the owner): `question` (a decision a node
cannot make), `clarification`, `requirement` (a change or addition), `blocker` (a platform or runtime fact), `proposal` (a contract change from a
part to its parent: the V's send-up), `escalation` (the ladder is spent). A request has a scope (the nodes it concerns), an owner by kind, and
may be **blocking**: it holds the nodes in its scope (all nodes when the scope is empty) until it is answered; nothing else stops. An answer
is free text plus, for an escalation, a resolution (retry or stop); the text becomes guidance for the nodes in scope (the feedback of their
next attempt). A requirement or clarification also voids the nodes that were already accepted, so they redo their work under the new words.

**Built (2026-11-10):** the inbox end to end. Journal records `workflowRequestRaised` and `workflowRequestAnswered`; the engine holds nodes behind
blocking requests and will not integrate while one is open; with `--ask` an item the ladder cannot finish is deferred to the owner instead
of failing the workflow (a blocking escalation; the owner answers retry-with-guidance or stop); `sub0squad request list|raise|answer`; the
portal's Intent view (REQ-1 and later requirements, the assumptions made when planning, the open inbox with answer forms, the decisions log,
and a form that adds a requirement, clarification or question to a running project); nodes held by a request are drawn dashed with the
request's id. The portal's two POST routes need the per-run token embedded in the page and a same-origin request; a generated site is on
another origin and has neither. Tests: a blocking request holds only its scope and releases on answer; deferral then retry or stop; the CLI
round trip, including refusals (unknown kind, unknown request, already answered, finished workflow).

**Not built yet** (the order I would take them): (1) agents raising requests themselves (a reply block asking a question or reporting a blocker,
instead of guessing); (2) impact analysis: a requirement change finds the affected nodes and their dependents from the graph and the keyed history
instead of relying on the scope the owner ticks; (3) the architect view: proposals from parts against interfaces, the decision log with the
requirements each serves, interface contracts as first-class nodes; (4) the project-manager pass (section 11) filing proposals into the
same inbox when capacity is free, so its suggestions are visible and answerable; (5) quotas and the frontier switch as governance requests
(the owner approves a frontier escalation per item instead of a global switch); (6) the V (section 13) raising `proposal` requests for
send-up and `escalation` when its round-trip budget is spent.

## 15. Parallelism inside a node: fork or stay in one context (owner question, 2026-11-10)

When a node's plan phase finds N separate areas (a lexer, a function table and a parser inside one engine item), there are four ways to run
them. They are different things and want different triggers:

| Mode | What it is | Good when | Costs |
|---|---|---|---|
| **Serial in one context** | one conversation, area by area, a gate or test run between areas (the repair-turn loop, extended from "fix failures" to "next area") | the areas share a lot of context or one interface, the total fits one attempt, capacity is short | wall time is the sum of the areas at one request's decode speed; one area's failure stalls the rest; the context grows |
| **Partition fork** | N sub-nodes with disjoint files, each with its own tests, under a coordinator that integrates them (the V, already built) | the areas can be specified by exact signatures, each is big enough to matter (about 600 tokens or more), engine slots and memory are free | the interfaces must be fixed up front; each fork repeats the shared prefix (prefill) and the integration is a step of its own |
| **Redundant fork** | N attempts at the *same* area, different derived seeds, first to pass its gate wins and the rest are cancelled | slots would otherwise be idle and a retry is the expected cost (a weak model, a flaky area) | tokens for the losers; only free capacity should pay it |
| **Pipeline** | producer and checker overlap (tests are written while another area is being implemented) | the test-first handoff, already used | none to speak of |

**The decision is data, not taste.** The plan phase returns the areas as a structure (name, exported signatures, files, estimated tokens, which
areas each uses), and the harness chooses by rule: the estimated time serial (the sum of the areas' tokens over one request's measured decode
speed) against forked (the largest area's tokens over the per-request speed at that many requests in flight), with the engine's idle slots and
free memory as constraints and the node's coupling as a veto. The speed curve is measured on this host: aggregate decode over concurrent
requests was 6.95 tok/s at 1 and 12.0 at 4 for the 35B-A3B (so a 4-way fork speeds up about 1.7x), 1.38 to 9.17 at 16 for the dense 27B
(6.6x), 5.2 to 19.9 at 8 for the 9B (3.8x). A fork that gains less than about 1.3x, or that needs more slots than are free, stays serial.

**Dynamic, not decided once.** Start serial when the node is small. Fork when the plan phase's estimate says so, when the first serial attempt
shows the node is hard (a near pass that stalls, a repeated failure), or when the project-manager pass sees idle slots and a ready queue that
is too thin (section 11). Take back to serial when memory pressure is on (section 11): forks are the first thing to shed.

**One cost worth cutting later.** Forks that share a long prefix (the contract, the included tests) repeat its prefill. llama.cpp can save and
restore a slot's cache (`/slots/{id}?action=save|restore` with `--slot-save-path`) and reuse a prefix (`--cache-reuse`); prefilling the shared
context once and restoring it into N slots would remove the repeat. Not checked on our build yet.

**Where it lands in the build.** (1) The plan phase emits areas with interface skeletons (today it is a size estimate and, on failure, a
division written by the architect job). (2) A partition fork is already a division under a coordinator; the change is that the *node itself*
proposes it before generating anything, validated by the same rules (scoped tests, covered files). (3) Serial-in-context gains area turns: after
an area the agent runs that area's tests, then continues in the same conversation. (4) Redundant forks use the lease: they hold only slots no
other ready item wants. All four are recorded in the ledger by mode so each can be judged on tokens, time and success.

## 16. Ethos: craft on the small, separation and interactions on the large

Two kinds of work, two stances, one graph. A **leaf** is small and focused: its worker is the expert on that one thing and does it as well as it
can be done (a tight card, exact interfaces, its own tests, the repair loop). A **larger** piece is not done better by trying harder: the
work is to divide it well and to make sure that the designed separation and the interactions hold. In the V that is the descent (clean
boundaries, exact signatures, tests per part, files covered) and the ascent (the coordinator's integration gate, scoped send-down, send-up
proposals when a boundary was wrong). The gates are how a promise made at design time is checked at build time: a part passes its own tests,
the whole passes the tests of the level above. Workers say it in one line each: "own one focused piece, be the expert, keep to the interfaces";
the architect says "your job is the separation". The README states the same for people: many small minds, one whole solution.

## 17. First responders and the harness-expert (owner direction, 2026-11-10)

An escalation is rarely a *design* decision. Often it is a tooling or harness fault (a fatal issue) or something a small, well-informed role can
settle (an overseer). The owner should be asked last, and asked with the question already framed. So an escalation now passes through two roles
before it reaches a person:

| Role | Does | Output |
|---|---|---|
| **Overseer** (built, rule based) | analyses the failure evidence and classes the request: `guidance` (a mechanical cause with advice: a test file that crashes while loading, tests that pass against the stub, files outside the allowlist, a malformed reply, a syntax error), `fatal` (the environment: engine unreachable, tool missing), or `owner` (nothing mechanical found: the same tests fail the same way, a real decision) | a verdict and a diagnosis; guidance is answered at once, at most twice per item, as the node's feedback; fatal and owner stay open with the analysis attached and the card marked |
| **Harness-expert** (built as the briefing; a model later) | knows how the harness behaves (scaffold and stubs, the gates and their thresholds, the reply format, the sizing and coordinator rules) and **collates** the situation, the node's card, those facts, the evidence, what the ladder already tried and the overseer's analysis into one briefing | `items/<node>/briefing-<request>.md`, shown in the portal as "Briefing for a decider"; the same text is the prompt for an architect or project-manager model, or for a frontier model reached by a file handoff |

Verified end to end on a governed mock run: the engine escalated, the overseer classed it `owner` ("the same tests failed with the same messages
across attempts"), the briefing was written, and the portal card shows the class, the analysis and the briefing link. The roles are the same family
as the architect (shapes the division), the project manager (idle-capacity pass, section 11) and the V's reviewer: each reads the keyed evidence,
each can raise or answer requests, and each can be a rule today and a model tomorrow behind the same record. The harness-expert is the one that
holds *tool* knowledge, so the others do not each have to rediscover it; its facts text (`webHarnessFacts`) is also what a fresh architect
needs to write a plan that suits the harness.

Next: a model-backed overseer for the `owner` class that proposes options (not only a diagnosis); harness facts per project type (C++ builds
and tests will have their own); the briefing attached to any request, not only escalations (a node's question, a part's send-up proposal); and a
one-click "copy as prompt" in the portal.

## 18. Traceability: what is kept for every attempt and job (2026-11-10)

Question from the owner, after a division of `ui-page` failed: is the conversation, the thinking, persisted for a job or a node, and can it be reached?
What the first real runs actually kept, and what was missing:

- **Kept:** per attempt, `prompt.txt` (the first system and user message), `reply.txt` and `reply-<turn>.txt` (each model answer), `gate.txt`, the files the
  attempt wrote (`out/`) and the accepted outputs; per job, `spec.json`, `events.ndjson`, `exit.json` and the two output streams.
- **Missing:** the *repair* prompts (the feedback the worker was sent between turns), the settings and seed of each call, the model's reasoning text
  (the server returned it or not; the code only counted its characters), the numbers of each call, and the verdict after each turn. Worse, a job's
  printed output was block buffered, so a job that was killed or timed out lost it: the failed `ui-page` division had run its first turn, been rejected for a
  reason that was printed and then lost, and timed out during the correction (an architect job had 30 minutes of wall time for three turns of up to 28 minutes).
  And the portal could open a node's files but not a job's logs.

Now: every llm worker and architect writes `items/<node>/attempt-N/conversation.md`, appended turn by turn and flushed at once: the settings of the call
(engine, model, sampling, request seed), every message sent (the full first prompt, then only what was new each turn), each reply with its tokens, speed,
finish reason and the reasoning text when the server returns it, and the verdict that followed (the validation error, the gate output, "accepted",
"stagnated"). Job output is line buffered. Architect jobs get wall time for all three turns. The portal opens a job's `stdout`, `stderr`, `exit` and
events from the Jobs tables and from a node's drawer (`@jobs/<job>/run-N/<file>`, a fixed list of names under the jobs directory), and shows a job's
note ("wall time budget exceeded") next to its state. The conversation file is in the node's Artifacts list like any other file. Thinking is
off for these workers by default, so reasoning will normally be empty; it is kept when a profile turns thinking on.

Not yet: one place that shows a node's whole history across attempts (the keyed history of section 10), a side-by-side of two attempts, and the
trace of the V's integration rounds and send-downs as a story.
