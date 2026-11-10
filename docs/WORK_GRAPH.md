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
