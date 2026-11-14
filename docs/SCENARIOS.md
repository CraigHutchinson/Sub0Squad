# Capability scenarios: each workflow element proven in isolation, not by chance

Status: built 2026-11-11 (`sub0squad scenario`, `src/scenario.cpp`); fixtures validated without a model; no model results yet.

## Why

A work item that passes once may have passed by luck, and a run that passes end to end hides which element carried it. The first 35B runs showed the cost: an item
"accepted" by the stub, an interface detector that fired on noise, repair turns that overflowed the window. Each was found by watching a whole run for hours. A
scenario takes ONE element, starts it from a fixed state, lets the agent try it many times with different seeds, and judges every try with the trusted harness
gate. The result is a rate with a confidence bound, so a prompt, workflow or harness change can be compared with data, and a regression is visible at once.

## A scenario

* **A starting state**, built from the platform's reference files (the same ones the mock agent writes): the accepted outputs of the items it depends on, and
  for a repair scenario a broken draft of the item together with the gate's own failure text.
* **One agent invocation** (the same `llm-agent` job the daemon runs, with a trial-specific seed, the chosen treatment and repair turns).
* **A judge that is not the agent**: the harness gate over the result. A tester's tests must also be *correct*: the oracle runs them against the reference
  implementation, and a test file the reference fails does not count.

| element | starts from | passes when |
|---|---|---|
| `tester` | the contract only | the tests build against the stub, mostly fail on it, and all pass on the reference implementation |
| `developer-first` | the reference tests, nothing written | the item's gate passes |
| `developer-repair-syntax` | a draft with a stray token (the real slip: `();`) and the compiler's words | the gate passes; the share of lines changed is reported (patching, not rewriting) |
| `developer-repair-semantic` | a draft with a wrong result (the modulo defect) and the failing test's words | the gate passes |
| `app-first` | reference engine and reference app tests | the app's gate passes |
| `app-repair` | a draft app with a defect (wrong number format) | the gate passes |

Each runs on **every platform** (C++, Python, web): a prompt that only works on one is overfitted to it, which is the reason there are three.

## Not by chance

* **Many trials, distinct seeds** (`--trials`, default 10; seed per trial, so a run replays exactly).
* **The statistic is the 95 % Wilson lower bound**, not the rate: 10 of 10 gives 0.72, 9 of 10 gives 0.60, 8 of 10 gives 0.49. A scenario is *proven* when its lower
  bound reaches `--min-lower` (default 0.6, that is at least 9 of 10); otherwise it is reported as *not proven*, however good the rate looks.
* **Fixtures are validated first** (`scenario selftest`, also a ctest): from every starting state the reference passes the judge and the starting state does not. A
  scenario that passes with no agent, or that the reference fails, is a bad scenario and is rejected before a token is spent.
* **Same seeds across treatments**: comparing two prompts uses the same trial seeds, so the difference is the prompt.
* **Results are archived** (`scenario-results.json` with the build and engine) so the numbers belong to a commit.
* **Two results are compared with `sub0squad scenario compare A.json B.json`** (built): scenario by scenario, both pass counts with their 95 % Wilson intervals, the change
  in mean seconds and tokens, and a verdict. A difference is a `regression` or an `improvement` only when the two intervals do not overlap (9 of 10 against 8 of 10
  is "no evidence of change"); the command exits 1 on a regression, so a script can gate a change on it. With 10 trials only large changes are visible: that is
  the honest reading of 10 trials, and the reason to raise `--trials` before concluding a small one.

## Using it

```sh
sub0squad scenario list
sub0squad scenario selftest                       # no model; 18 fixtures, about a minute
sub0squad scenario run --engine qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=8192,slots=4 \
    --trials 10 --parallel 2 --only developer-repair-syntax --treatment baseline --out out/scenarios/baseline
```

`--only` takes an element, a platform or `platform/element`. `--parallel` is bounded by the engine's slots; on this host decode speed per slot falls as slots fill,
so measure a scenario at the parallelism you intend to run with. One CPU-heavy job at a time still holds: do not run the suite while a workflow run uses the engine.

## The iteration loop

1. Run the suite on a build; keep the results as the baseline.
2. Pick the least reliable element; read its failed trials (`out/.../work/<platform>-<element>-<n>/items/.../conversation.md`).
3. Change one thing (a prompt line, the view, the edit syntax, a card section, the ladder), run the same scenario with the same seeds, compare the lower bound.
4. Keep a change only if the bound improves on that element and does not drop on the others. Record it in `docs/INTERVENTIONS.md` with the numbers.

## Next scenarios (not built)

Status 2026-11-14: still not built as `scenario run` entries. Three of the six are, however, proven WITHOUT a model at the level below, by tests that drive the
real executor or engine with a fake engine or recorded outcomes: *interface gap* (`test_heal`: an undefined contract function is mended by the harness, one
`mend` decision, no `rearchitect`; `test_workflow` `interface-gap-rearchitects` for the ladder's rule), *reviseTests* (`test_workflow`
`repeated-failure-sends-tests-back`), *integrate* (`test_fork`: assembly by routed findings; `test_workflow` `v-ascent`). *decompose*, *read-then-edit* and
*length continuation* have unit tests (`test_workflow`, `test_agent`, `test_canvas_agent`) and no scenario. What a scenario adds, and these tests cannot, is a
pass RATE on a real model: that needs the fixtures below and a model run.

`interface-gap` (the gap is named, the rewritten card names every export, the next attempt passes), `decompose` (the architect's division is valid and covers every
file), `reviseTests` (a disputed tester fixes the wrong expectation and agrees with the reference), `read-then-edit` (a draft too large to show whole; reads used),
`integrate` (a coordinator's tests over merged parts), and a timeout/length scenario (the reply is cut off and continued). The knowledge store (docs/KNOWLEDGE_STORE.md)
is the larger task each of these is a slice of.
