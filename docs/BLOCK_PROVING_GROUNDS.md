# Block proving grounds: isolated base concepts that build up to the whole

Status: design, 2026-11-12 (owner direction: parallelism and throughput are a core principle, satisfied by folding, forks and the canvas; each is a base building block with its
own proving ground, among other isolated concepts that build up to the whole). Complements `docs/BUILDING_BLOCKS.md` (the order of work), `docs/SCENARIOS.md` (the per-element
capability suite) and `docs/CANVAS.md`. Nothing here is built yet except where a row says so.

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
| 1 | **Reply formats**: whole file, edit (search, lines), transaction, read, with cut-off handling | recorded replies and generated replies; apply and compare with the intended result | apply(reply) equals intended text on 100 % of well-formed cases; every malformed case gives a named error and changes nothing | 0 | partly built (`applyEdits` tests) |
| 2 | **Gates and judges**: precheck, build, tests, oracle, attribution of the first failure | the reference solution passes, the starting state fails, per platform | 18 of 18 fixtures valid (all platforms and elements) | none | built: `scenario selftest` |
| 3 | **Single-section capability**: tester, developer first, repair (syntax, semantic), app | the six scenarios on three platforms, judged by block 2 | Wilson 95 % lower bound at least 0.6 over 10 trials, per element and platform | 0, 1, 2 | fixtures built; no model results yet |
| 4 | **Folded views and reads**: outline correctness; reads used and bounded | outline unit tests on C++, JavaScript, Python samples; a scenario where the draft is too large to show whole | fold regions correct on the samples; a read-then-edit scenario passes at the block 3 bar; read-only replies bounded | 1 | outline built and unit-tested; scenario not built |
| 5 | **Canvas transactions**: revisioned sections, all-or-nothing, rebase, revert | a model-based test: random operation sequences against an in-memory model; the same repair scenarios answered as transactions | text equals the model after every step on 1,000 random cases; repair scenarios at the block 3 bar with a changed-lines share well below a rewrite's | 1, 4 | core built and unit-tested; model-based test and wiring not built |
| 6 | **Parallel sections (fork)**: a skeleton, one section per worker on idle slots, merged | the same file generated sequentially and in parallel from the same skeleton, same seeds, same gate | **speed-up**: wall time to a passing file at least 1.8x at 4 slots (*target*); **parity**: pass rate within 10 points of sequential; **utilisation**: idle-slot time under 25 % while sections are pending; **conflicts**: zero lost edits | 3, 5 | designed (`WORK_GRAPH.md` 23, `CANVAS.md`); not built |
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
