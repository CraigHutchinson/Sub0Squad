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
2. **Report only** (`gate_t0.py`): render the tree in `gate.md` under the flat table, same verdict and exit codes. Rewrite cases appear as `NOT IN GATE` children.
3. **Composites staged**: add the three new composites with `staged=True` (excluded from `GATE`, `SETS`, tops until switched). Each is calibrated without a model
   (`make_t0.py --check`: it must fail in the way it says: every defect present, the right tests red), then 3 runs each on the current build. Calibration passes when a
   composite's pass rate is no worse than the lowest of its children's; a composite that fails while all its children pass is itself the first interaction finding.
4. **Driver**: `dig_t0.py` and the `run_t0.sh` names (`@quick`, `@release`, `@tops`, `T0_OUT`). Dry-run mode (`--plan`) prints what it would run.
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
