# Block proving grounds: isolated base concepts that build up to the whole

Status: design, 2026-11-12 (owner direction: parallelism and throughput are a core principle, satisfied by folding, forks and the canvas; each is a base building block with its
own proving ground, among other isolated concepts that build up to the whole). Complements [BUILDING_BLOCKS.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/BUILDING_BLOCKS.md) (archived) (the order of work), `docs/SCENARIOS.md` (the per-element
capability suite) and `docs/CANVAS.md`. Updated 2026-11-12 by the Fable iteration: the model-free proving grounds of blocks 1, 4, 5 and 6 and the `block` command are built
(section "Built without a model"); nothing below has a real-model result yet (section "Real-model measurements" holds the commands).

## Principles

1. **Bottom-up proof.** A block is built on blocks that are already proven. Its proving ground is an isolated fixture with a trusted judge, a metric, and a threshold; the whole is only
   trusted because every block under it was. A defect found at a higher level is first reproduced as a fixture at the block that should have prevented it (the self-mending rule).
2. **Throughput by default.** Work that divides into independent sections is generated in parallel up to the idle slots; a sequential path needs a stated reason (the sections are coupled:
   one needs another's result before it can be written). The structure that makes parallelism safe is the same structure that makes replies small: sections (folded regions of the canvas),
   transactions on them, claims so two workers do not edit the same section, and a coordinator that integrates. So parallel generation is not an add-on to the canvas; it is the reason for it.
3. **Same output, faster.** Parallel generation must give the same quality as sequential on the same task (same seeds, same gates); a speed-up that lowers the pass rate is not a speed-up.
4. **Measure the cost of coordination.** The skeleton, the section briefs and the integration are tokens too; the metric is wall time to a passing whole, not just decode speed.
5. **Every block reports a number**, archived with the build sha, so a change that makes a block worse is visible before it reaches a workflow.

## The ladder

Each block names its concept, the isolated fixture, the trusted judge, the metric and a first threshold (a target to be confirmed by measurement, marked *target*, not a result), and what it needs.

| # | block | isolated fixture and judge | metric (threshold) | needs | state |
|---|---|---|---|---|---|
| 0 | **Engine transport**: completion, streaming, timings, slot pinning, token counts | a fake streaming server (events split across chunks, pauses, error status) | pieces seen before the server finishes; result equals the non-streaming one; errors reported | none | built: `test_stream` |
| 1 | **Reply formats**: whole file, edit (search, lines), transaction, read, with cut-off handling | recorded replies and generated replies; apply and compare with the intended result | apply(reply) equals intended text on 100 % of well-formed cases; every malformed case gives a named error and changes nothing | 0 | **built**: 28 recorded-reply cases (four of them repairs of a planner document as edits: an inserted closer, one changed value, a new file with an edit, a stale line refused) (`tests/data/replies/`), every form and every refusal, cut-off replies and continuations; `test_blocks`, `block run --block 1` |
| 2 | **Gates and judges**: precheck, build, tests, oracle, attribution of the first failure | the reference solution passes, the starting state fails, per platform | 18 of 18 fixtures valid (all platforms and elements) | none | built: `scenario selftest` |
| 3 | **Single-section capability**: tester, developer first, repair (syntax, semantic), app | the six scenarios on three platforms, judged by block 2 | Wilson 95 % lower bound at least 0.6 over 10 trials, per element and platform | 0, 1, 2 | fixtures built; no model results yet |
| 4 | **Folded views and reads**: outline correctness; reads used and bounded | outline unit tests on C++, JavaScript, Python samples; a scenario where the draft is too large to show whole | fold regions correct on the samples; a read-then-edit scenario passes at the block 3 bar; read-only replies bounded | 1 | outline, section view, path-only read and bounded reads built and tested (`test_blocks`, `test_canvas_agent`); the read-then-edit scenario on a model is not built |
| 5 | **Canvas transactions**: revisioned sections, all-or-nothing, rebase, revert | a model-based test: random operation sequences against an in-memory model; the same repair scenarios answered as transactions | text equals the model after every step on 1,000 random cases; repair scenarios at the block 3 bar with a changed-lines share well below a rewrite's | 1, 4 | **model-based test built** (1,000 random cases equal the model, `canvasModelCheck`); **wired** (`--executor canvas`, `test_canvas_agent`); the repair scenarios as transactions on a model: not run |
| 6 | **Parallel sections (fork)**: a skeleton, one section per worker on idle slots, merged | the same file generated sequentially and in parallel from the same skeleton, same seeds, same gate | **speed-up**: wall time to a passing file at least 1.8x at 4 slots (*target*); **parity**: pass rate within 10 points of sequential; **utilisation**: idle-slot time under 25 % while sections are pending; **conflicts**: zero lost edits | 3, 5 | **built** (`src/fork.cpp`, `llm-fork`, `--executor fork`); the harness is proven model-free (parity: identical files; 0 conflicts; 0 lost edits; `test_fork`, `test_blocks`); speed-up, parity of pass rate and utilisation on the 35B: not measured |
| 7 | **Section integration**: the coordinator merges sections and finds seams | sections with a planted seam defect (a signature mismatch between two sections) | the integrator names the seam in the first report on at least 9 of 10 seeded defects; a clean merge raises no false seam | 5, 6 | not built |
| 8 | **Division**: the architect splits an item into testable parts | a set of items with known good divisions; the validator and the division-completion rules judge | every proposed division valid and covering at least 8 of 10; invalid ones are named, never merged | 2, 3 | validator partly built (`completeDivision`) |
| 9 | **Decision policy and the ladder**: widen before retrying, early abort, bounds | recorded failure sequences replayed through the policy | each failure signature leads to its intended rung (golden decisions); no rung is entered twice | 1, 2 | policy unit tests built; golden logs recorded: `tests/data/actions/*.ndjson` hold every decision (with its inputs and reasons) of every engine scenario, compared byte for byte by `test_workflow`, including the mend decisions (`mend-*`). Not yet: recorded failure sequences of REAL runs replayed through the policy |
| 10 | **Process as data**: stages, executors, the standard process equal to the legacy engine | golden action logs of the legacy engine | the standard process reproduces the goldens exactly | 9 | built as stages as data and proven by ctest (`test_predicate`, `test_process`, `test_workflow`; `PROCESS_DESIGN.md` section 13) |
| 11 | **The whole on a platform**: the calculator end to end | the plan on a platform, frontier off, the 35B | zero manual mends over 3 runs in a row; per-item numbers recorded | 3 to 10 | first runs in progress (`w-000002`), not passing |
| 12 | **The whole at scale**: the knowledge store | the slices of [KNOWLEDGE_STORE_SCOPE.md](https://github.com/CraigHutchinson/Sub0Squad/blob/96a17c925fb82feff086fc5106bc0a79915be3e8/docs/KNOWLEDGE_STORE_SCOPE.md) (archived) | the slice gates and properties | 11 | designed |

## The parallel-sections proving ground (block 6) in detail

* **What is held fixed**: the platform, the file, the skeleton (signatures and the section list, written once by a primer and checked by the compile precheck), the per-section cards, the seeds, the gate.
* **What varies**: how many sections run at once (1, 2, 4) and whether sections are generated against the skeleton alone or also see finished neighbours.
* **Judge**: the item's gate over the merged file (all tests pass), exactly as for a sequential attempt; plus the canvas log (no lost or conflicting edit).
* **Measured per run**: wall time from first request to gate pass; time with an idle slot while a section was pending; aggregate decode tokens per second against a single stream on the same engine;
  tokens spent on skeleton, section briefs and integration; attempts per section; pass rate over trials.
* **Why it should win on this host**: a mixture-of-experts model decoding several sequences reads its weights once per step for all of them, so aggregate decode rises with the number of concurrent
  sequences until compute binds; the engine here has 4 slots. How much it rises is a measurement (the docs record decode figures at one and several sequences for other models; the 35B's curve at
  4 slots is to be measured with `sub0squad scenario run --parallel`), and the *target* above is a goal, not a claim.
* **When parallel loses** (and the harness must then choose sequential): sections coupled by a shared helper that does not exist yet; sections too small for the coordination cost; memory pressure (the shed rule applies: forks are the first thing to shed).

## Built without a model (2026-11-12)

**Throughput by default is a rule in the code**, not a preference: `planParallel` (`include/sub0squad/throughput.hpp`) decides how many sections are generated at
once. It is the smaller of the sections and the idle slots; one at a time requires a stated reason, which is recorded in `fork.json`; below a memory floor forks
are shed first (also recorded); an experiment can fix the width. The fork executor calls nothing else to choose its width, and `test_blocks` pins every case.

| block | what proves it here | number on this build |
|---|---|---|
| 1 reply formats | `runReplyFixtures` over `tests/data/replies/*.case`: the reply (or a cut-off reply and its continuations) is applied as the agent applies it and compared with the intended files; a malformed reply must be refused with a named error and change nothing | 28 of 28 |
| 1 turn control | `detectRunaway`, `trimOverlap`, `continuationText`, `cutOffInsideEdit` on text, with the boundary cases (40 lines is allowed, 41 is not; a partial line gives no verdict; indentation is not an overlap) | all pass |
| 4 folded views | fold regions, the section view, the outline for a path-only read, a one-line function addressed by name | all pass |
| 5 canvas | `canvasModelCheck`: random sequences of transactions (through the API and through the reply text), stale transactions, malformed ones, reverts, against an independent model; the text must equal the model after every step and every revision must stay retrievable | 1,000 of 1,000 cases (6,012 steps); a second seed 200 of 200 |
| 6 parallel sections | `parallelSectionsProof`: the Python calculator engine from one skeleton, one section at a time and four at once, on a slot-model engine that answers each section from the reference; both merged files through the real gate | 12 sections; both pass; files identical; 0 conflicts; 0 lost edits |
| 6 the arithmetic | `simulate`, `accountSpans`, `parallelMetrics` on hand-checked cases (four equal sections; unequal sections; idle slot-time; the skeleton counted in the speed-up) | all pass |

**The slot model is a placeholder, and its speed-up is arithmetic.** `SlotModel` has 4 slots, 10 tokens/s alone and an aggregate of 1.0, 1.7, 2.3, 2.8 times that at 1 to
4 sequences. Those figures are an assumed shape, not a measurement of the 35B or of any model. On them the harness reports 1.73x for the 12-section task including
the skeleton, with 7 % of the tokens spent on coordination. That number says the accounting works; it says nothing about the real engine. Replace the curve with
the measured one (command 1 below) before quoting any speed-up.

**The command.** `sub0squad block list` names every block and how it is proven. `sub0squad block run [--block N] [--fixtures DIR] [--out DIR] [--exe PATH]` runs the
proving grounds that need no model and writes `block-results.json` (schema `block-results-1`, stamped with the build) next to `scenario-results.json` under
`--out` (default `out/scenarios`). States: `proven`, `partly proven` (the model-free part passed; a part that needs a model remains: blocks 5 and 6), `failed`,
`not run`, `by ctest` (proven by a test this command does not re-run), `needs model`, `not built`. Block 2 runs the real toolchains and so needs `--exe`.

## Real-model measurements (for the lead session; none of these has been run)

Run one at a time, with the engine otherwise idle, and archive each `--out` directory with its build. `ENGINE` stands for
`qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=8192,slots=4` (use the profile of the engine that is running).

1. **The aggregate decode curve** (block 6, "why it should win"): the same scenario at 1, 2 and 4 concurrent trials; `engine.decode_tps` per trial is in each
   trial's job metrics, and the aggregate is the sum.
   ```sh
   sub0squad scenario run --engine ENGINE --only cpp-cmake/developer-first --trials 4 --parallel 1 --out out/scenarios/decode-p1
   sub0squad scenario run --engine ENGINE --only cpp-cmake/developer-first --trials 4 --parallel 2 --out out/scenarios/decode-p2
   sub0squad scenario run --engine ENGINE --only cpp-cmake/developer-first --trials 4 --parallel 4 --out out/scenarios/decode-p4
   ```
2. **Block 3 baseline and block 5 on the same seeds** (session against canvas; the bar is the Wilson lower bound per element, and for the canvas a changed-lines
   share well below a rewrite's: `agent.json` holds `canvas.changedLines` and `fileLines`):
   ```sh
   sub0squad scenario run --engine ENGINE --trials 10 --parallel 2 --executor session --out out/scenarios/session
   sub0squad scenario run --engine ENGINE --trials 10 --parallel 2 --executor canvas  --out out/scenarios/canvas
   ```
3. **Block 6, the parallel-sections proving ground**, on the node that failed in run `w-000002` (catalogue row 37, `dev-engine` of the C++ calculator), same seeds,
   one section at a time and then as wide as the slots (take WORKFLOW from an archived run whose `test-engine` is accepted):
   ```sh
   sub0squad replay WORKFLOW dev-engine --fork --seeds 1,2,3 --engine ENGINE --max-tokens 1500 --repair-turns 2
   ```
   `replay --fork` runs `llm-fork` per seed, each from its own skeleton. The arms of the comparison:
   ```sh
   sub0squad replay WORKFLOW dev-engine --fork --sequential-reason "block 6 baseline" --seeds 1,2,3 --engine ENGINE --max-tokens 1500 --repair-turns 2
   sub0squad replay WORKFLOW dev-engine --fork --parallel 2 --seeds 1,2,3 --engine ENGINE --max-tokens 1500 --repair-turns 2
   sub0squad replay WORKFLOW dev-engine --fork --parallel 4 --seeds 1,2,3 --engine ENGINE --max-tokens 1500 --repair-turns 2
   ```
   Each variant's `items/dev-engine/attempt-1/fork.json` holds `passed`, `failure`, `width`, `wallSec`, `sectionWallSec`, `targeted`, `mechanicalMends`,
   `conflicts`, `lostEdits`, the tokens per phase and each section's calls and refusals. Speed-up is sequential `wallSec` over parallel `wallSec` for the
   variants that pass; parity is the pass rate of each arm over the seeds. **Read row 47 first:** before 2026-11-13 forked sessions were pinned to one slot,
   so no earlier run measured parallel sections at all. In each section's conversation the per-call decode speed should now be BELOW the single-sequence
   speed while the aggregate is above it; if every call still decodes at the single-sequence speed, the sessions are queueing again. (`idleShare` is
   measured from client spans and cannot show this.)
4. **The whole on a platform with sections** (block 11): `sub0squad up --frontier off`, then
   `sub0squad do "write a scientific calculator in C++" --agent llm --engine ENGINE --executor fork --max-tokens 1500`.

## Model tiers and the tier 0 gate (2026-11-17)

The blocks above are exercised without a model or one scenario at a time. The **model tiers** exercise them together on a real engine, smallest first. The current runner uses T0 as the admission gate for T1 and T2. The comparison and receiving requirements for each tier are described below.

| tier | what one run is | where | gate to run it |
|---|---|---|---|
| **0** | one worker on the canvas, one seeded task on the reference calculator: a defect, a stub, a missing part, a broken build, a curveball | `validation/tiers/make_t0.py` (the cases), `run_t0.sh` (runs), `gate_t0.py` (the gate) | none: it is the gate |
| **1** | one worker builds the whole calculator on the canvas against the owner's tests (process `single`: no division, no forked sections) | `validation/tiers/t1-plan.json`, `run_tier.sh <repo> 1 <engine>` | tier 0 PASS, same build, same engine |
| **2** | the same plan with forked sections (block 6): skeleton, parallel sections, the harness's assembly | `run_tier.sh <repo> 2 <engine>` | tier 0 PASS, same build, same engine |

**The cases** (`make_t0.py --list`), each built from `validation/reference/calculator-cpp` and given the reference tests as the owner's tests (plus tests that pin a
whole feature where the reference's do not, so a partial answer cannot pass): *fix* (one wrong line, an off-by-one, a symptom far from its cause, a missing check,
two defects), *compile* (a syntax error, a warning as error), *write* (one function into a stub, a feature missing from several places, a whole small file, a
tester's test file), *curveballs* (five defects, a file cut off mid-function, unresolved merge conflicts, an engine whose approach cannot meet the spec, a
function defined twice, a syntax error hiding a logic defect, a whole parser skeleton), *repeated class* (one defect pattern at six sites; twenty calls to a name
that does not exist: a batch fix or many small ones), and *two files*. `check_t0.sh` proves every seed fails the way its case says, without a model, and the
reference passes every test a case gives.

**The gate** (`gate_t0.py`) judges the run directories, never a summary: a run counts when it ran on the build being judged (the daemon's `run-info.json`), on
the named engine, with the case as `make_t0.py` defines it now (a run of an older definition is listed as stale, not counted). A run passes when the item was
accepted, a model built it (`report.md`), and nothing was left unfinished. **PASS** is every case passing in every counted run, with at least 3 counted runs per
case; one failing run is **FAIL**; too few runs is **INCOMPLETE**. It writes `out/runs/t0/gate.json` and `gate.md`.

**Code quality** is judged with it (`quality_t0.py`), against the seed and the reference: lines changed against the ideal fix, collateral changes outside the
defect, leftovers (a stub, a conflict marker, a TODO: a hard failure), repeated lines (copy-paste), format, comments dropped, and for a test file the share of
checks copied from the given tests. Only leftovers fail a run today; the rest are reported per run so thresholds can be set from measurements, not guesses.
It found a harness defect on its first day: a SYMBOL edit whose signature came from column 0 had its body pushed four columns too far (fixed in `canvas.cpp`).

**Where it sits in the workflow.** Model tiers need the local engine, so they are not in hosted CI; they are a local quality gate, like `local-ci/full`:
* **A release or milestone** (`v*`, `milestone/*`, a pull request labelled `ci:release`) needs a tier 0 PASS on the proving engine for the build being
  released, recorded with it (`out/runs/t0/gate.json` attached to the release notes or the pull request).
* **A change to the agents layer** (prompts, views, reply forms, the canvas, the turn loop): run the tier 0 cases it touches before merging; the whole suite
  before a release. **The smoke set** is the quick check while iterating: `run_t0.sh <repo> <engine> 1 @smoke` runs one fast case per kind of edit (a
  snippet, several edits in one reply, an insertion, a broken build, SYMBOL, FILE, conflict markers, two files; `make_t0.py --list smoke`), one run each, in
  about 8 minutes on the 35B against about 3.5 hours for the gate. It writes `smoke.md` and `smoke.json`, never `gate.md`: a smoke PASS is not the gate.
* **Tier 1 and above** run only on a tier 0 PASS: `run_tier.sh` refuses otherwise (exit 3) and says why. `--force "why"` runs one anyway when it is explicitly
  asked for, and records the reason in the run's home (`forced.txt`), with the gate it was forced past (`t0-gate.json`), so it is never read as a gated result.
* **A smaller model** (a 9B, a 12B) runs the same tier 0 suite once the proving engine (the 35B) passes it reliably: a smaller model that passes tier 0 shows the
  cases are well defined as building blocks; one that fails shows where the edge is.

### Scope and scaling beyond the proving grounds

The numbered **blocks** above isolate mechanisms. A scenario puts one mechanism in its real executor with a fixed initial state, a clear resolution, and a trusted judge. The **model tiers** measure how those mechanisms work together. Layers within T0 group a composite with its prerequisites, such as a rewrite and the individual edit and continuation skills it depends on. They remain part of T0. [Scenario contract](SCENARIOS.md), [T0 layers and case review](T0_LAYERS.md).

| Tier | Work and starting state | What it adds | Evidence required |
| --- | --- | --- | --- |
| **T0 — isolated work** | One worker, one known defect or bounded deliverable, fixed seed and acceptance tests. | One mechanism such as a compiler-guided repair, a read of folded source, or continuation of the worker's own truncated reply. A composite runs after its leaves are understood. | Seed and positive control validate the fixture. Capture the mechanism that actually ran and judge the exact artifact. The existing full T0 gate applies its own counts. |
| **T1 — whole deliverable, serial** | One worker builds the supplied scientific calculator plan and delivers both the evaluator and CLI. Fixed public contracts and owner tests. | The worker must plan its own edits across a complete product, preserve working behavior, and pass integration without coordination between agents. | Original 22 tests on the assembled artifact, model provenance, and no manual mends for a hands-off pass. T1-derived repair diagnostics can start from archived failures and carry separate verdicts. |
| **T2 — same deliverable, sections** | The same supplied plan, product and tests as T1. The fork executor uses a skeleton, section claims and assembly. | Section handoffs and parallelism. Keep the public interfaces and tests fixed while comparing execution strategies. | Score the final artifact with the T1 judge. Track lost edits, integration failures, coordination cost and time to a passing result. Compare with a matched serial baseline before claiming a speedup. |
| **T3 — minimal multi-role product (proposed)** | A tiny scientific slice with a supplied plan. One evaluator, one CLI, independent tests and one cross-component contract. Start with arithmetic, grouping, one trigonometric function with degrees, and invalid-input handling. | Scoped tester and developer work, the dependency handoff and automatic integration. Keep the initial workflow small and leave parallelism and complex recovery out of the first comparison. | Black-box evaluation of the actual CLI, compatibility across the seam, test expectation audit, and completion without a manual fix. A later seeded disagreement can isolate the dispute path. |
| **T4 — same product from a request (proposed)** | Run the T3 product from the same public request in an empty workspace. The planner supplies the plan and scaffold. | Planning and its handoff to the already exercised tester, developer and integration workflow. The supplied-plan T3 run is the comparison. | A valid plan with covered requirements, the same black-box grade as T3, and the entire request-to-accepted-product trace. Keep the reference and grader outside the agents' workspace. |
| **T5 — bounded scale and recovery (proposed)** | Start from a passing T4 workflow. Increase one axis per cohort, such as one more module, longer editable source, one more independent worker, another platform, or one deliberately seeded failure. | Evidence that the workflow retains correctness and predictable cost as it grows. A larger product such as the knowledge store starts with its own small received vertical slice. | Preserve the existing product checks and add only the acceptance checks for the added scope. Compare rates, integration failures, retries and cost under the same concurrency and model profile before combining axes. |

T0, T1 and T2 have repository runners. The T1 and T2 runner currently enforces a current T0 PASS for both. It does not enforce a T1 PASS before T2. For T2 performance evidence, require a received T1 baseline on the same product and judge. T3–T5 have a staging and admission runner in `validation/tiers/product_tiers.py`. They do not change the release gate. A historical supplied-plan plain calculator run reached automatic integration, with a 12/12 external grade. That is evidence for the workflow shape. The scientific slice and clean-slate path still need receiving evidence under their own contracts. [Current runners](../validation/tiers/run_tier.sh), [archived whole-workflow evidence](RESUME.md), [clean-slate contract](CLEAN_SLATE.md).

#### Keep each workflow small enough to explain

- **Add one source of uncertainty at a time.** Increase end-to-end responsibility before expanding features, file count, concurrency, platforms, or budgets. Where possible, hold the product constant across a tier boundary. Use the real runner and checker with the smallest plan that exposes the new handoff.
- **Keep the judge outside the work.** Prove a seeded fixture fails as intended and a known-good control passes. In later product tiers, independently build and grade what the workflow shipped. A role may revise its own tests through the workflow, while owner acceptance and the external grader stay fixed. An intermediate success or an accepted plan cannot stand in for a working product.
- **Keep the evidence comparable.** Pin build, fixture or request, model weights, chat template, effective slot context, concurrency and budgets. Record actual role ownership, test counts, wall time to acceptance, tokens, retries, first failure and artifact identity. Use paired seeds for comparisons and report trial counts and uncertainty. A one-off success establishes a path through the workflow. It leaves reliability and speedup open. Report scheduler or engine failures separately from worker failures.
- **Send failures back to the cheapest useful receiver.** A failed higher-tier run should yield the smallest reproduction that preserves the mechanism, such as a folded-view localization case, a parser contract case or a seam case with one dependency. Preserve the higher-tier failure and use it again after the leaf is fixed. Keep rare cases at lower cadence when they have a credible mechanism. Park unsupported permutations, and retire redundancy when an existing test covers the same behavior and boundary.

The next balanced slice stays at T1: exercise the same delimiter defect with the relevant source visible and folded, using the current archive and original tests. The top-down goal is a builder who can use the gate's diagnostics to finish a working calculator. The bottom-up constraint is the canvas view and read budget, which already affected localization at T1 size. Exposing the region isolates the cost of finding it while preserving the repair task. A passing isolated control is received back into the complete T1 build before using it as a prerequisite for later tier evidence. [Current T1 slice and results](T0_LAYERS.md#24-first-t1-rework-slice).

#### Admission, comparisons and release evidence

| Decision | Evidence to check before moving on |
| --- | --- |
| Receive a proving block | Its starting condition is observable, the control demonstrates the expected result, and the named workflow can consume it. Keep a model-free failure reproduction where the defect is mechanical. Use real-model trials to measure worker capability. |
| Admit T1 or T2 | Use the current runner's T0 PASS rule. Separately label an explicitly authorized forced diagnostic and preserve the gate it bypassed. Use completed T1 runs as the serial performance baseline for T2. |
| Receive the T3 handoff | Start from a received serial product and isolated tester and integration checks. Require fork evidence only for a workflow that uses the fork executor. A deliberate interface mismatch or wrong expectation is its own diagnostic cohort after a clean supplied workflow works. |
| Receive T4 planning | Hold the T3 scope and external grader fixed. Pin the public request, compare supplied and generated plans, and record whether a failed run stopped at planning, a handoff, integration or the product grade. |
| Admit a T5 scale experiment | Start from a received T4 slice. Name the dimension and bound under test, capture a same-profile control, and stop at the first unresolved limit. Decouple recovery challenges from load until each is understood on the small workflow. |

Use a **targeted check** during daily changes, then a **receiving cohort** when a change crosses a workflow boundary. Keep **release evidence** tied to the build and the existing gate policy. Choose the cohort size, seeds and time and token budgets before running it. Use full budgets for proof. Allocation, infrastructure failures and discovery stops stay visible with their own dispositions. Report the scheduled denominator, counted results and pass fraction with uncertainty. For throughput, report wall time to an accepted product and the pass fraction together, with coordination and retry costs included.

Use `python -B validation/tiers/product_tiers.py stage 3` (or `4` or `5`) to record a prepared trial. `run <tier> --prior <received-run>` checks admission before it starts the workflow and grades the published CLI independently. Add `--staged ID` to use a prepared trial; an unsatisfied prerequisite leaves it staged and records the reason. T5 implements one scale axis, the addition of right-associative power with explicit unary precedence. Model-free acceptance covered the supplied plan and the known-good CLI at T3/T4 (10 cases each) and T5 (13). Live T3–T5 promotion requires received predecessor evidence.

#### Scale the work without losing the diagnostic

- **Product scope.** Expand the public behavior or number of modules one at a time. For the calculator, accept a small scientific CLI across the evaluator seam before adding richer functions or a different interface.
- **Source and context.** Hold the defect and behavioral judge fixed while varying file size, visibility and effective context. Record where the source was shown and when a read or continuation was necessary. Keep output truncation, context fallback and incomplete inherited source as separate mechanisms.
- **Coordination and load.** Add independent workers or projects against the same acceptance and adjudication budget. Measure actual active slots and queue wait. A runnable staged plan does not establish achieved parallelism.
- **Recovery and portability.** Use one seeded failure at a known boundary per control, then exercise the real recovery path. Repeat the received slice on another platform before changing product scope there.

#### Run and evidence contract for the portal

Each cohort identifies its project, tier or scenario, planned cases and seeds, admission decision, and the pinned build, fixture or request, and engine profile. Each execution records its source, terminal verdict when available, observed phases, artifact identity, and evidence classification. Classifications distinguish live model, scripted harness, replay, forced, and discovery-stopped outcomes. An early-stop count can explain investigation cost. It does not fill a release sample.

Reuse existing gate, workflow and scenario records as the durable sources. The portal should group them by project and cohort and link to their owners' original records. Show staged work from an explicit brief or prepared spec, running work from current activity, and historical outcomes from the owning runner. Display unresolved prerequisites and unknown totals explicitly. The [portal work overview](PORTAL_WORK_OVERVIEW.md) defines identity, states and receiving checks for this view.

### Tier 0 iteration (2026-11-18): stopping early, the failure breakdown, the half-written A/B

**A run stops at its first failed attempt** (owner: tier 0 checks a targeted step, so a run should not grind through the squad's retries into a long
failure). `run_t0.sh` runs under the `t0` process (`lib.sh`: no decompose or sections, and `rearchitect`, `thinkFirst`, `escalate` and the new
`strategies` flag all off; `leaf.allow.strategies` turns off the manager's think and engine strategies) with `--max-local ${T0_ATTEMPTS:-1}`. One
exception remains: the near-pass rule (at most 15 % of the tests failing) still allows a repair attempt past the limit (`skeleton` run 1 of the
baseline). A failing run now takes 1 to 4 minutes instead of 10 to 14. `run_t0.sh` takes a lock (`out/runs/t0.lock`; exit 4 while another loop
lives): a stopped caller had left its bash loop running, and three loops wiped each other's homes, which looked like the strategy fix not taking.

**Baseline, one attempt a run** (`out/runs/t0-baseline-1attempt`): half-written 1/3, wrong-approach 0/3, skeleton 1/3.

**The breakdown.**
* **half-written** has one clear objective (finish the file) and a known cause: before the edits-first scope it passed 3/3 on its first attempt
  (`t0-101e19c`); after it, the 35B's first move was a 43-line SEARCH of the cut region (stopped: long SEARCH), then a 165-line completion (stopped:
  big edit), and it never appended after the last line.
* **wrong-approach has no clear resolve of its own.** Its brief says only "most of the provided tests fail"; the model has to decide on a rewrite, so a
  failure does not tell a wrong decision from a rewrite done badly. It is a composite ([T0_LAYERS.md](T0_LAYERS.md); `REQUIRES` in `make_t0.py`), and
  two of the cases it requires fail on their own (`out/runs/t0-rewrite-b155485`, with the full ladder): **write-engine 0/2** (4 to 5 attempts each),
  **replace-function 1/2** (the pass on attempt 5), add-functions 1/1. It is not worked on directly: it is checked again once its requirements pass.
  `skeleton` requires the same write-engine.

**The half-written A/B** (35B, 5 runs an arm, one attempt; `out/runs/t0-ab-<arm>`):

| arm | passed | turns | out tokens |
|---|---|---|---|
| **hint**: the cut-off file is named, with the form that finishes it (`=== EDIT: <file> AFTER <n> ===`) | **2/5** | 4 to 7 | 85 to 6,656 |
| stub: the file treated as a stub (FILE allowed, no edit size limits) | 0/5 | 8 to 11 | 2,604 to 9,665 |
| edits: FILE still refused, the edit size limits lifted | 0/5 | 8 to 11 | 3,069 to 11,415 |
| today's rules (19 runs: the baseline, plus the first A/B, which ran the default in every arm) | 4/19 | 4 to 19 | 1,739 to 10,680 |

The hint is kept (`95b4c6a`; `endsInsideBlock` in `turn_control.cpp` decides a cut-off file) and the experiment switch is gone. 2/5 against about
20 % is within the noise of 5 runs (identical code gave 0/5 and 2/5 in the first A/B), but it is the only arm that changed the first move: 3 of 5 runs
opened with the append. **The hint runs that still fail append too little:** the missing tail is 56 lines (under the 100-line edit limit), but the
model appended 11 lines (the current function), then 2, leaving the class's members (`s_`, `pos_`, `degrees_`) and `evaluate()` unwritten; the
compile errors then point at line 16 (the constructor), and it goes back to editing code that was right. A next step, if half-written is iterated
on: the hint lists what the tail must still hold (the names the errors report as undeclared, the functions the spec declares that the file lacks).

**A measurement pitfall found by it:** jobs run with a fixed environment (`src/shim.cpp`), and a measurement switch reaches the agent only when it is
listed there beside `SUB0SQUAD_ISSUE_LAYER`. The first A/B had `SUB0SQUAD_CUTOFF` missing from that list and measured the default four times;
the hint arm's transcripts, which never showed the hint, gave it away. Check an arm's transcript for its effect after its first turn.

**Next, in order:** half-written (iterate the hint, or take the gain); write-engine (it blocks wrong-approach, skeleton and write-file);
replace-function; then wrong-approach and skeleton again; then a fresh full gate on that build, then tier 1. Also, the gate should report a failing
composite as blocked by a failing requirement (`gate_t0.py` does not read `REQUIRES` yet), not as a failure of its own.

### Thinking (2026-11-18/19): the T1 check_eol failure, and reasoning as the missing piece

**The T1 failure** (`t1-first-attempt/.../15101-limit6000`): turn 0 wrote the whole file, defining and calling `Parser::check_eol()` without
declaring it in the class. The verdict was two short errors, naming the definition (line 71) and the call (line 254); the class (36 to 59) was on
screen. The worker spent five turns only reading folded bodies unrelated to the error, until the reads ran out. **Thinking was off for every turn of
every T0 and T1 run so far:** `EngineProfile::thinking` defaults to false and no tier passes `thinking=1`; the ladder's `thinkFirst` retry was the only
rung that turned it on, and the t0 process turns that off.

**Two cases reproduce it** (`make_t0.py`): **undeclared-member** (T1's shape: `Parser::checkEnd()` defined out of the class and called, never
declared; the fix is a declaration at neither line the errors name) and **undeclared-many** (`eat()` moved out of the class: one cause, 16 errors).
Both seeds fail to compile, and a one-line declaration passes every test (`check_t0.sh`). Without thinking the 35B edited the lines the errors name
(rewrote the definition in place, deleted it, invented an overload of `eat`), not the class.

**The A/B** (35B, 5 runs a case an arm, one attempt, dev build; `out/runs/t0-fixsite-<arm>`). The note (`SUB0SQUAD_FIXSITE=1`, the owner's
wording, every compile verdict): "Review all of these errors together before you edit: several may come from one root cause, and an error is reported
where the compiler noticed it, which is not always where the cause is. Address the root cause directly; how is yours to decide."

| arm | undeclared-member | undeclared-many | turns |
|---|---|---|---|
| control | 1/5 | 5/5 | 1 to 15 |
| note | 2/5 | 4/5 | 1 to 11 |
| **note + thinking** | **5/5** | **5/5** | **2 to 3** (47 to 101 s a run) |

Nine of the ten thinking runs took the same path: thinking reached its cap, the harness stopped it with no edit begun and carried the reasoning's end
into a turn with thinking off ("write the transaction it was leading to"), which wrote the declaration in 105 to 325 tokens. **A forced thinking stop
is built and works:** the cap is `kThinkingShare` (60 %) of the reply budget; `kCarriedReasoningChars` (3000) of its end are carried; thinking loops
are caught by the same repeat rules as replies.

**The sweep over the earlier failures, thinking on** (`out/runs/t0-think-sweep`, 3 runs each, `--max-tokens 3000`): **0/15** (half-written,
write-engine, replace-function, skeleton, wrong-approach). Every thinking turn hit the cap (about 1,800 tokens) and none ended on its own: half of each
attempt's turns were reasoning cut off and thrown away. **With room** (`out/runs/t0-think-room`, `T0_DO_ARGS="--max-tokens 7000"`, a cap of about
4,200 tokens): **half-written 2/3**, replace-function 0/3 (each failure 1 or 2 of 8 tests). Thinking still never ended on its own (every stop at
about 14,700 characters): **the 35B writes the whole code inside its reasoning** (the carried tails end mid-way through `evaluate` and
`parsePostfix`), so the thinking is mostly a full draft that is thrown away, its last 3,000 characters carried, and the code written again.

### Think, plan and act (2026-11-19)

The saved Windows runs used qwen35, the compile feedback note, a 7,000-token reply budget and three runs per case. The earlier control used the same note and budget. Counts are from the run summaries.

| Prompt | half-written | replace-function | undeclared-member |
|---|---:|---:|---:|
| Earlier control | 2/3 | 0/3 | |
| Cause and plan in the thinking prompt | 2/3 | 0/3 | |
| Plan reply followed by a direct act turn | 2/3 | 0/3 | 3/3 |

The two new arms used the dev build recorded as 0.0.0-220-ga4bfb99-dirty. The original gate used the release executable's version and excluded those runs. The runner now supplies the executable it used to the gate. The summaries are under out/runs/t0-thinkplan-1 and out/runs/t0-thinkplan-steps.

In the separate-plan arm, three attempt traces recorded a completed plan. The first replace-function trace hit the thinking cap on three turns. It reviewed existing functions and weighed competing continuation and edit instructions. The options prompt asks for the root cause, three approaches with their risks, and a choice. Its fake-engine test exercises the plan handoff and the thinking-off act turn. The live experiment and the next design slices are in [the reasoning workflow brief](sessions/canvas-reasoning-workflow.md).

Other open experiments include using interrupted reasoning as a reply prefill when it contains a usable draft, enabling thinking only after a failing verdict, and varying the thinking budget. The compile note remains experimental.

### Matched Windows screen and request replay (2026-11-19)

One run per case and arm, with seed 1911, qwen35 (Qwen3.6-35B-A3B APEX I-Compact), context target 16,384, the compile note, and 7,000 reply tokens. Both arms used the same 104a804 native executable. The evidence prefix is out/runs/t0-reasoning-20261119-121522. The archived plan beside each run preserves the original contract.

| Case | Steps outcome / seconds | Options outcome / seconds | Thinking stops, steps / options | Completed plans, steps / options |
|---|---|---|---:|---:|
| replace-function | Failed, 4/8 tests still failing / 470 | Failed, 5/8 still failing / 356 | 3 / 3 | 1 / 0 |
| undeclared-member | Passed / 78 | Passed / 105 | 0 / 1 | 0 / 0 |
| half-written | Failed, 5/16 still failing / 600 | Failed to compile / 449 | 3 / 4 | 2 / 0 |

Both arms passed one of three cases. The shorter options failures left worse artifacts. The traces recorded 119,239 reasoning bytes for steps and 117,612 for options. Five cancelled replace-function calls in each arm returned no completion usage. The 14 tokens shown for options cover its final empty file block, so totals from the runner cannot establish token savings here. In the steps replace-function trace, only about seven seconds separated the two edited replies from their compiler or gate verdicts. That bounds the directly observed opportunity for overlapping those two checks on this case.

Both replace-function arms tried to implement the inherited scientific contract, including trig, in an arithmetic diagnostic. The scope guard stopped the oversized edits twice in each arm. The fixture now has an arithmetic contract and the same seeded file and eight tests. Tier 0's case identity includes the contract, so a historical run cannot qualify against a different one. All 32 plans were compared offline. Only this case changed. See [intervention 82](INTERVENTIONS.md#what-the-table-says).

The first options plan request from the original replace-function trace was replayed with a 768-token limit and the recorded request seed. These are single requests, with the cache disabled and all other sampling values held fixed. A complete plan has both plan markers.

| Request | Total tokens | Seconds | Complete plan |
|---|---:|---:|---|
| Thinking on, no backend budget | 768 | 19.03 | No, length limit before visible content |
| Thinking on, backend thinking budget 160 | 311 | 9.61 | Yes |
| Same budget and presence penalty 1.5 | 325 | 10.19 | No, missing closing marker |
| Thinking off, same prompt and sampling | 241 | 8.45 | Yes, two proposed options overlapped |

Only the bounded-thinking response gave three distinct approaches and a complete block. The direct response used fewer tokens and also produced a plan. This replay verifies that the running Windows backend honours thinking_budget_tokens without a template change. One plan completion says nothing about whether the act turn passes the gate. Requests and responses are in the evidence prefix's request-probes folder.

#### Corrected arithmetic fixture

The options rerun on the corrected contract kept seed 1911, the same 104a804 executable and the same 7,000-token budget. It improved from the starting five failures to one failure in eight tests, in six turns and 398 seconds. The uncorrected options run left all five starting failures. The new artifact compiled, but the gate still failed because -2^2 evaluated to 4 instead of -4. The generated parser applied its unary minus before exponentiation. The run still used three interrupted thinking turns, and two replies hit the edit guard before restructure was allowed. The correction improved the artifact on this seed, with higher elapsed time and no acceptance.

A second request-only replay used the corrected first-turn prompt, thinking off and otherwise identical sampling. Each arm added one instruction to the preceding arm. All three returned both plan markers.

| Added instruction | Total tokens | Seconds | Plan behavior |
|---|---:|---:|---|
| None | 280 | 9.66 | Proposed a whole-file rewrite and described unary precedence incorrectly |
| Name the permitted edit form and first target | 221 | 8.81 | Proposed a SYMBOL edit for evaluate, with ambiguous unary precedence |
| Also dry-run the two precedence examples | 328 | 9.45 | Proposed SYMBOL and correctly grouped -(2^2) and 2^(3^2) |

The visible plans had 160, 132 and 186 words respectively against the 150-word instruction. These probes cover plan quality and contract checking. See the arithmetic-options and scope-probes run directories.

#### Act replay and compact retention

The two plans that named a SYMBOL edit were handed to the real canvas agent on the corrected fixture. Both used the same binary, sampling, seed and compile/test gate. The loopback proxy injected the plan into the first act request only, matching the current one-use handoff. All generated edits were checked by the normal harness.

| Carried material | Act turns | Replay seconds | Gate |
|---|---:|---:|---|
| First act receives the edit-form plan | 4 | 134.04 | 7/8 |
| First act also receives the correct precedence dry-run | 4 | 149.08 | 7/8 |
| Correct dry-run also retained on repair turns | 6 | 200.20 | 7/8 |

The initial replies differed and both hit the edit cap. After that, the first two replays had byte-identical requests and replies through the final gate, which failed at -2^2. A third replay kept the dry-run conclusion on retries. Its first request was byte-identical to the second replay. It produced different repairs, staged three compile failures, and still failed the same precedence check. Repeating the correct conclusion did not make the parser put unary minus in the right place. The next experiment needs a check tied to the candidate's actual parser structure and gate feedback. These three replays are one case and one seed, with run data in the act-replay and act-replay-retained folders.

## How the blocks are used

* **Before a change** to a prompt, a view, a reply format, a gate or the policy: run the blocks it touches, keep the result as the baseline.
* **After a change**: the same blocks with the same seeds; keep the change only if the block improves and no block below it regresses.
* **Before a workflow run** on a new model or platform: blocks 0 to 5 must be at their bar; a run that needs a manual mend is evidence that a lower block is missing a fixture, and the mend is first turned into one.
* **Reporting**: `sub0squad scenario run` already archives per-element results; blocks 4 to 9 add their own commands to the same archive (`block run --block N`), so one table says which blocks are proven on which build.

## Build order (what to do next, smallest first)

1. Block 1: recorded-reply fixtures for every format, including cut-off replies, as a test and a scenario input.
2. Block 5: the model-based canvas test, then wire the agent to transactions (iteration brief, item 2).
3. Block 6: the parallel scenario on one platform (C++), with the speed-up and parity measurements; this is also the first use of the 35B's real aggregate throughput.
4. Blocks 7 and 9: the seam scenario and the golden decision logs.
5. `block run` and the archive table.
