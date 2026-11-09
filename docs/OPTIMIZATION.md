# Optimising the workflow for a weak model

Status: 2026-11-09. Context: the first real-model runs ([SQUAD_UX_AND_HARNESS.md](SQUAD_UX_AND_HARNESS.md) section 8) failed because small-model
work packages were too big or their inputs wrong. This document is the design of the response: **every job is a unit that can be improved in
isolation, and a failing job climbs a ladder of increasingly structural remedies, each of which counts as an attempt.**

## 1. The escalation ladder (per work item)

N is `maxLocalAttempts` (default 2). The decision policy (`RulePolicy`, `rules-1`) records its inputs and reasons for every step.

| Rung | Trigger | What happens | Counts as an attempt |
|---|---|---|---|
| retry | local attempts < N | another attempt with the previous gate excerpt as its only extra context | yes |
| **decompose** | N local attempts failed, item splittable (several files or complexity of at least 2), division depth < 2 | a `decompose` job asks the model **to decide the division**: it proposes 2 to 4 smaller work packages (a plan-2 fragment, test-first). The daemon validates the result as data and **merges it into the plan**: the item is replaced, fragment roots inherit its dependencies, dependants wait for the fragment's leaves, the files must cover the original's, ids become `parent.child`. The children run through the same pipeline and may be divided again (depth limit 2) | yes |
| **reviseTests** (send back to the producer) | no division (declined, invalid, or not splittable) and the item is graded by tests another item wrote | the producing tester item is **disputed**: its acceptance is voided, a `dispute.txt` carrying the failing tests' output becomes its feedback, it re-runs, and the developer item then continues against the revised tests | yes (for the item, and the tester's re-run is its own attempt) |
| **rearchitect** | still failing | a dedicated `rearchitect` job rewrites the item's **work card** (shorter, concrete examples, the specific pitfalls) into `card-override.json`; the next attempt uses it | yes |
| escalate | every local rung tried | one attempt on the `frontier-stub` tier (nothing is sent anywhere) | yes |
| fail | `maxTotalAttempts` (default 8) reached, or frontier off | the workflow fails with the reason | |

Each rung is tried once per item; a rejected division (invalid plan) counts as a failed attempt and the ladder moves on. The engine stays a
pure, level-triggered function (a restart derives the same next step); the new action kinds are `amendPlan` and `writeDispute`, executed by the daemon.

## 2. Isolation and focus: the per-job optimiser

`sub0squad optimize-job --workflow-dir D --item I --engine E --treatments baseline,index,planfirst --rounds R` runs **one item with its inputs fixed**
(the accepted outputs of its dependencies stay as they are) under each **prompt treatment**, and judges every attempt through the same trusted gate
without publishing anything. It reports, per treatment, the pass count, failing tests, prompt and completion tokens and seconds, and writes
`optimization.json`. It is a normal job, so it runs under the daemon with a wall budget and is cancellable.

| Treatment | Input change |
|---|---|
| `baseline` | the work card plus the full accepted test file |
| `index` | the card plus only the *names* of the tests (about a tenth of the prompt) |
| `planfirst` | a focusing step: first a 120-word plan (structure and tricky cases), then the code written against that plan |

Treatments are data (`WorkflowSpec.treatment`, `--treatment`), so a winner becomes the project default. The same loop applies at every stage: tester
items, developer items, the architect's own prompts. The ladder's `rearchitect` is the in-flight version of the same idea (rewrite the input after
failure); the optimiser is the offline version (try input variants before committing).


## 3. Sampling, thinking, and the agent checking its own work

- **Thinking is off** (`enable_thinking=false`), a deliberate choice for speed on CPU (about 8 tokens/s). It is a profile option (`thinking=1`) and has **not** been measured yet.
- **Sampling was wrong, now fixed.** Until now agents sent temperature 0.2 and left the rest at the server's defaults (this snap: temperature 1.0, top_p 0.95,
  top_k 20, min_p 0.05). Qwen's published recommendation is 0.7 / top_p 0.8 / top_k 20 for non-thinking and 0.6 / 0.95 / 20 for thinking; those are the
  profile defaults now (`temp=`, `top_p=`, `top_k=` override). Evidence that 0.2 hurt: the retry in the first real run reproduced the same 34 failures.
  (Qwen3's card is the source of the numbers; Qwen3.6's own card was not checked.)
- **The agent now verifies before it says done** (`--repair-turns`, default 2 in workflows): after writing its files it runs the item's tests (the same gate,
  without publishing), and on failure continues the **same conversation** with the failure excerpt, up to N more turns. If a repair turn returns files
  identical to the previous turn it stops (**stagnation**) and the item goes back to the ladder.

### Native thinking versus iterating externally

| | Native thinking | External iteration (self-check loop) |
|---|---|---|
| Ground truth | none: the model reasons about code it cannot run, so a wrong belief (`-2^2` is `4`) is re-derived confidently | the real tests: exact failing cases and messages |
| Cost | many extra decode tokens before the answer, paid on every attempt, at 8 tok/s minutes each | one extra full generation per failed turn, but only when a check fails; the prompt grows with the replayed conversation |
| Strength | better first drafts, plans, spotting inconsistencies between spec parts | converges on whatever the tests can express; model-independent |
| Weakness | cannot know what the tests will say; may reason itself into the wrong answer | only as good as the tests; can stagnate (same mistake resent) |

They are complementary, and the data so far says the external loop is what moves the needle: see below. Native thinking is a candidate for the **planning** step
(`planfirst` is a cheap, non-thinking version of it), not yet measured.

## 4. Results (2026-11-09, snap Qwen 3.6 35B-A3B, isolated `engine` item, correct tests fixed)

Isolation: the item's inputs were fixed (a correct 12-test file), every attempt went through the same gate, nothing was published. Two rounds per
treatment, temperature 0.2, one shot, no repair loop:

| Treatment | Failing tests per round | Mean failing | Prompt tokens | Seconds |
|---|---|---|---|---|
| `baseline` (card + whole test file) | 5, 11 | 8.0 | 1,303 | 335 |
| `index` (card + test names only) | 4, 8 | 6.0 | 482 (63 % fewer) | 325 |
| `planfirst` (a 120-word plan, then code) | 2, 2 | 2.0 | 2,955 | 407 |
| `planfirst` + a hand-rewritten card (two pitfalls and examples added; a stand-in for the rearchitect job) | 7, 1 | 4.0 | 3,357 | 420 |

Every run failed at least one test. The two recurring failures were `-2 ^ 2` (precedence of unary minus) and `log2` read as `log` plus `2`. With two rounds per
configuration, **run-to-run variance (1 to 11 failing) is larger than most differences**; only `index` cutting prompt tokens by 63 % at no clear loss, and
`planfirst` being consistent in its first two rounds, are suggestive, and neither is established.

Verify-and-repair, same item, recommended sampling, `planfirst`, up to 3 repair turns (one run, 36 minutes):

| Turn | Prompt tokens | Completion tokens | Failing |
|---|---|---|---|
| 0 | 1,614 | 3,265 | 7 (`PI`/`E` undefined, `pow` NaN, `-2^2`) |
| 1 | 5,037 | 3,180 | **1** (`-2^2`) |
| 2 | 8,292 | 3,190 | 1, files **identical** to turn 1 |
| 3 | 11,557 | 3,190 | 1, identical again |

One repair turn fixed six of seven failures with the gate's own excerpt as the only feedback: external iteration works and the feedback is good. The last
failure was not fixed in two further turns because the model resent the same file (the stagnation rule now stops after the first repeat, saving about 13 minutes
here). Costs to note: prompt tokens grow with the replayed conversation (a bounded re-prompt with only the latest code and failure would be flatter), and one
item took 36 minutes. **The item still did not pass**; the remaining failure is a stubborn belief about precedence, exactly where division (a smaller work package
with its own focused tests) or the rearchitect card's explicit pitfall is meant to take over.

### What is verified and what is not

Verified by tests: the ladder as a pure function (retry, decompose with plan merge and dependency rewiring, rejected division, send back to the producer with
the producer's acceptance voided and re-run, rearchitect, escalate, frontier off), and the whole ladder through the daemon with mock agents
(`test_project`, scenario `projladder`). Measured on the real model: the table above. **Not verified live:** the model-decided division produced one answer
(`llm-architect --mode decompose`): a plausible split into a parser and an evaluator, rejected by the validator because one proposed item had no files, i.e. the
safety net worked and the model's JSON discipline did not; a full workflow with the ladder and repair loop on the real model has not been run; native thinking
has not been measured.
