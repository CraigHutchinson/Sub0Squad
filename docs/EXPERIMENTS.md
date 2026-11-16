# Experiments: the proving sandbox on a local llama.cpp server

The squad has no release and no production code. The work now is to **prove** each part of it on a real local model, **optimise** it, and **A/B-verify** every change
before it becomes a default. This page holds the method, the host and engine facts, the tools, what has been run, and the queue of experiments in priority order.
What is built is in [STATUS.md](STATUS.md); the decisions of the clean-slate sandbox (no hand-written plan, no reference inside the harness) are in
[CLEAN_SLATE.md](CLEAN_SLATE.md); what each number means is in [METRICS.md](METRICS.md).

## 1. Method

* **Clean slate.** A real-model run starts from the request in words. A blueprint plan is a mock fixture for machinery tests, never the start of a measured run.
  The reference implementation lives in `validation/` and judges a finished run from outside (`validation/grade_run.py`); the harness never reads it.
* **One variable per comparison.** An arm differs from its control in one switch (a treatment, a stance, an executor, a process parameter). Same build, same
  engine profile, same seeds.
* **Smallest proof first.** A recorded reply or fixture (no model) → one item replayed on the model (`replay`, `scenario run`) → a whole project → a 20-seed
  campaign. Each step is only taken when the step below it says the arm is worth it.
* **Intervals, not anecdotes.** Rates are reported with their 95 % Wilson interval; a difference counts only when the intervals do not overlap
  (`scenario compare` applies this rule). Five seeds is a rough figure; twenty is the campaign.
* **Cost in tokens and engine ticks**, not minutes: wall time moves with load ([METRICS.md](METRICS.md) section 3).
* **New behaviour is off by default** and becomes the default only on an A/B result, recorded here.
* **Archive every run** (`sub0squad archive --label NAME`; the campaign driver archives each run) with the build id it ran on.

## 2. Host and engine

**The bar model** is Qwen3.6-35B-A3B (Q4_K_M, mixture of experts, about 3B active): a worker reliable enough that a failure points at the harness. The 9B
is a stress target for harness hardening only.

**The engine** is our own `llama-server`, started by the owner as a contained user service (a memory cap and no swap, so an out-of-memory kill takes the engine,
not the session). The squad never starts, stops or restarts it.

```sh
# the profile every experiment below assumes; adjust to the engine that is actually running
ENGINE=qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=8192,slots=4,seed=42
# engine side: -c 32768 -np 4 (four slots of 8192), --metrics (tick counters for the campaign), thinking off
```

**Host facts** (the Linux dev host, measured 2026-11-09/10): Ryzen 9 8945HX, 16 cores on two CCDs, AVX-512, 30 GB RAM, **one DRAM channel** (32 to 35 GB/s
read, flat from 1 to 32 threads), no usable GPU. The 35B holds about 23 GB. Consequences:

| Figure | Value | Meaning for experiments |
|---|---|---|
| Decode, one stream, 35B | 5 to 8 tok/s (about half its bandwidth ceiling) | an output token costs 30 to 50 prompt tokens' time: **output is what to save** |
| Decode, aggregate over 4 slots | about 12 tok/s (1.7x of one) | parallel slots pay; plan for width |
| Prefill, cold | 5 to 160 tok/s depending on model | prompt text is cheap when cached |
| Prompt cache, same slot | a repeated 3k-token prefix costs 0.6 s instead of 19 s | keep shared text first; slot affinity is built |
| Slot window | 8192 tokens | a planner reply plus a repair turn overflowed it (CLEAN_SLATE section 6.1) |

## 3. Tools

| Question | Tool | Notes |
|---|---|---|
| Does a reply format, view or mend work at all? | `sub0squad block run [--block N]`, `ctest` | no model; recorded replies in `tests/data/replies/` |
| One element, many trials, an interval | `sub0squad scenario run --engine $ENGINE --trials N --out DIR`, then `scenario compare A.json B.json` | per-element pass rate with Wilson bounds; arms by `--executor`, `--treatment`, `--stance` |
| One failing item of an archived run, re-run under arms | `sub0squad replay WORKFLOW ITEM [--fork] --seeds 1,2,3 --engine $ENGINE` | the paired comparison on a real failure |
| One item under several treatments | `sub0squad optimize-job` | gated without publishing |
| The planner alone | `sub0squad llm-planner --workflow-dir DIR --request "..." --engine $ENGINE` | valid-plan rate, repair turns, plan lint |
| A whole project, many seeds | `scripts/campaign.py run --name NAME --runs 20` (`--mock` for a machinery smoke test) | resumable; `campaign.jsonl`, `campaign-report.md`, an archive per run |
| Independent verdict on a finished run | `validation/grade_run.py ARCHIVE` | reference acceptance on the product; the squad's tests against the reference |

`scripts/start-campaign.sh` boots the engine service and the campaign service in order (owner's machine).

## 4. A campaign

Twenty seeds of one pinned build (the driver copies the binary into the campaign directory), run one after another on one daemon so seeds do not distort each
other. Per run, one line in `campaign.jsonl`: result, wall time, items accepted, attempts, decisions by kind, mends, ledger tokens and the fork attempts' own
tokens, the best test result reached on the failing item (`bestTests`), the first-failure signature and its class, and the engine tick counters before and after.
`campaign-report.md` is rewritten after each run: success rate with its interval, the table, the class histogram.

Budget: a run is 30 to 190 minutes on this host, so a campaign is 10 to 60 hours of engine time. It runs unattended as a user service with its own memory cap,
waits for free memory before each run, and stops after three harness errors in a row.

What the result decides: a rate near 0 with failures in P and A means the harness and the plan are the work; 20 to 80 % with a spread means variance is large
and comparisons need more seeds; mostly S and M means the model is the limit at this task size. Any class seen in three or more seeds becomes a catalogue row in
[INTERVENTIONS.md](INTERVENTIONS.md), a fixture and a mend or prompt change before the next campaign.

## 5. Failure classes (a heuristic first cause; confirmed by reading the run)

| class | meaning | typical signature |
|---|---|---|
| **P** plan or contract | the plan, contract or card was wrong, thin or contradictory | interface mismatch; a test importing a name no contract states |
| **D** decomposition | the division was invalid or over-reached | a fragment that adds work the parent never had; 19 member sections |
| **A** assembly | the parts do not fit together | link errors, `-Wunused`, a section never accepted |
| **S** implementation | one function is wrong | tests failing with a value or an exception |
| **T** tests | the tests are wrong or too few | a tester failing; a dispute upheld; a literal that contradicts the contract |
| **H** harness | the instrument misled or failed | no failure record, a crash, wrong accounting, mislabelled gate text |
| **M** model limit | the model cannot do this at this size | repeated identical replies |
| **E** environment | memory, engine, disk | engine stopped answering, shed jobs |
| **B** budget | the wall or token budget ended a converging run | timeouts |

The driver assigns A, P, S, T and B from the gate text and the persona; H and M, and the split between P and A, need a reader.

## 6. What has run on the real model

**Whole projects** (35B, the C++ calculator, executor `fork`, all from a hand-written plan; none finished). Each run's findings became catalogue rows, built and
proven without a model; whether each mend holds on the model is unknown until the next run.

| Run | Main cause | Rows |
|---|---|---|
| v8 | a tester's item divided into a whole project | 44 to 46 |
| v9 | the fork worked mechanically; sections queued on one slot; every round re-ran every section | 47 to 52 |
| v10 | the merged file lacked the contract's entry point; `-Wunused` spent attempts | 59 to 64 |
| v11 | attempt 1 passed 20 of 22 and the ladder threw the near pass away; class members never defined | 70 to 73 |
| v12 | the entry point refused three times for one compile error; mends spent on its consequences | 74, 75 |
| baseline seeds 1000 to 1002 | stuck at `dev-engine` with 2 of 4 items accepted: seed 1000 by wrong test literals (section 7), 1001 and 1002 by a 19-section class split that ran out of wall time (class D) | 76, 77 |

**The planner from a bare request** (no blueprint): 3 of 5 valid on the latest protocol (cpp-6); failures were JSON structure in nested cards and stubs that
did not compile under `-Werror`; every plan had width 2 on 4 slots. Details in [CLEAN_SLATE.md](CLEAN_SLATE.md) section 7. No model-made plan has been built
into a project yet.

**Clean-slate runs of 2026-11-16** (remote session; from [RESUME.md](RESUME.md), not re-run here):

| Run | What it showed |
|---|---|
| `llm-planner --prose` (prose plan, then form) | 1 valid plan of 4: an early, small sample of E4 (the two-step arm is opt-in) |
| w-000020, a supplied plan through `do` | 12 of 12 black-box cases from `validation/grade_blackbox.py`, the first graded pass |
| w-000022, a planned plan | the developer's app failed (NaN result) |
| w-000023, the plan of w-000022 supplied | **succeeded** through the whole workflow (items, the dispute path `reviseTests`, integration); the grader: product 12 of 12, 5 literals audited, 0 wrong |

The remote session reported 48 of 48 tests passing at `96a17c9`. **All of these used `write a calculator in C++`, not the scientific request** (it had
slipped; [BACKLOG.md](BACKLOG.md) "Now"): they prove the machinery on a four-function calculator. The full run from the request, planner included, has not
passed in one go.

## 7. The worker probe (2026-11-15): why the stance experiment is first

A strong worker (Haiku, no tools, no knowledge of the system) was given the real `dev-engine` card of seed 1000 and the real gate. It wrote a complete engine
that compiled cleanly and passed 8 of 11 tests, and said unprompted that four test literals contradict the contract's own arithmetic (`2 + 3 * 4` expected 7,
`5 % 2` expected 0, ...). It refused to bend the engine to them. On the next turn it worked out that the gate's expected/actual labels were swapped (since fixed,
row 76). Its debrief:

1. The tests are treated as the specification and nobody checks them; the ladder spent ten attempts on an unsatisfiable target.
2. **There is no channel to say "your test is wrong"**: the reply format allows edit blocks only, so its objection was prose the parser discards.
3. Without the arithmetic it would have changed the engine until the tests passed: the failure a small model cannot recover from.
4. Claims need the gate, not the reply: it reported "no warnings" without compiling.

So the 35B worker is told it is a small piece of a squad and that it must make the tests pass, and it has nowhere to object. Experiment E1 tests whether
telling it it is a dedicated specialist with standing to contest and to ask, and giving it a block to do so, changes what it does.

## 8. The queue

Ordered by expected value against cost. Status is one of: *ready* (can run on the engine now), *build* (needs code first), *design*.

| # | Experiment | Hypothesis | Arms | Primary measure | Status |
|---|---|---|---|---|---|
| **E1** | **Specialist stance and the contest channel** | A worker told it owns a dedicated, specialised assignment, may contest a wrong expectation and may delegate, raises the seed-1000 defects instead of bending the code, without losing pass rate on a correct card | `--stance baseline` against `--stance specialist` | on the faulty card: concern raised (and on which literals), code bent to a wrong literal (graded against `validation/`); on a correct card: pass rate and tokens | **build done, ready to run**: section 9 |
| E2 | KV memory per token and the window plan | A per-role window (planning wide and narrow, sections narrow and wide) removes the planner's overflow without losing width | `-np 4 -c 32768` against `-np 2 -c 32768` for the planner | valid-plan rate, overflow count; KV bytes per token from the engine log | ready (measurement first) |
| E3 | Plan lint fed into the planner's repair turn | The planner widens its plan when told its fill and the slot goal | lint recorded only against lint in the repair message | plan width and fill on 4 slots; valid-plan rate | build |
| E4 | Prose plan, then per-item format calls | Dividing planning from syntax raises the first-try valid rate | single reply against two-step | valid-first-try, repair edits, tokens per plan | partly built: `llm-planner --prose` (prose then whole-plan form; per-item calls still to build), [CLEAN_SLATE.md](CLEAN_SLATE.md) section 8 |
| E5 | Grammar or JSON schema on the format step | llama-server's constrained decoding removes format failures without hurting content | no grammar against `json_schema` on the format call | format failures; plan quality by the same judge | build (after E4) |
| E6 | Slot save and restore | Restoring a saved KV slot saves the prefill of a resumed item | cold prompt against `/slots/{id}?action=restore` | prefill seconds saved; disk and memory per slot | ready (measurement first; check our build's flags) |
| E7 | Executor comparison | `fork` buys quality per token over `session` and `canvas` on the same plan | `session`, `canvas`, `fork` | quality per token, ticks to the accepted whole ([METRICS.md](METRICS.md) section 5) | ready (needs a model-made plan that builds) |
| E8 | The aggregate decode curve | Concurrent sections decode faster in aggregate than one | `scenario run --parallel 1, 2, 4` | sum of `engine.decode_tps` | ready |
| E9 | Spec-check first turn | A forced "what I assume, what looks wrong" turn catches card defects early | off against on | useful-question rate ([CONTEXT_AND_PLANNING.md](CONTEXT_AND_PLANNING.md) section 6) | build (after E1) |
| E10 | Clean-slate baseline campaign | The whole process from a bare request | 20 seeds | success rate with interval; class histogram; grader verdicts | build (planner process end to end) |

## 9. E1 in detail: the specialist stance

**What changes** (built 2026-11-16; off by default; [PROMPTS.md](PROMPTS.md) has the exact text):

* The system block of every worker (session, canvas and forked sections) states the assignment as a dedicated, specialised role the worker owns: it is the
  expert for that piece, it is trusted to decide how, the card and the tests are claims to check rather than orders, and it may contest them, ask, or hand back
  work that is not its own.
* A `=== CONCERN: <kind> ===` block (kinds: `wrong`, `contradictory`, `underspecified`, `missing`, `too-big`, `better-idea`) may come before or instead of
  the edits. The harness records every concern in the attempt (`concerns.md`, the trace, the agent's `agent.json` counts) and never counts it as stray prose.
  Routing a concern to the owner of the expectation (the keeper of [CONTEXT_AND_PLANNING.md](CONTEXT_AND_PLANNING.md) section 5) is not built yet: E1
  measures whether workers *raise* the right concerns before that machinery is worth building.
* `--stance baseline|specialist` on `do`, `project submit`, `scenario run` and the agent commands; the stance is recorded in the workflow spec and the scenario results.

**Cases**, on one item each, 5 seeds per arm first, 20 if the five differ:

1. **Faulty expectation** (the reason it exists): `dev-engine` with seed 1000's wrong-literal tests (`validation/fixtures/seed1000-test-engine-wrong-literals.cpp`).
   Good outcome: a `wrong` or `contradictory` concern naming the bad literals, and an engine that still agrees with the reference (`grade_run.py`). Bad: the
   engine bent to a wrong literal.
2. **Correct card** (the control for cost): the same item with the reference-agreeing tests. Good outcome: pass rate and tokens no worse than baseline, and few
   concerns (a concern on a correct card is noise and is counted as such).
3. **A thin card** (optional): a contract with the argument order removed. Good outcome: an `underspecified` concern.

```sh
# WORKFLOW: a workflow (restored from campaign seed 1000's archive) whose test-engine was accepted with the wrong literals. Each replay writes its
# variants under <sandbox>/replays/dev-engine-<time>/s<seed>/ and prints a table with a concerns column; the workflow itself is untouched.
TESTS=tests/eval.test.cpp     # the dev-engine gate's test file: check gate.tests in the plan
# case 1, faulty tests (as the tester wrote them)
sub0squad replay WORKFLOW dev-engine --seeds 1,2,3,4,5 --engine $ENGINE --repair-turns 2 --stance baseline
sub0squad replay WORKFLOW dev-engine --seeds 1,2,3,4,5 --engine $ENGINE --repair-turns 2 --stance specialist
# case 2, correct tests: the reference-agreeing file put in place of the tester's
sub0squad replay WORKFLOW dev-engine --seeds 1,2,3,4,5 --engine $ENGINE --repair-turns 2 --stance baseline   --accepted test-engine:$TESTS=validation/reference/calculator-cpp/$TESTS
sub0squad replay WORKFLOW dev-engine --seeds 1,2,3,4,5 --engine $ENGINE --repair-turns 2 --stance specialist --accepted test-engine:$TESTS=validation/reference/calculator-cpp/$TESTS
# who raised what, and did the engine stay right
cat <sandbox>/replays/dev-engine-*/s*/items/dev-engine/attempt-1/concerns.md
validation/grade_run.py <variant directory>
```

The replay runs the session executor (one worker, whole file); `--fork` runs forked sections with the same stance. Case 2 needs the reference's test file
to match the plan's interface: true for the blueprint plan seed 1000 ran on, not for a model-made plan.

**Decision rule.** Adopt `specialist` as the default if case 1 raises the defect in clearly more seeds than baseline and bends the engine in no more, and case 2
shows no loss of pass rate outside the interval and at most about 10 % more tokens. Record the result here and in [PROMPTS.md](PROMPTS.md).
