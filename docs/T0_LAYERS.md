# T0 layers: the tier 0 proving ground as a tree (design, 2026-11-18)

Design only; nothing here is built. Backlog entries: [BACKLOG.md](BACKLOG.md), "Tier 0 layers". Background: [BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md)
("Model tiers and the tier 0 gate"), [RESUME.md](RESUME.md) (the gate on `101e19c`: FAIL, 22 of 24).

## 1. Why

Tier 0 is 24 flat cases (27 with the rewrite set), each run 3 times: 72 runs, about 2.0 hours on the 35B (`out/runs/t0-101e19c`, summary rows). Two problems.

* **It is slow for what it says.** 22 cases cost 877 s a pass between them; wrong-approach and skeleton cost 751 s and 806 s a run, 64 % of the gate, and used all
  5 attempts even in the runs that passed. Most of the 22 are one-line edits that a single scenario with several seeded defects exercises in one run
  (`bugs-6` already does: 3.0 defects a turn).
* **A flat failure does not say why.** On 2026-11-18 wrong-approach failed 2 of 3, and finding the causes took a day by hand: the whole-file rewrite running
  past the 3000-token reply (R1), continuation seams (fixed: line-boundary resume, `joinContinuation`), scope rules that never let the model stop rewriting
  (fixed: escalation), continuations re-emitting written functions. The hand decomposition (`write-engine`, `add-functions`, `replace-function`, set
  `rewrite`, `diagnostic=True`) is the model for this design: make it data, make it automatic.

So the tree is first-class. **Top cases** combine several issues and are the fast default. **Lower cases** are the base requirements each top case depends
on, down to **leaves** (one kind of work). A failing top case is explained by running its children; a release runs every layer.

## 2. The layer model

A node is a case in `CASES`. Two new optional keys, data only, no change to a case's plan or its `case_key`:

```python
"edit-mix": dict(..., requires=["fix-op", "fix-boundary", ...]),   # children: the bases this case combines or builds on
```

* `requires` lists children. A case may have several parents (`write-engine` serves `wrong-approach` and `skeleton`). The graph must be a DAG; `make_t0.py`
  asserts it, asserts every `requires` name is in `CASES`, and asserts every case is reachable from a top case (no orphan).
* `layer` is derived, never written: 0 for a node with no `requires`, else 1 + the highest child layer. Top = a node no other node requires.
* A node whose children are listed but which is also a useful stand-alone scenario stays one node: `requires` says what explains it, not what it contains.
* `tags=["smoke"]` replaces the `SMOKE` list; `SETS["smoke"]` is derived from the tag (same 8 cases, same order, so `@smoke` is unchanged).
* A composite is judged exactly as a leaf: tests pass, `builtByModel`, no leftovers (`quality_t0.judge`). `defects=n` keeps its meaning.

### 2.1 Mapping of all current cases

`layer` is a height, so tops sit at different heights (a top that only requires leaves is layer 1). Tops marked **T**; the three bold composites are new `CASES` entries
built by combining the existing seed transforms (`DEFECT_BANK`, `conflict`, `repeat_*`, ...) and the existing reference tests; they add no tests (section 6 on feasibility).

| layer | case | requires | note |
|---|---|---|---|
| 4 **T** | **edit-mix** | bugs-10, fix-distant, repeat-trig, fix-many, fix-two-bugs | new: bank defects 0,1,3,4,5 (not degrees, which repeat-trig seeds), the `^` grammar defect and the six trig sites, in one file |
| 2 **T** | **build-mix** | compile-and-logic, compile-warning, duplicate, repeat-rename | new: the file does not compile (typo, a duplicated function, ~20 calls to a missing name), a warning appears once it builds, a logic defect once warnings are clean |
| 2 **T** | **files-mix** | two-files, merge-conflict | new: a defect in main.cpp and one in calc.cpp, with conflict markers in calc.cpp |
| 2 **T** | add-functions (R2) | write-function, write-feature | existing, promoted to the gate: its four missing functions are write-function plus write-feature at once |
| 2 **T** | write-file | half-written, write-engine | unchanged; main.cpp over a stub |
| 0 **T** | write-tests | | the tester persona; its own kind of work, stays alone |
| 1 **T** | wrong-approach | replace-function, write-engine, add-functions | unchanged; the composite the day's work was about |
| 2 **T** | skeleton | write-engine, add-functions, (skeleton-fill, backlog) | unchanged; every method a stub |
| 3 | bugs-10 | bugs-6 | scaling: defects a turn |
| 2 | bugs-6 | bugs-3 | |
| 1 | bugs-3, fix-many, fix-two-bugs | fix-op, fix-boundary, fix-missing-check | 3, 5 and 2 defects; fix-many gives no hint in its card |
| 1 | compile-and-logic | compile-typo, fix-op | a typo hiding a logic defect |
| 1 | merge-conflict, two-files | fix-op | single-feature curveballs below files-mix |
| 1 | half-written | write-function | a file cut off part way |
| 0 | fix-op, fix-boundary, fix-distant, fix-missing-check | | single edit kinds |
| 0 | compile-typo, compile-warning, duplicate, repeat-trig, repeat-rename | | single broken-build and repeated-class kinds |
| 0 | write-function, write-feature | | SYMBOL replaces a stub; SYMBOL creates |
| 0 | write-engine (R1), replace-function (R3) | | the rewrite bases |

30 nodes: 8 tops (3 new), 22 lower; every one of today's 27 cases stays, so every plan, key and past run directory stays valid. A top is also a child when another
node requires it (`add-functions`, `write-engine`).

```
edit-mix ........... bugs-10 > bugs-6 > bugs-3 > fix-op, fix-boundary, fix-missing-check     fix-many, fix-two-bugs (> the same three)
                     fix-distant     repeat-trig
build-mix .......... compile-and-logic > compile-typo, fix-op     compile-warning     duplicate     repeat-rename
files-mix .......... two-files > fix-op     merge-conflict > fix-op
add-functions ...... write-function     write-feature
write-file ......... half-written > write-function     write-engine
write-tests
wrong-approach ..... replace-function     write-engine     add-functions
skeleton ........... write-engine     add-functions     (skeleton-fill, backlog)
```

### 2.2 Time (35B, from `t0-101e19c` run rows; R1 to R3 are estimates from the `wa-scope` summaries)

| set | cases | runs | rough wall time | note |
|---|---|---|---|---|
| today's gate | 24 | 72 | 2.0 h | 877 s a pass for the 22 plus 1 557 s for the two rewrites |
| smoke (unchanged) | 8 | 8 | 8 min | |
| quick, 1 run each | 8 tops | 8 | about 35 min while the rewrites fail (each 750 to 800 s); about 15 min once they pass in 1 or 2 attempts | the six others are about 60 to 140 s each, about 8 min together (estimates: the composites are unmeasured) |
| quick, 3 runs each | 8 tops | 24 | about 1.7 h today; about 50 min once the rewrites pass | the flat gate's 3-run bar, on the tops only |
| release, tops 3 runs, every lower node 1 run | 8 + 22 | 24 + 22 | about 2.2 h today | lower nodes once: about 12 min for the 20 cheap ones, about 15 min for R1 and R3 |
| release, every node 3 runs (`--min-runs-low 3`) | 30 | 90 | about 3.1 h | the thorough option, for a milestone |
| dig-down of one failing top | its subtree | at most 3 a child | rewrite: about 1 h; edit-mix: about 15 min | only the failing branch |

The saving is honest but modest while the rewrites fail: they dominate. The real quick-mode gain is that the 21 cheap cases become 5 composites and stand-alones, about 14 min a pass
down to about 8, and that failures arrive with their explanation attached.

### 2.3 Case review before T1 rework

Use a case to answer one question about a named workflow. Record the triggering state, the capability needed, the frozen outcome, and the evidence that ties it to a real workflow or a plausible failure mechanism. When a test bundles several mechanisms, use isolated controls to find which ones cause the failure, then retain the composite as an integration check. Archive a redundant test only when its replacement covers the same failure and boundary. An unobserved case with a credible mechanism can stay at lower cadence. A speculative case without a credible mechanism belongs in the backlog until it can justify measurement.

| T0 family | Trigger and what it proves | Near-term treatment |
| --- | --- | --- |
| `fix-op`, `fix-boundary`, `fix-distant`, `fix-missing-check` | A complete source file fails behavior tests. Localize and repair a wrong operation, boundary, distant grammar cause, or absent check. | Keep as small controls. Reuse existing precedence assertions when developing T1 grammar checks. |
| `compile-typo`, `compile-warning`, `duplicate`, `compile-and-logic` | A build failure or warning prevents useful behavior feedback. Repair the compile failure, then follow the test evidence for any remaining defect. | Keep. Run the relevant controls when changing build or repair feedback. |
| `write-function`, `write-feature`, `write-file`, `write-tests` | A stub, missing feature, missing file, or missing tests with a known contract. Proves bounded construction and the separate tester role. | Keep as construction controls. Use the named role and contract to judge each output. |
| `merge-conflict`, `two-files` | Competing source edits or coordinated edits across files. | Keep distinct integration controls. Run on changes to those paths or in the full gate. |
| `fix-two-bugs`, `fix-many`, `bugs-3/6/10`, `repeat-trig`, `repeat-rename` | Multiple fixes or one repeated defect class. Distinguishes a valid repair from throughput and batching of repairs. | Use as scale or cadence probes. Their old passes give a reference point. Additional synthetic permutations need a receiving workflow. |
| `half-written` | Pre-existing source fails compilation at EOF. Finishing only the open function can leave declarations and entry points missing, causing misleading follow-on diagnostics. | Park further tuning until it blocks a downstream workflow. Preserve the failed evidence, fixture and full-gate membership. |
| `wrong-approach`, `skeleton` | Strategy selection, whole-engine construction and long-reply handling can all contribute. Failures of smaller rewrite cases have been observed. | Keep composite verdicts visible. Debug the smallest demonstrated mechanism first and revisit the composites once it works. |
| Own-reply continuation diagnostic | The engine reports an interrupted reply with a usable transaction prefix. Success requires actual continuation and the original gate. | Keep separate from source repair. The live assistant-prefill case passed and the tail fallback has scripted coverage. |

The historical flat gate at `101e19c` passed 22 of 24 cases three times each. Only `wrong-approach` and `skeleton` failed. Those results describe that build. These dispositions control where to spend development time. The release gate and its verdict keep their current rules. [Historical T0 evidence](BLOCK_PROVING_GROUNDS.md#tier-0-iteration-2026-11-18-stopping-early-the-failure-breakdown-the-half-written-ab), [continuation evidence](T0_ITERATIVE_SANDBOX.md#dedicated-reply-continuation).

### 2.4 First T1 rework slice

The T1 consumer is the scientific calculator built by one worker. It needs to accept expressions with grouping and function arguments, preserve power precedence and associativity, and give the same answers through the CLI. Starting from the archived 447-line failed T1 artifact, four model-free replays used its original 22-test plan, the same CLI source and the current production C++ gate:

| Artifact | Failures |
| --- | ---: |
| Archived baseline | 10/22 |
| Restore delimiter recognition in the token-to-parser contract | 3/22, all power-related |
| Restore the power grammar only | 7/22, all grouping or function-related |
| Both repairs | 0/22 |

Evidence lives at `out/runs/t1-diagnostics/20261118T205335Z-65f73352/summary.json`, with per-arm source hashes and gate logs. This received the causal split through the real gate using human-authored control edits. The original live T1 result remains a failure.

The next bounded model experiment starts with those two isolated failure groups. Use an artifact that retains the relevant contract defect while the other group passes. Keep the expected behavior public, keep the accepted patch private, and require the original tests for each arm. First establish the repair with the target region readable, then repeat the same defect with the region folded. Two live short-window `fix-op` runs already read a folded region and passed, so the open question is whether that behavior transfers to a T1-sized artifact and survives a subsequent revision. Track requested regions, repeated reads and edit targets. Add a named leaf only where existing tests and profiles cannot exercise that distinction. Receive both defects together through the original 22 tests before spending on another whole-project model run. A full gated T1 run still needs a current T0 PASS. [Archived T1 failure and fold evidence](BACKLOG.md), [T1 gate policy](BLOCK_PROVING_GROUNDS.md#model-tiers-and-the-tier-0-gate-2026-11-17).

Run a single T1-derived repair in the sandbox by passing the source directory from the selected replay arm. For the delimiter repair, use the arm in which only power was fixed:

```powershell
python -B validation/tiers/t0_sandbox.py run --t1-source out/runs/t1-diagnostics/20261118T205335Z-65f73352/power/items/app/attempt-1/out/src --seed 4101 --timeout 240
```

The `--t1-source` option freezes the replay's CLI and the original calculator header and permits edits to `src/calc.cpp`. Before inference it builds the supplied seed and requires a behavioral failure from the original 22-test suite. It keeps the same tests for the worker and the independent checker. These runs are repair diagnostics outside the gated whole-project T1 result.

**First live delimiter diagnostic.** The initial runner setup was invalid because a custom C++ scaffold suppressed the harness header. The corrected runner includes the frozen header and preflights the seed. The corrected trial `20261118T210043Z-19dc5e578658` started with 7 of 22 failures and ended with the same count. The initial view folded the relevant token accessor even at the 65,536-token server profile. The worker used broad reads and edited the already working power grammar. This is a localization failure on a valid seed. A focused prompt arm can ask the worker to group actual errors and inspect shared token handling without naming the repair.

**Focused localization prompt.** Trial `20261118T210323Z-54fcb796adaa` kept the same seed, tests and limits. Asking the worker to group actual failures and trace shared token handling still led to an unrelated power rewrite and a compile failure. The trial used nine turns and 4,217 recorded output tokens versus five turns and 502 in the first valid trial. This is one matched-seed observation. The next receiving experiment should vary access to the relevant source region independently of prompt wording so it can distinguish localization from faulty edits.

**Matched visibility control.** On the same build and seed, the folded arm `20261118T214305Z-115d167b648a` remained 7/22 failing after five turns. The visible arm `20261118T213931Z-393de05aa8ba` received the unmodified contents of `Parser::peek` in the initial view and failed after changing power parsing. The gate reported an undeclared parser function. The intervention changes only visibility via `--t1-visible Parser::peek`. This comparison establishes that showing the source in the initial request did not resolve this seed's failure. It leaves both arms outside T1 promotion evidence.

**Whole-project first attempts with two output limits (2026-11-18).** The campaign at `out/runs/t1-first-attempt/20261118T221614Z-d4d06afa` used the frozen calculator plan and the archived T1 canvas worker command with explicit seeds and prompt caching disabled. Each seed started in a fresh directory. It ran worker attempt 1 and the official C++ gate directly, so the failure policy could not start a second worker attempt. These are forced diagnostics outside admission. The manifest records the executable and plan hashes.

| Seed | Output tokens allowed | Opening reply | Final blocker |
| --- | ---: | --- | --- |
| 15101 | 3,000 | Cut off and continued | CLI missed standard-library declarations for input and exceptions |
| 15102 | 3,000 | Cut off and continued | Parser called a private method. CLI also redefined its result type and entry function |
| 15103 | 3,000 | Cut off and continued | CLI redeclared the screen border variable |
| 15101 | 6,000 | Finished without a cutoff | Parser defined and called `check_eol()` without declaring it |

All four opening prompts listed all 22 failures from the starter stubs. All four generated drafts hit compilation errors before the behavior tests ran, so this campaign cannot tell whether the old delimiter or power defects recur. The 6,000-token arm isolated a compile blocker that can occur without truncation. The next targeted test can isolate whether the worker can repair a single reported compile error without rewriting unrelated code.

### Failure-mode diagnostics

The named failure-modes set adds three isolated checks after the full run, without changing its 26 official plans. The build-definition conflict seed is rejected by the compiler. The data-dependent runtime cycle builds and terminates before reporting a test assertion. The factorial boundary seed returns infinity for a finite result. The focused test rejected that result when the older size-only assertion accepted it. Every repaired reference passed the same checks. Control evidence is under out/runs/t0-fairness-preflight-20261120/failure-mode-controls-v2.

### 2.5 Optimizer, QA and DRY exercises

The worker receives a change request, the public contract and the evidence a maintainer would normally see. The checker holds the private fixtures, threshold calibration and known controls outside the worker prompt. Start each exercise as a named T0 diagnostic and record the behavior and quality verdicts independently.

| Exercise | Public task | Independent receiving check |
| --- | --- | --- |
| Optimizer | Reduce repeated work on a named path while preserving output and error behavior. | Run the existing behavior suite and count the relevant operations, allocations or reads on fixed inputs. Show that the seed exceeds the stated budget and a known-good implementation stays within it. Keep timing as a separate host measurement. |
| QA | Add tests for a named behavior and its boundary or error cases. | Run the submitted tests against the unmodified implementation and one or more frozen defects. Require a clean pass on the reference and an assertion failure from the behavior under test on a defect. A compile error, crash or missing test counts as a separate failure. |
| DRY and C++ review | Move a named duplicated invariant to one source of truth, or repair a stated review finding, while preserving public behavior. | Test every affected consumer. Use a structural check only when the requirement can be stated independently of the reference patch. Record reviewer findings with the applicable rule and source location. Keep line counts, similarity and repetition as supporting diagnostics. |

Use the existing repeated-conversion case to calibrate the DRY review and the archived test-writing mutation probe to calibrate the QA check. A useful next optimizer fixture has a deterministic work count at the caller boundary and a known reference that reduces it. Keep the patch private and permit a different implementation with the same behavior and measured result.

For a best-practice exercise, the review could ask for the factorial limit to have a descriptive name and a short explanation of the numerical boundary. Verify that 170 factorial is finite and accurate, that 171 is rejected, and that the existing invalid-input behavior is preserved. The current expression comparing the value with 1e306 also accepts infinity. A finite-value probe closes that gap in a separate diagnostic. A style-only case needs an independent criterion that can reject a behavior-green seed and accept a conforming edit. Until it exists, keep the named constant and its explanation as a review exercise.

For skeleton fills and folded functions, consider short comments that state an invariant, assumption or call-order constraint that a reader needs. In a parser, a useful comment might explain why a call consumes input before recursion. Put a comment needed to understand a folded block where the ordinary folded view shows it, near the visible signature. Check it against the code and a representative test. Comment count, verbosity and summaries of obvious statements provide weak evidence. Run a future paired experiment with the same source and read interface to see whether these comments help the worker choose which blocks to expand and preserve the invariant while editing.

Evaluate mechanical formatting recovery separately from model judgment. A C++ prototype can apply the project's clang-format configuration to touched ranges in a scratch candidate, show the formatting diff and rerun the same checks before accepting it. Preserve the original and formatted revisions and their provenance. Evaluate clang-tidy findings and semantic fix-its separately from indentation and alignment. Verify idempotence, ranges and behavior, and use each language's own formatter and rules where indentation affects execution.

A criterion joins the release gate after positive and negative controls establish that it checks the stated contract, the experiment measures false rejects on alternative valid implementations, and a held-out task shows that the prompt and checker transfer. Keep PP, TG and first-pass ordinals in the same report. They measure how the worker got to the result. They carry no vote on whether a particular code structure meets the contract.

### 2.6 Split planning and handoff

The plan aims for about 150 words on the current step. Expand it when the next edit needs more detail. Record dependencies, missing evidence, and work that can be planned in a later turn or separate scope. Use the reply token budget to bound transport and the receiving view budget to retain source evidence alongside the carried plan.

Start with a serial A/B on the same T0 fixtures, seeds, budgets and owner checks. The control uses ordinary plan and act. The split arm declares one checkable slice, its prerequisite evidence and a list of remaining slices. Use the first slice in the same worker and record the tested revision, result, and remaining plan. A later turn receives that handoff and can continue or revise it. The full work item stays open until the unchanged acceptance criteria pass. Require evidence that the receiving worker can recover the scope and that checks still describe the revision being edited. Record first-pass and in-budget acceptance, PP, TG and handoff errors. A future separate-agent arm can assign non-overlapping scopes and test merge and conflict behavior once the serial handoff works.

### 2.7 Edit address shootout

A SEARCH/REPLACE transaction requires the worker to copy the old text before sending the replacement. The model-free sandbox measures alternative addresses on a small source change with moved blocks, duplicate text and a copy typo. It validates emitted edits using the production canvas reply checker.

| Address | Qwen reply tokens | Behavior and trade-off |
| --- | ---: | --- |
| SEARCH/REPLACE | 33 | Follows an intact unique text match after a move. Refuses duplicate text and a copying error. Larger replacements repeat the old and new bodies. |
| LINES | 23 | Changes only the selected lines and needs no old text. In the replay, the old numeric address edited the wrong function after the functions moved. |
| SYMBOL | 47 | Named function replacement found the same function after it moved and ignored duplicate lines in other functions. Requires the full function text and an unambiguous symbol. |
| Block and content hash | 31 | The sandbox found the same original line after a move and distinguished duplicate text in another block. It refused a changed line and duplicates inside the selected block. |
| Revision-guarded range | 26 | Refused relocation and an added duplicate elsewhere because the file changed. Simple to validate, with a fresh read as the recovery path. |
| Scoped view handle | 24 | Bound a short handle to a known view, block and content digest. Relocated the intended line and refused a changed or ambiguous target, an invalid view and an unknown handle. |

The native checker verified all 22 emitted transactions. The guarded, hash and handle arms are sandbox resolvers. They lower accepted addresses to normal LINES edits, and the refusal controls verify that rejected addresses never reach the native decoder. The fixture models a single editor with a stale view after an earlier edit. It makes no assumption about concurrent acceptance. The Sub0Canvas shared source-addressing brief keeps the actor-view and rebase contract open for Tier 2.

Start the receiving experiment with the scoped view handle. It preserves the exact block and underlying digest and costs one reply token more than the raw line address on this sample. The canvas issues the handle with its view and retains the binding. The worker only copies it. Confirm the binding and identify a unique exact match in the intended block before applying. If the handle is stale and the target cannot be resolved uniquely, return a bounded refresh. A short start/end context pair is another candidate for a multi-line span. It can survive unrelated movement and needs the same ambiguity and stale-content checks.

Keep zero wrong-target writes and zero out-of-scope writes as receiving criteria. Start with exact output comparison, then add insertions, deletions, overlap, partial folded reads and a behavior check on a second task. On a paired live trial, measure reply tokens and prompt tokens together, including issued handles and refreshes. Record first-candidate acceptance, number of reads and retries, refusal reasons, recovery success and changed scope. Track portability and the number of affected tests separately. Keep timing as system-specific evidence.

Run the sandbox with python -B validation/tiers/edit_syntax_shootout.py --exe PATH --out NEW_DIRECTORY. Optionally pass the local tokenizer endpoint with --tokenizer for measured reply tokens. Evidence is under out/runs/t0-edit-syntax-shootout-views-20261120.

## 3. Modes

All three judge the run directories, as `gate_t0.py` does today: a run counts when its `case_key`, build and engine match; stale runs are listed, not counted.
That is also the reuse rule (section 3.4).

| | quick | release (complete) | dig-down |
|---|---|---|---|
| nodes run | top layer (`--tag smoke` for the leaf subset) | every node of every layer | the failing branch of a failing top, recursively |
| runs | `--min-runs` (default 1; 3 for a pre-release check) | tops `--min-runs` (3), others `--min-runs-low` (1) | per child up to `--dig-runs` (default 3), stopping on a failure |
| writes | `quick.md/json` | `gate.md/json` | `dig-<top>.md/json` and a `Dig-down` section in the other two |
| verdict | `QUICK PASS/FAIL/INCOMPLETE`: says nothing about the lower layers | `PASS` only if every node passes at its run count | per top: the lowest failing node(s), or `interaction` |
| exit | 0/1/2 as today | 0/1/2 as today | 0 root found, 1 nothing found (flaky only), 2 budget spent |

### 3.1 Where it lives

* `make_t0.py`: the tree data, `TOPS`, `LAYER[name]`, `CHILDREN[name]`, `--list tree`. No behaviour change to `plan_for`.
* `gate_t0.py`: gains `--mode quick|release` (judging only, never runs a model) and a tree renderer. `--set` stays; `--set smoke` equals `--mode quick --tag smoke`.
  The per-run judging (`judge_run`, `case_key`, quality) is untouched. The judged result becomes a function `judge_node(name, build, engine)` returning
  `{state, runs, passed, firstAttempt, ...}`, which the report, the roll-up and the dig-down all call.
* `run_t0.sh` stays the only thing that runs a case (`START=n run_t0.sh <repo> <engine> <repeats> <case...>`). It gains `@quick`, `@release`, `@tops`
  beside `@smoke` (names resolved by `make_t0.py --list <set>`), and writes the same `out/runs/t0/<case>/run-N`.
* New `validation/tiers/dig_t0.py <repo> --engine qwen35 [--top NAME ...] [--budget-min 90] [--dig-runs 3]`: the only driver that decides what to run next.
  It calls `gate_t0.judge_node`, then shells out to `run_t0.sh` with `START` set to the next free run number (so no run is overwritten), then judges again.
  Python for the loop (the roll-up logic is data-heavy), bash for the runs (it owns `lib.sh`). `run_t0.sh @release` is just a loop of the same calls in
  layer order, lowest first, so a lower failure is known before the top runs would have been explained.
* Implicit dig-down: `run_t0.sh ... @quick --dig` runs `dig_t0.py` for each failing top when the quick pass ends; the default is off in CI, on interactively.

### 3.2 How verdicts roll up

Each node has a **self state** from its own counted runs (`PASS`, `FAIL`, `INCOMPLETE`, `NOT RUN`). Nothing infers a child from a parent or the reverse in
quick mode: the quick verdict is the tops' alone. A top PASS does not claim its children pass; it claims the combination worked on this build.

* **Release PASS**: every node PASS at its run count. A failing lower node fails the release even when its top passes (the combination hid a defect the base
  case shows). `INCOMPLETE` if any node has too few counted runs and none failed.
* **Dig result per failing node** `N`, once its children are judged:
  * some child FAIL: recurse into that child; `N` is **explained by** the failing descendants. The report names the *lowest failing nodes* as root causes.
  * no child FAIL (all PASS or none left): `N` is an **interaction failure** (size, or two requirements together). It is a root cause itself, the evidence
    that a base case is missing (a backlog entry), and the report says so.
  * a leaf FAIL: a root cause; no further descent.
* **Flaky**: a node whose runs passed but whose first-attempt rate is under 1/2 (wrong-approach passes today used 5 of 5 attempts) is `WEAK`. `WEAK` does not fail
  a gate. It starts a dig only with `--dig-weak`, because the cheap leaf runs are where the attempts-to-pass cost is attributed.

### 3.3 Dig-down procedure and stop rules

```
dig(top):  queue = [top]; findings = []
  node = pop; judge it (counted runs on this build and engine)
  if node is PASS or (WEAK and not --dig-weak): continue
  children = CHILDREN[node] sorted by layer descending, then cost ascending (cheap leaves first)
  for child in children:
      if child has >= dig-runs counted runs, or any counted FAIL: use that (reuse, no run)
      else run it, 1 run at a time up to dig-runs, STOP at its first FAIL (a failing child needs no more evidence)
         or at dig-runs PASS (early pass: two of two is enough when --early-pass)
      push child if FAIL or WEAK
  if no child FAIL: findings += interaction(node) else findings += lowest failing descendants
```

Stop rules: wall budget (`--budget-min`, default 90; the loop checks between runs, never kills one), run budget (`--max-runs`, default 30), one failing child
per level when `--first-cause` (depth-first, cheapest root cause only), and a node is visited once however many parents require it. Siblings do not run in
parallel: one engine, `-np 4` is reserved for the model, not for the harness.

### 3.4 Run directories and reuse

* Run directories do not move: `out/runs/t0/<case>/run-N`, case-keyed, so a leaf run made for any reason (a smoke, an earlier dig, a release) is shared by every parent.
* Reuse is judging, not bookkeeping: a run counts for a node if its `case_key`, build (`state/run-info.json`) and engine match, exactly today's rule. A change to the build
  or to a seed makes it stale; nothing is cached anywhere else. `dig_t0.py` never re-runs a child that is already judged enough; `--fresh` forces new run numbers.
* `START` numbering: the driver reads the highest `run-N` for the case and starts at N+1. Experiments that must not mix with the gate set `T0_OUT=out/runs/wa-scope-v5`
  (a small `run_t0.sh` change: the hard-coded `out/runs/t0` becomes `${T0_OUT:-...}`; the `wa-scope-v*` directories were this by hand).
* The summary rows (`summary.md`) stay a progress log; the report is built from the run directories.

## 4. Report format

`gate.md` (release), `quick.md` and `dig-<top>.md` share one renderer. The tree replaces the flat table; the columns are today's, plus a roll-up and the
findings above it. Sketch (invented figures, current column set):

```
# Tier 0 release: FAIL
Build 0.0.0-190-gabc1234, engine qwen35. Tops 3 runs, others 1. 1 root cause, 1 interaction. Dig-down ran 7 runs (31 min).

## Findings
1. ROOT   replace-function (layer 0)  FAIL 0/3  scope rules: 2 replies stopped for rewrite, escalation not reached     -> wrong-approach, skeleton? (no)
2. INTERACTION  skeleton (layer 3): children write-engine PASS 3/3, add-functions PASS 3/3; the skeleton itself fails: 241/81 lines, 3 stubs left.
   Missing base case: skeleton-fill (BACKLOG).

## Tree
| layer | case | state | passed | first attempt | turns | out tokens | quality | not counted |
| 3 | wrong-approach | FAIL | 1/3 | 0/3 | 94 | 66617 | run-2: 329/168 lines ... | |
| 0 |   replace-function | FAIL | 0/3 | ... | | | | |
| 0 |   write-engine | PASS | 3/3 | ... |
| 0 |   add-functions | PASS | 3/3 | ... |
| 3 | edit-mix | PASS | 3/3 | ... |
| 2 |   bugs-6 ... | (not run in quick) |
```

Rules: tops in `TOPS` order; children indented under their first parent, and listed once with "(also under ...)" afterwards; a node not run shows `NOT RUN`, never a blank; states
`PASS / FAIL / WEAK / INCOMPLETE / NOT RUN`; the findings (lowest failing nodes, interactions, WEAK nodes) come first because that is what a person acts on. `gate.json`
(`t0-gate-2`) carries the same tree (`nodes[name] = {layer, requires, state, ...}`, `findings`) and the existing per-case fields, so a reader of `t0-gate-1` keeps working
on `cases`. The quick and dig files state in the title that they are not the gate, as `smoke.md` does.

## 5. Migration, with no behaviour change until switched

1. **Data only** (`make_t0.py`), **done 2026-11-18** (`REQUIRES`, `LAYER`, `TOPS`, `--list tree`; the 24 gate plans checked byte-identical; the composites'
   edges wait for step 3): `requires` on the existing cases, `layer`/`CHILDREN`/`TOPS` derived, the DAG asserts, `--list tree`. `GATE`, `SETS`, `SMOKE` and
   `diagnostic` untouched; `plan_for`, `case_key` and every plan unchanged (check: all 27 `case_key` hashes before and after). The rewrite cases stay `diagnostic`.
2. **Report only**, implemented (`gate_t0.py`): render the tree in `gate.md` under the flat table, same verdict and exit codes. Rewrite cases appear as `NOT IN GATE` children.
3. **Composites staged**, implemented: the three composites are available in `@calibration` and shadow tier sets, outside the 24-case release gate. Each is calibrated without a model
   (`make_t0.py --check`: it must fail in the way it says: every defect present, the right tests red), then 3 runs each on the current build. Calibration passes when a
   composite's pass rate is no worse than the lowest of its children's; a composite that fails while all its children pass is itself the first interaction finding.
4. **Driver**, implemented: `dig_t0.py` reads recorded evidence and plans the next useful cases. `run_t0.sh --plan` lists `@quick`, `@release`, `@tops` or `@calibration` without running a model. Set `T0_OUT` for an isolated cohort. The shadow `@release` writes `release.json` and leaves `gate.json` for the existing release gate.
5. **Switch**: one commit changes `GATE` (tops) and adds `RELEASE` (all nodes): `gate.md` becomes the release verdict, `quick.md` the default fast check, `@smoke` keeps
   working. `diagnostic` is replaced by "a lower node": the rewrite set joins the release. Update `BLOCK_PROVING_GROUNDS.md` and the `run_t0.sh` header in the same commit.
   The pre-push and CI paths that cite the smoke set are unchanged.

**What the release verdict becomes.** Today: PASS = every one of 24 cases passed N of N. After: PASS = every node of the tree passed at its run count (tops 3, lower 1),
which is a stricter statement over a larger set (the 24 cases, R1 to R3 and the 3 composites) at a similar cost. Tier 1 and above keep requiring a tier 0 PASS on the
same build and engine; `run_tier.sh` reads `gate.json` and must read the `verdict` of the *release* file (a `quick` PASS is refused, as a smoke PASS is). Until
the rewrites pass, the release verdict is FAIL either way, and now carries the dig-down that says which node.

## 6. Risks and open questions

* **Composite feasibility.** A seed with defects in several functions was only built for `bugs-N`; `build-mix` needs defects that mask each other in order (the warning
  appears only after the typo is fixed). Each composite must be checked with `--check` and a model-free "apply the reference" run before it counts. Fall back to
  keeping the flat cases as tops where a composite cannot be built.
* **A composite may be harder than its parts.** Several kinds in one reply can fail where each passes alone. That is a finding (an interaction), not a flaw, but it means a quick
  PASS rate may initially be lower than today's gate; calibrate (step 3) before switching.
* **Confidence per run drops.** 3 runs of 8 tops check less independent behaviour than 3 runs of 22 cases. Release keeps the leaves for that reason (once each); the
  open question is whether release leaves should be 1 run or 2 (about 25 min more).
* **Shared children skew the budget.** `write-engine` serves three parents; a dig visits it once. Fine for cost, but a shared child's flake can look like three failures:
  the report names it once with its parents.
* **Reuse across builds.** Runs on the previous build are stale by the existing rule, so a dig after every commit re-runs its children. Intended; `--accept-build PREFIX`
  could allow a documented waiver for a docs-only commit, not proposed here.
* **Dig-down in CI.** Wall time is open-ended; it is interactive by default and bounded by `--budget-min` where it runs unattended.
* **Weak-pass threshold** (first-attempt rate under 1/2) is a guess; wrong-approach's passing runs all used 5 of 5 attempts, which suggests attempts-to-pass should itself be a
  gated number once the rewrites are stable.
* **Open for the owner:** is a quick PASS allowed to unblock tier 1 on a build that already has a release PASS from an earlier commit (my view: no, same build only, as today)?
