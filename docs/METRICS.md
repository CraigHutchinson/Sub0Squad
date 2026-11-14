# Metrics and ways of working: what we measure, why, and how it changes how we build

Status: design, 2026-11-14 (owner direction: KPIs and metrics for success rates and work done against work achieved; a deep and wide look at effective agentic ways of working; the fork
should raise token throughput without needing to cut wall-clock time, because focused sessions buy detail; product quality at optimal token efficiency is the goal; express cost in engine ticks, which
do not move with system load, so that effective concurrency shows up end to end). Nothing here is built except where a row says so. It complements `docs/BLOCK_PROVING_GROUNDS.md` (what is proven)
and `docs/SCENARIOS.md` (capability in isolation) with the question those two cannot answer: **what did a run cost, and what did it buy?**

## 1. Why now: the numbers we show today are wrong for forks

The ledger and `project status` report tokens per workflow ("work vs harness", "work ratio"). Comparing them with the fork executor's own records for the three real C++ runs shows the blind spot:

| run | tokens in the ledger (in + out) | tokens in the fork attempts' own records (skeleton + sections + integration) | fork tokens vs ledger | fork attempts | wall time of those attempts |
|---|---|---|---|---|---|
| run-v9 (35B, fork) | 1,432 | 396,719 | 277x | 6 | 128 min |
| run-v10 | 38,165 | 221,294 | 5.8x | 5 | 124 min |
| run-v11 | 39,662 | 272,518 | 6.9x | 7 | 188 min |

The per-section sessions never reach the ledger. So every efficiency figure for a forked run (work ratio, tokens per accepted item, share of waste) was computed over a fraction of the real spend, and "work
ratio 0.0 %" in those runs is not a finding about the model, it is a hole in the instrument. The first metrics task is therefore **accounting that reconciles with the engine** (section 6), before any KPI is trusted.
The same runs also show what wall-clock time hides: run-v11's first fork attempt reached a near pass (20 of 22 tests) in 2,256 s on a host also running builds and tests; the identical work would take
more or less wall time on another load. Ticks (section 3) do not.

## 2. Principles

1. **The goal is product quality at optimal token efficiency.** Not speed. Wall clock is a property of the host's load on a given day; tokens and ticks are properties of the work.
2. **The fork is for focus, not (only) for speed.** A section session sees one function, a small prompt and one job, so it can be more careful (detail, edge cases) for the same or fewer tokens than one
   long session that has forgotten its own start. It raises token *throughput* (more tokens generated per engine tick, because slots run side by side) and it may or may not shorten wall time. The
   claim to test is "higher quality per token", with ticks as the neutral clock for the end-to-end cost (section 5).
3. **Count at the source, reconcile with the engine.** Every session records its own usage; the engine's counters give the independent total; a reconciliation test fails if they disagree by more than a few percent.
4. **Judge quality independently of the model's own tests.** A model that writes weak tests and passes them has not made good software. Quality is measured by an oracle the model did not write (the reference test suite),
   by static checks and by sanitizers, as well as by the item's own gate.
5. **Paired comparisons, with confidence.** Two ways of working are compared on the same tasks with the same seeds, and a difference is called real only when the intervals do not overlap (the rule `scenario compare` already uses).
6. **Metrics decide things.** A metric that no decision uses is removed. Each one below names the decision it feeds (ladder rung, executor choice, prompt change, model choice, release gate).

## 3. The tick model: cost that does not move with load

llama.cpp's server runs **continuous batching**: each *tick* is one forward pass (one `llama_decode` call) that advances every busy slot by one token (decode) or processes a chunk of prompt (prefill).

* **Work** (what a workflow needs) is a count of tokens: prompt tokens processed, tokens decoded, tokens served from the cache.
* **Decode ticks** are the number of forward passes during decode. With `b` slots busy a tick yields `b` tokens, so `tokens decoded / decode ticks = mean busy slots`, the **concurrency factor** actually achieved. It is 1 for a lone
  session and up to the slot count for a perfectly parallel phase.
* **Prefill tokens** are counted separately (prefill cost grows with tokens, not with ticks).
* A tick costs a roughly constant amount of wall time at low batch (memory-bandwidth bound: the weights are read once per tick whatever the number of sequences) and more as the batch fills; the host's other load scales it.
  So **ticks and prefill tokens are the load-independent measures, and wall time is derived**: `wall ~ decode ticks x seconds per tick(batch, load) + prefill tokens x seconds per prefill token`. Where a wall figure is needed
  it is computed from measured rates, and labelled as an estimate.

Where the counters come from: `llama-server --metrics` exposes `llamacpp:n_decode_total` (forward passes), `llamacpp:n_busy_slots_per_decode` (mean busy slots per pass), `llamacpp:tokens_predicted_total`,
`llamacpp:prompt_tokens_total` and the corresponding seconds. The engine service in use has not been started with `--metrics` (it answers 501), so the first step is one flag on its start line. Per request the server already
returns `timings` (`prompt_n`, `predicted_n`, `cache_n`), which the agent records; the engine-wide counters are sampled by the daemon at workflow start, at each job start and end, and at workflow end, so the ticks a workflow used and the
ticks each phase used are differences of two snapshots. A session's own latency in ticks is its `predicted_n` plus its prefill chunks; the **critical path in ticks** of a workflow is the longest chain of dependent sessions measured that way.

## 4. The KPI hierarchy

Five levels, from the outcome down to the system. Each row: the definition, the decision it feeds.

### 4.1 Outcome (did it work)

| KPI | definition | feeds |
|---|---|---|
| Project success | the workflow's integration passed with no manual step | the release bar of a platform ("zero mends over 3 runs") |
| Items accepted | accepted items / items in the plan, at the end | where to look first when a run fails |
| First-pass rate | items accepted at attempt 1 / items, by persona and platform | prompt and scaffold changes (a rising rate is the clearest sign of a better harness) |
| Attempts per accepted item | mean and the tail (95th percentile) | ladder design; the cost of the long tail |
| Ladder depth | the deepest rung reached per item (retry, mend, sections, reviseTests, rearchitect, think, fail) | which rungs earn their cost (section 7) |
| Manual step-ins | count of catalogue rows needed to finish a run | the self-mending ledger: the target is zero |
| Healed rate | mends applied / mechanical failures seen | where to add mends |

### 4.2 Quality (how good is what was built)

| KPI | definition | feeds |
|---|---|---|
| Oracle pass rate | share of an INDEPENDENT reference suite (tests the model did not write) that the final deliverable passes | the true quality number; compares executors and prompts |
| Own-tests pass rate at acceptance | the item gate's tests passed / total | sanity: should be 100 % at acceptance |
| Test quality | mutation-style: share of seeded defects (the harness's known-bad variants) that the model's tests catch | whether testers are doing their job |
| Static quality | warnings under the strict flags, QA findings (duplication, long functions), sanitizer-clean | the "good code" floor beyond passing tests |
| Dispute rate | tests sent back as wrong / tests written | tester quality; the cost of wrong specifications |
| Defect escape | defects found at integration that a lower level should have caught | where the V is weakest |

### 4.3 Efficiency (tokens: work done against work achieved)

Work **done** is every token the engine processed for the workflow; work **achieved** is what is in the accepted deliverable. The gap is the cost of the way of working.

| KPI | definition | feeds |
|---|---|---|
| Work ratio | accepted deliverable tokens / all tokens (prompt + completion), from the reconciled accounting | headline efficiency |
| Waste breakdown | all tokens split into: accepted work, rejected attempts, repair turns, prompt overhead (system + card), drafts and views shown, reads, skeleton, integration, planning, aborted generation | which waste to attack first |
| Tokens per accepted item / per passing test / per quality point | cost normalised by what was bought | comparing ways of working |
| Cache share | prompt tokens served from the slot's cache / prompt tokens | prompt layout (stable prefixes first) |
| Harness leverage | bytes supplied by the harness (scaffold, stubs, helpers, skeleton) / (those + tokens the model wrote) | the case for deterministic scaffolding |
| Coordination cost | (skeleton + integration + planning tokens) / section tokens | when a fork stops paying (too many tiny sections) |
| Marginal gain | quality gained per 10,000 tokens across successive attempts of one item | the stopping rule: when another retry is not worth it |
| Abort savings | tokens a cancelled stream would have cost (bound minus used) | turn control |

### 4.4 Concurrency and throughput (ticks)

| KPI | definition | feeds |
|---|---|---|
| Concurrency factor | tokens decoded / decode ticks, over a phase and over the whole workflow | whether parallel phases really parallelise |
| Slot utilisation | concurrency factor / slots | when to widen or narrow a fork |
| Tick latency to quality | decode ticks elapsed from workflow start until each accepted item (and the whole project) | the end-to-end time measure, load independent |
| Critical path (ticks) vs total (tokens) | the longest dependent chain of sessions against all work | the ceiling on what more parallelism can buy |
| Parallel efficiency | ideal ticks (total tokens / slots) / actual ticks | how much of the available parallelism the plan uses |
| Focus gain | for the same task, oracle pass rate and tokens per quality point of the forked path against the session path | the fork decision (section 5) |
| Idle-while-pending | ticks with an idle slot while a section was ready | the scheduler |

### 4.5 System (context only, never a target)

Decode and prefill tokens per second per slot, host load, memory available, shed events, time in queue. These explain a wall-clock figure; they are not goals and are not compared across days.

## 4a. Habits and the craft score (owner direction, 2026-11-14)

Mending a compile error is not a failure: it is the normal development loop, and so are the other things a good engineer does: fixing, iterating, delegating, splitting and forking, designing and architecting, testing, QA, suggesting,
improving. Outcome KPIs alone would reward luck and punish honest iteration, and would say nothing about *how* the work was done. So the scorecard also scores the **habits**, because habits are what the harness can teach and
reinforce, and they predict outcomes on tasks we have not run yet.

**Principle: score the habit's effect, not its count.** Activity alone earns nothing (a model that "iterates" ten times on the same mistake is thrashing). A habit scores when it is *present*, *effective* (it changed the outcome) and *efficient* (tokens per
unit of progress). Each is computed from records we already keep or are adding (journal decisions, request records, gate texts, canvas logs, session usage), so the score cannot be gamed by saying the right words.

| habit | observable evidence | effectiveness (did it help) | efficiency |
|---|---|---|---|
| **Fixing and mending** | a gate failure followed by a change that removes its first error; a harness mend; a section retry | errors resolved per turn; the first error actually changes (progress) rather than repeating (thrash) | tokens per resolved error (a mend costs none); identical consecutive replies score zero and are flagged |
| **Iterating** | the attempt chain on an item; repair rounds in a fork; best-state resumes | the best state improves attempt over attempt (tests passed, errors falling); a later attempt never ends worse | marginal gain per 10,000 tokens; the attempt at which quality stopped improving versus where the work stopped |
| **Delegating** | work handed to a role or session built for it: sections to forks, divisions to the architect, mechanical fixes to the harness, test failures routed to the owning section | the delegate's result was accepted without the delegator redoing it | tokens the delegator did not spend; delegation latency in ticks |
| **Splitting and forking** | an item divided into testable parts or sections; the width actually used | oracle pass rate and tokens per quality point against the unsplit path; focus gain (section 4.4) | coordination cost; idle-while-pending |
| **Designing and architecting** | a plan or contract written before code; interfaces named; a skeleton that compiles | interface mismatches avoided; attempts saved against a run with a thinner design; plan validity at first submission | design tokens as a share of the total; the cost of a redesign |
| **Testing** | tests written before code; tests that fail on the stub; tests that catch seeded defects | oracle agreement; seeded-defect catch rate; disputes upheld | tests per token; tests that were never run or never failed |
| **QA and review** | a review step that reads the merged result; QA findings (duplication, long functions, warnings); sanitizer runs | findings acted on and resolved; defects found before integration that would have escaped | review tokens against defects caught |
| **Suggesting** | a worker or the harness raises a proposal, a question or a blocker instead of guessing (inbox records) | the suggestion was accepted and changed the plan; a clarification avoided rework | accepted suggestions per request; the cost of a wrong guess avoided |
| **Improving** | refactors, cleanups, a catalogue row turned into a mend and a fixture, a prompt change that raised a block's bar | a later run's KPI is better on the same task; the same failure signature does not recur | improvement per change; regressions introduced |
| **Verifying** | running the checks before claiming done; reading the gate rather than the agent's claim | claims that matched the gate; no acceptance that a later check overturned | verification cost against failures prevented |

**Roles are scored on their own habits.** A tester is scored on testing and verifying, a developer on fixing, iterating and patching, an architect on designing and delegating, an integrator on QA and verifying, the harness itself on mending, delegating
and improving. A run's **craft score** is the weighted combination of its roles' habit scores; per habit the score is `presence x effectiveness x efficiency`, each in 0 to 1, and a habit with no opportunity in the run is not scored (it is
neither rewarded nor punished).

**Thrash and convergence are the two sides of iteration.** The harness tracks, per item, whether each turn changed the first error (progress) or repeated it (thrash). Convergence rate (turns that made progress / turns) is what the iteration habit is scored on; a run of
repeats triggers a different action (a hint, a deterministic edit, another strategy: catalogue rows 74 and 75) instead of the same turn again, and the thrash count is itself a KPI.

**How the score is used.** (1) To compare ways of working on paired runs, as in section 5: the craft score must predict the outcome KPIs, and a habit whose score does not predict anything is dropped. (2) To coach: the weakest habit of a role is named in
the scorecard and drives the next change (a hint table for a thrashing developer, a stronger skeleton for a weak design habit). (3) To emphasise: the habits are named in the prompts and the process stages (a review stage, a verify stage), because what the harness asks for is what it will get.
(4) As the language of the **knowledge register** and the **dreamer**: lessons are filed against a habit ("fixing: a const_iterator binding error is cured by cbegin"), so what one run learns about a habit helps the next.

**Weights** start equal across the habits that applied and are calibrated against outcome KPIs over the archived and future runs; they are a hypothesis until a regression of outcomes on habit scores supports them.

## 5. The fork decision, stated as an experiment

For the same task, platform, model and seeds, run the **session** executor, the **canvas** executor and the **fork** executor, several times each, and report per executor: oracle pass rate (4.2), tokens per quality point (4.3), decode ticks to the accepted whole (4.4),
concurrency factor and coordination cost. Decision rule (initial, to be tuned): the fork is preferred where its oracle pass rate is not lower and its tokens per quality point are within 30 % of the session's, or where its oracle pass rate is higher by at least 10
points; fewer ticks is a bonus, not a requirement. Where the fork's coordination cost exceeds its section tokens, the item is too small to fork and the harness runs it sequentially with the stated reason (it already records one).
This is the experiment block 6 of `docs/BLOCK_PROVING_GROUNDS.md` needs; the metrics make it comparable.

## 6. Instrumentation: where each number is captured

1. **Per session usage.** Every model session (agent attempt, fork skeleton, each section, each repair turn, integration round, mend, architect job, planning) writes a usage record: purpose, item, section, attempt, executor,
   prompt tokens, completion tokens, cached tokens, decode steps (`predicted_n`), reasoning tokens, aborted or not, outcome. The ledger gains rows **per session** with a `purpose` label, not only per job (this closes the blind spot of section 1).
2. **Engine counters.** The engine is started with `--metrics`; the daemon snapshots `n_decode_total`, `tokens_predicted_total`, `prompt_tokens_total` (and busy-slot means) at workflow start and end and at job boundaries, writing `engine-snapshot` records.
3. **Reconciliation.** A self-test and a per-run check: the sum of session tokens equals the engine counter delta within 2 %, and decode ticks derived from the counters equal the sum of per-session decode steps divided by the mean busy slots within a stated tolerance. A run
   that does not reconcile is marked "metrics unreliable" in its scorecard instead of showing wrong efficiency. (Against run-v9 to v11 this check fails by 5.8x to 277x, which is how it would have been caught.)
4. **Quality capture.** At integration the harness runs the independent reference suite (the platform's reference tests) against the final deliverable and records the oracle pass rate; the mutation-style check runs the model's tests against the harness's known-bad variants.
5. **Scorecard.** `sub0squad metrics WORKFLOW` prints the KPIs of section 4 with the waste breakdown; the final `report.md` carries it; `metrics.json` is archived with the run (with the build id); the portal gets a Scorecard panel (waste as a stacked bar, concurrency over ticks as a line, ladder depth per item).
6. **Compare and gate.** `sub0squad metrics compare A B` reports each KPI with intervals over repeated runs and calls a difference only when the intervals do not overlap; `scenario compare` is the same rule for the scenario suite. A regression gate fails `block run` when a proven block's KPI drops.
7. **Offline baselines.** The archived runs (v8 to v12) hold `fork.json`, canvas logs and conversation traces: a `metrics backfill` command computes what can be recovered from them (tokens by purpose, attempts, ladder depth) so the baseline exists before the new instrument does.

## 7. Ways of working: what the evidence says, and the metric that settles each

A deep look at the workflow patterns in use or proposed, each with what it is meant to buy, what the runs show so far, and the number that decides it.

| way of working | meant to buy | evidence so far | decided by |
|---|---|---|---|
| **Small focused sessions (fork, sections)** | detail and fewer wandering tokens; context that fits the 8k window; throughput | run-v10/v11: 8 to 14 sections at width 3 to 4, idle share near zero, zero conflicts; assembled engines compiled and passed 13 of 17 and 20 of 22 tests, which no single-file session reached | focus gain, tokens per quality point, coordination cost |
| **Test-first with an independent oracle** | tests that mean something; developers graded by tests they did not write | testers repeatedly accepted; wrong tests found only through failing developers; stubs that throw let weak tests pass (rows 2, 33) | oracle pass rate, test-quality (seeded defects caught), dispute rate |
| **Cheap checks before expensive ones** | catch slips in seconds (syntax precheck, a compile of one section) before a full gate | stray `();`, unterminated strings, unused variables cost whole attempts before prechecks | checks per token; share of failures caught at the cheapest level |
| **Patch, do not rewrite** | far fewer tokens per fix; no regression of what worked | whole-file rewrites hit the token limit (rows 29, 34); line edits and transactions built | changed-lines share, tokens per repair, regress rate |
| **Keep the best state, never regress** | progress is not thrown away | run-v11: a near pass (20 of 22) was lost to later rounds (row 70) | regress rate; best-vs-final gap; attempts that end worse than the best |
| **Mechanical fixes by the harness, not the model** | zero tokens for known errors; no wasted rungs | `hasExp` failed three model retries; the `silence-unused` mend fixed the same class with no model | healed rate, tokens saved per mend (a retry costs thousands, a mend none) |
| **Deterministic scaffolding** (skeleton from the contract, helpers like `app::run`, stubs that cannot cheat) | the model spends tokens only where judgement is needed | every omission of a contract function or helper misuse cost an attempt (rows 59, 71, 72) | harness leverage; first-pass rate before and after a scaffold change |
| **Widen before retrying; route failures to owners** | fix the cause at the right scope | interface mismatches retried at the leaf; failures routed to sections by message (row 64) | attempts per accepted item; ladder depth |
| **Bound everything** (turn limits, early abort, wall budgets) | no runaway cost | a skeleton of 13,893 tokens for stubs; a 340-line reply at the token limit | abort savings; the tail of tokens per attempt |
| **Right-size the model per job** | cheap models where they suffice | the 9B could not do the C++ tester job; the 35B can (and still struggles with long files) | first-pass rate and tokens per accepted item by model and persona |
| **Redundant (hedged) attempts** on idle slots | convert idle capacity into a higher success chance | not built | tokens spent per extra success; only worth it where utilisation is low and failure is costly |
| **Stop rules** | stop retrying when marginal gain is low | the ladder spent hours after a near pass | marginal gain per 10,000 tokens across attempts |
| **Clarifying questions up front** (planning stage) | less rework from a wrong assumption | not built (backlog) | rework avoided: tokens in attempts on a platform later changed by the owner |
| **Knowledge reuse** (the register) | lessons not relearned | not built | tokens saved per project; first-pass rate on repeated failure signatures |

**Reading the table as a method.** A way of working earns its place only when it improves quality per token, or the end-to-end tick latency at equal quality, on paired runs. The two most valuable results so far came from removing waste the harness itself caused (stubs that cheated,
helpers misused, rewrites, lost best states), not from changing the model: the harness's leverage is the main lever, and its KPI is first-pass rate rising as scaffolding improves.

## 8. Initial targets (to be calibrated on the first reconciled baselines)

| KPI | first target |
|---|---|
| Ledger reconciles with the engine | within 2 % on every run |
| First-pass rate, developer items, 35B | at least 70 % on the calculator platforms |
| Work ratio | at least 35 % on a passing run |
| Oracle pass rate of an accepted project | 100 % of the reference suite |
| Concurrency factor in a section phase at 4 slots | at least 2.5 |
| Manual step-ins per run | 0 |
| Convergence rate of iteration (turns that changed the first error / turns) | at least 0.8 |
| Thrash (identical consecutive replies) | 0 |
| Regress rate (attempts ending worse than the best earlier state) | 0 |
| Coordination cost of a fork | under 60 % of its section tokens |

These are goals to measure against, not claims about today.

## 9. Build order

1. **Accounting that reconciles** (slice 0; per-turn first-error and reply-identity records are added at the same time, they are the raw data of the habit scores): per-session usage records and ledger rows with `purpose`; the fork and agent write them; the reconciliation test; `--metrics` on the engine start line (the service command in the notes and the README get the flag).
2. **Engine ticks**: counter snapshots by the daemon; decode ticks, concurrency factor, tick latency per item; tests with the fake engine serving a `/metrics` response.
3. **Scorecard**: `metrics WORKFLOW`, the report section, `metrics.json` in the archive, the portal panel; `metrics backfill` for runs v8 to v12.
4. **Quality capture**: the independent oracle run and the seeded-defect check at integration.
5. **Habit scores and the craft score** (section 4a) from the records of steps 1 to 4, then **compare and gate**: `metrics compare`, the regression gate in `block run`.
6. **The fork experiment** (section 5) on one platform, 35B, several seeds, with the scorecards side by side; the first result decides the default executor.

## 10. Open questions for the owner

1. Is the oracle (the platform's reference tests, which the model never sees) the right independent quality measure, or should a human-written acceptance suite be added for the larger projects (the knowledge store)?
2. Should "success" at the project level ever count a run that needed a mend? (Proposal: yes, a healed run counts as success; a manual step-in does not.)
3. Wall-clock time is kept only as an estimate derived from ticks; is a measured wall figure still wanted for the portal's live view? (Proposal: show both, label the measured one as load dependent.)
