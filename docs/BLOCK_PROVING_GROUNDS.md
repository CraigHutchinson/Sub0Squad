# Block proving grounds: isolated base concepts that build up to the whole

Status: design, 2026-11-12 (owner direction: parallelism and throughput are a core principle, satisfied by folding, forks and the canvas; each is a base building block with its
own proving ground, among other isolated concepts that build up to the whole). Complements `docs/BUILDING_BLOCKS.md` (the order of work), `docs/SCENARIOS.md` (the per-element
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
| 1 | **Reply formats**: whole file, edit (search, lines), transaction, read, with cut-off handling | recorded replies and generated replies; apply and compare with the intended result | apply(reply) equals intended text on 100 % of well-formed cases; every malformed case gives a named error and changes nothing | 0 | **built**: 24 recorded-reply cases (`tests/data/replies/`), every form and every refusal, cut-off replies and continuations; `test_blocks`, `block run --block 1` |
| 2 | **Gates and judges**: precheck, build, tests, oracle, attribution of the first failure | the reference solution passes, the starting state fails, per platform | 18 of 18 fixtures valid (all platforms and elements) | none | built: `scenario selftest` |
| 3 | **Single-section capability**: tester, developer first, repair (syntax, semantic), app | the six scenarios on three platforms, judged by block 2 | Wilson 95 % lower bound at least 0.6 over 10 trials, per element and platform | 0, 1, 2 | fixtures built; no model results yet |
| 4 | **Folded views and reads**: outline correctness; reads used and bounded | outline unit tests on C++, JavaScript, Python samples; a scenario where the draft is too large to show whole | fold regions correct on the samples; a read-then-edit scenario passes at the block 3 bar; read-only replies bounded | 1 | outline, section view, path-only read and bounded reads built and tested (`test_blocks`, `test_canvas_agent`); the read-then-edit scenario on a model is not built |
| 5 | **Canvas transactions**: revisioned sections, all-or-nothing, rebase, revert | a model-based test: random operation sequences against an in-memory model; the same repair scenarios answered as transactions | text equals the model after every step on 1,000 random cases; repair scenarios at the block 3 bar with a changed-lines share well below a rewrite's | 1, 4 | **model-based test built** (1,000 random cases equal the model, `canvasModelCheck`); **wired** (`--executor canvas`, `test_canvas_agent`); the repair scenarios as transactions on a model: not run |
| 6 | **Parallel sections (fork)**: a skeleton, one section per worker on idle slots, merged | the same file generated sequentially and in parallel from the same skeleton, same seeds, same gate | **speed-up**: wall time to a passing file at least 1.8x at 4 slots (*target*); **parity**: pass rate within 10 points of sequential; **utilisation**: idle-slot time under 25 % while sections are pending; **conflicts**: zero lost edits | 3, 5 | **built** (`src/fork.cpp`, `llm-fork`, `--executor fork`); the harness is proven model-free (parity: identical files; 0 conflicts; 0 lost edits; `test_fork`, `test_blocks`); speed-up, parity of pass rate and utilisation on the 35B: not measured |
| 7 | **Section integration**: the coordinator merges sections and finds seams | sections with a planted seam defect (a signature mismatch between two sections) | the integrator names the seam in the first report on at least 9 of 10 seeded defects; a clean merge raises no false seam | 5, 6 | not built |
| 8 | **Division**: the architect splits an item into testable parts | a set of items with known good divisions; the validator and the division-completion rules judge | every proposed division valid and covering at least 8 of 10; invalid ones are named, never merged | 2, 3 | validator partly built (`completeDivision`) |
| 9 | **Decision policy and the ladder**: widen before retrying, early abort, bounds | recorded failure sequences replayed through the policy | each failure signature leads to its intended rung (golden decisions); no rung is entered twice | 1, 2 | policy unit tests built; golden logs not recorded |
| 10 | **Process as data**: stages, executors, the standard process equal to the legacy engine | golden action logs of the legacy engine | the standard process reproduces the goldens exactly | 9 | designed (`PROCESS_DESIGN.md`) |
| 11 | **The whole on a platform**: the calculator end to end | the plan on a platform, frontier off, the 35B | zero manual mends over 3 runs in a row; per-item numbers recorded | 3 to 10 | first runs in progress (`w-000002`), not passing |
| 12 | **The whole at scale**: the knowledge store | the slices of `KNOWLEDGE_STORE_SCOPE.md` | the slice gates and properties | 11 | designed |

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
| 1 reply formats | `runReplyFixtures` over `tests/data/replies/*.case`: the reply (or a cut-off reply and its continuations) is applied as the agent applies it and compared with the intended files; a malformed reply must be refused with a named error and change nothing | 24 of 24 |
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
   `sub0squad do "write a calculator in C++" --agent llm --engine ENGINE --executor fork --max-tokens 1500`.

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
