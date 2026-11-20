# Clean slate: a sandbox project with no pre-built plan and no reference inside the harness

Status: 2026-11-16. Owner direction of 2026-11-15. Built: the validation-suite split, the planner role with its repair loop (repair as edits, JSON mends), the plan
checks and the `planned` process. Not built: plan review, expectation QA, the loop stage, the clean-slate campaign. The experiments that follow from this page
are E2 to E5 and E10 of [EXPERIMENTS.md](EXPERIMENTS.md).

## 1. The principle

Everything the harness runs on must be something a **real project** could supply: a request in words, answers to clarifying questions, and what the squad itself produces. Two things we have been leaning on are not that:

* **A hand-written plan** (`docs/calculator-cpp.plan.json`, the mock planner's blueprints). Every real run so far started from one. The planner role, the one that turns a bare request into items, contracts and tests, has never run. Failures that begin in the plan (underspecified or wrong contracts, an unusable decomposition) were fixed in the blueprint by us and so never happened to the squad.
* **A reference implementation** (the mock agent's correct files; the C++ oracle). A real project has none. An oracle in the gate made the process look better than it is and leaked the answer to the tester (see [EXPERIMENTS.md](EXPERIMENTS.md) section 7).

The harness therefore contains neither as a dependency. Blueprints stay only as **mock-agent fixtures for fast machinery tests**; they never start a real-model run.

## 2. The split: harness versus validation suite

| | harness (what a real project gets) | validation suite (`validation/`, only we have it) |
|---|---|---|
| input | the request in words, owner answers | the sandbox spec: a bare prompt, never shown as a plan |
| plan | written by the squad's planner (a model), reviewed by the squad | none; the plan is an OUTPUT to be graded |
| tests | written by testers, checked by expectation QA and independent readings | a reference acceptance suite the squad never sees |
| judgement of success | the squad's own gates and QA | `validation/grade_run.py`: reference acceptance on the product, tests-versus-reference on the squad's tests, plan quality review |
| reference implementation | none | `validation/reference/` |

Built now: the oracle is out of the harness; `validation/grade_run.py` grades an archived run (tests-vs-reference and product-vs-reference); the seed 1000 wrong-literal tests are a fixture in `validation/fixtures/`. First reading of the archived seed 1000 run: the accepted engine tests agree with the reference, the accepted app tests do not (the `--screen` frame's padding), and the product fails the reference acceptance.

## 3. The clean-slate sandbox project

* **The prompt:** `write a scientific calculator in C++` (and `... as a web page`, and the Python variant), unchanged, **with no blueprint**. Triage must not find one: the known-blueprint path is disabled for real-model runs; the request goes to the planning stage.
* **Planning stage** (exists in part: `--process clarified` asks what the request leaves open once): a **planner** job (the 35B, frontier off) turns request plus answers into a plan-2 document. The plan is validated by `Plan::fromJson` and the plan checks; a plan the validator refuses goes back to the planner with the validator's messages (a repair turn), the same loop a developer has with a compiler.
* **Plan review** (a distinct role, ideally a second sampling or a smaller prompt): checks the plan for the failures the campaign found: contracts that contradict their own examples, a test card that names no argument order, a division finer than the model can finish in the wall budget (seeds 1001 and 1002: 19 member sections), items with no acceptance, missing files. Findings are issues raised to the planner (CONCERN channel of `docs/CONTEXT_AND_PLANNING.md`), answered with an amendment or a reason.
* **Expectation QA** (backlog item of the same date): the explicit gate step that judges tests against the contract, by independent reading (a second tester, or the QA persona working the contract's own examples), not against a reference.
* **Golden examples in the contract:** the planner is asked to put worked examples into the contract (`2 + 3 * 4 = 14`). They are checkable data a real project's author can write, and the cheapest available check on a tester's literals.
* **Grading:** after each run `validation/grade_run.py` produces the independent verdicts; the campaign report adds them next to the squad's own result. The squad never sees the reference; the harness never reads it.


### End-to-end receiving case for the scientific calculator

Track the next exploratory product run in [Sub0Squad #7](https://github.com/CraigHutchinson/Sub0Squad/issues/7). Use the exact request
“write a c++ scientific calculator application, make it fancy”. The goal is to receive the work through the actual Squad roles from a fresh workspace.

- A planner agent authors the plan from the request. Keep the supplied-plan and blueprint paths inactive. Record what the planner asks or assumes about
  the application interface and presentation.
- Tester agents author the application tests and the harness records their provenance. Developers implement against the accepted contracts and tests.
  Integration assembles and checks the result. Preserve each handoff, repair and terminal verdict. Treat a missing role or a bypassed handoff as evidence.
- Grade scientific behavior with independent checks after the build. Use grade_blackbox.py with the scientific group if the project exposes the supported
  command-line interface. Record the delivered interaction and presentation separately from arithmetic correctness, with evidence from the actual build.
- Use a fresh session and the planned process to exercise the local planner, or the clarified process when testing explicit scope questions. Keep a single
  declared choice fixed for the first trial. Record phase PP and TG, failed checks, the first cause and the integrated output. Keep this exploratory run
  separate from T0/T1 admission.

The active T0 cohort finishes before considering the shared-engine launch of this product run. The prior clean-slate prompt and supplied-plan runs remain
separate controls.

## 4. What is needed (build order)

1. **The planner role**: a prompt, the plan-2 schema as the reply format, validator-driven repair, the plan written to the workflow (`plan.json`), journaled like any job; no blueprint lookup for real-model runs (`--blueprint` stays an explicit opt-in for machinery tests).
2. **Plan checks** in the validator for the mechanical failures already seen (section count against budget; every tester card states the check argument order or the harness lint removes the need; every contract has at least N examples; every developer item depends on a tester).
3. **The plan review role** and the issue loop (needs the CONCERN channel; a rule-based keeper first).
4. **Expectation QA** as a gate step.
5. **Campaign driver**: `--no-blueprint` as the default, the grader run after every run, the report columns for plan validity, plan repairs, review findings and the reference verdicts.
6. **A clean-slate baseline campaign** (20 seeds): the number the whole project is judged by. The earlier baseline (3 runs with a hand-written plan) is kept as the contrast: `out/campaign/baseline-prefix`.

## 5. Risks

* A 35B planner may produce unusable plans most of the time; that is a finding, not a reason to bring the blueprint back. The levers are the prompt, the repair loop, the review and the plan checks.
* Without a reference in the loop, wrong tests are caught only by the squad's own checks; the first campaigns may fail more often than the blueprint runs. They measure the real process.
* The grader's reference is itself ours and could be wrong; its tests are the same ones the mock agent passes, run in CI, and a disagreement is read by a person before it is believed.

## 6. Refinements (owner direction, 2026-11-15, while the first probes ran)

**6.1 A larger context.** The first real planner probes ran into the slot, not the model: the engine runs `-c 32768 -np 4`, so each slot holds 8192 tokens, and a planner reply of 5,000 tokens plus the prompt and one repair turn overflowed it (probe 2). Today's answer is to make the planner small (a reply cap, a size check, no repeated refused reply). The structural answer is to let roles have different windows: a planning role is one long conversation, a section writer is many short ones. Options, to be measured on this host (the 35B uses about 23 of 30 GB; the KV cost per token has not been measured, only guessed, so the first step is to read it from the engine's start log):
* fewer, larger slots for the planning phases (`-np 2` at 16384 each, or one 32768 slot) with the width restored for the execution phases; the engine is restarted between phases by the campaign driver, or two engine services share the memory budget (the second a small model);
* a larger total (`-c 65536`) if the KV memory allows, with the same four slots at 16384;
* a per-role window in the engine profile (`ctx=` per route), so a role is only given what its context honestly needs.
The planner and every other role must read the window from the profile and budget against it (the planner already counts the previous reply; the rest do their own trimming).

**6.2 The planner knows the slot goal.** A plan is a shape: width, depth, item size. The planner is told what the engine can run at once (`slots`, the window per slot, the measured tokens per second per request) and that the goal is to keep every slot busy: so independent deliverables are independent items with no shared files, the critical path is short, and no item is bigger than one slot's window and the wall budget. `plan-lint` (width, critical path, rough minutes) already measures this; its numbers go back to the planner as part of the repair messages, not only as warnings.

**6.3 Planning in phases, continuously.** One planning step is the wrong unit. The shape that fits how development actually goes:
* **Architecture and design phase** (once, at the start, then whenever the design changes): L0 intent, L1 architecture and rationale, L2 contracts (`docs/CONTEXT_AND_PLANNING.md` section 3). The output is the design document, the interfaces (the plan's scaffold: headers) and the decomposition into deliverables; it needs the long window, so it runs wide-context and narrow.
* **Execution planning** (many times): from the current design and the current state, the next items: their cards, tests, order and width for the slots available NOW. It runs as development proceeds: after each accepted item or a failure the plan keeper may add, split, merge or reorder items (`Plan::extended`, the contract amendment of `docs/PROCESS_DESIGN.md`), and every change goes through the consolidation step (change log, regenerate what is derivable, mark dependents stale).
* The two phases are one continuous loop with different cadence and different windows: design changes are rare and expensive, execution planning is frequent and cheap. The process is data (`planned` today is plan, build, integrate): the target is `design -> plan next wave -> build -> integrate -> review -> plan next wave ...`, a stage list with a loop condition, run until the acceptance is met or the budget is.
* This is also where wrong tests and thin contracts get corrected: a concern raised against the design reopens the design phase for that entry only.

**Order of work.** (1) finish and record the first probes with the single-phase planner (running now); (2) measure KV memory and decide the window plan; (3) pass `slots` and the window to the planner and feed `plan-lint` into its repair messages; (4) split the planner into design and wave-planning roles on the `planned` process; (5) the plan review and expectation QA; (6) the loop stage kind.

## 7. First probe (the planner on the real 35B-A3B, 2026-11-15)

Method: `sub0squad llm-planner` with the request "write a calculator in C++", 4 slots at 8192 tokens each, at most 4 turns. No blueprint, no reference.

| Batch | Seeds | Valid | Notes |
|---|---|---|---|
| probes 1 to 4 (cpp, web, py) | single runs | cpp: 1 of 3 valid after fixes to the harness; web 1 of 1; py 1 of 1 | probe 1 timed out (loaded machine), probe 2 hit the token cap and the slot size |
| cpp-5 | 1, 2, 3 | 2 of 3 | seed 1 copied a stand-in message from the repair history |
| cpp-6 (repair protocol fixed: a long refused reply is dropped and the request is re-asked with the reasons) | 1 to 5 | 3 of 5 (seed 3 in 1 turn, seed 5 in 3, seed 2 in 4) | seeds 1 and 4 never got valid JSON out of the `card` of the CLI tester item in 4 turns |

What the repair loop caught (all real, all mechanical): a missing stub for `src/main.cpp`, a stub of more than 30 lines (an implementation, not a stub), a scaffold that did not compile (`-Werror` unused parameters), a missing header, and malformed JSON in one item's `card`.

Plan shape: always 4 items, an engine pair and an app pair. The widest level is 2, so on 4 slots the lint reports 33 to 41 % fill, a speedup of 1.3x to 1.7x, and a backfill rate of 0 %. The warning fires every time. The planner is told the slot count but does not act on it yet; the lint is recorded, not fed back into repair turns.

Plan quality, honestly: the checks make the plans well formed and compilable, not good. Seen so far: hedged contracts, fragile or wrong worked examples (for example `10 / 3 = 3.3333333333333335`, `10 / 0 = Error`), and an over-large last item. Nothing here has been run through the squad yet; no plan from this probe has been built into a project.

Failure classes: (1) JSON structure errors in a deeply nested `card` (2 of 5): a small model loses track of braces; a flatter plan form or a per-item reply would avoid it. (2) Scaffold stubs that do not compile under `-Werror`: caught and repaired in the same run. (3) Under-allocation of ready work: not caught by any check.

Not verified: a build from any of these plans, the success rate over a larger sample (5 seeds is a rough figure), and whether feeding the lint into the repair turn raises fill.

## 8. How a plan should be written (design, 2026-11-15)

**The problem.** One planner reply carries two disciplines at once: *what to build* (the split, the contracts, the examples) and *exact syntax* (nested JSON with
escaped quotes, C++ stubs that compile under `-Werror`). Nearly every failure in section 7 was format, not planning, and a failed reply was rewritten whole.

**Prior art** (vendor claims marked):

| Source | Finding | For us |
|---|---|---|
| Tam et al., "Let Me Speak Freely?" (arXiv 2408.02442) | Strict format restriction lowers reasoning quality; "answer in prose, then convert" keeps it. Shown on GPT-3.5-class models and small schemas. | Supports splitting planning from formatting; measure on ours. |
| BAML schema-aligned parsing (vendor benchmark) | Parse what the model wrote against the declared schema (trailing commas, unquoted strings, near-miss enums) instead of demanding strict JSON. | `repairJson` is a hand-made subset of this. |
| TOON (own benchmark only) | A tabular notation of JSON, about 40 % fewer tokens, best on uniform arrays. | Our plan is nested: small gain. |
| Grammar-constrained decoding: llama.cpp GBNF and `json_schema`, XGrammar, Outlines | Syntax cannot fail; it does not make content right, and may hurt it. | llama-server accepts a grammar per request; our engine call does not pass one yet. |
| The owner's finding | Thinking off is more reliable and faster for JSON. | The planner runs with thinking off. |

**Options.** A: today (one reply, JSON and FILE blocks, mends, repair by edits). B: two steps, a prose plan then a format step that is repaired by edits and may
be constrained. C: a lenient notation with our own parser. D: grammar-constrained JSON for the whole reply. E: TOON for the items table. F: section by section, a
skeleton then one small call per card, in parallel on idle slots.

**Recommendation: B then F**, with a grammar only on the format step and schema-guided tolerant parsing in the parser: (1) a short prose plan, thinking off;
(2) one format call per item, in parallel; (3) the scaffold written by a developer-persona call and judged by the plan checks and the compile; (4) `repairJson`
and the mends grown into a schema-guided parser; (5) each step measured against A on the same seeds (valid-first-try rate, repair edits, tokens, wall time).
A grammar is adopted only if the plan's content does not get worse on the same judge. These are experiments E3 to E5. The same "formatter" building block
(free output in, a checked artefact out, repaired by edits) serves the gate report, the card rewrite and the manager's decisions.

**For the owner:** may a second, smaller local model serve the format step, and may the engine call pass a grammar or schema (local only)?

**The discipline ladder** (owner, 2026-11-15). Prose then format is not only for the plan. Each piece of work has the same layers, and each layer has one job:

| layer | writes | judged by | when it fails, raise to |
|---|---|---|---|
| plan prose | the split, contracts, examples in words | the formatter can reproduce it; the examples are consistent | the owner (intent) |
| plan form | `plan-2` JSON and scaffold from the prose | `Plan::fromJson`, the plan checks, the scaffold compile | plan prose (a decision is missing or contradictory), else repaired by edits in place |
| work prose | per item: how the tests or the code will be built, in words (the cases to cover; the functions and their steps) | the contract, the examples | the plan form or plan prose |
| work source | tests or code | the gate | work prose (the description cannot be implemented), else repaired by edits |

The rule: a worker who cannot turn the layer above into its own output does not paper over it. It raises the issue to the AUTHOR of that layer (the one who wrote the prose),
who answers or revises that layer, exactly as a fork's coordinator can be asked about a section. This is the same hand-off the `CONCERN` item in the backlog describes
(pull-context and concern), now with a fixed ladder: a layer's author is recorded with the layer (planner/prose.md names the lead; the work prose names the item's
planner), so a question has an addressee. A format problem never climbs: it is repaired by edits where it is. A content problem (a contract that two items read differently,
an example that contradicts the contract) climbs one layer at a time, and the layer that fixes it is the one whose output is changed.

Build order: (1) plan prose then plan form, measured against the single reply (started: `llm-planner --prose`); (2) the concern hand-off from a failing item to the plan
layer (the dispute mechanism, `reviseTests`, already sends a developer's failure back to the tester; it generalises); (3) work prose for the tester and the developer
(a short description first, then the code, so a failing test is first discussed in words); (4) a recorded author per layer.
