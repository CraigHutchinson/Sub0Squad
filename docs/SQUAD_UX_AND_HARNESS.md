# Squad UX, personas, work cards and the project harness

Status: DESIGN + implementation in progress, 2026-11-09. Builds on SANDBOX.md. Goals (owner direction): verify the loop with a local
llama.cpp Qwen model; make the sample project a **web** project that is easy to test and exercise; add **worker personas** so testing is
designed in; give small work items an **efficient harness** so an agent spends tokens on the actual work; **measure actual work versus
harness/boilerplate**; make the per-project harness an early, cheap need; keep the UX simple with **triage as the first call** and
architect or project-manager roles only when needed; and allow a **frontier deferral** for planning, today performed by a human or by
this Claude session writing a plan file, never by an API call (frontier tiers stay stubbed, DECISIONS 14).

## 1. UX: one call, triage first

```
sub0squad do "write a scientific calculator as a web page"
```

`do` runs **triage** (a rule-based `triage-1` today, a small local model later) and prints what it decided and why:

| Triage output | Meaning |
|---|---|
| project type | `web-static`, `cpp-header`, ... selects the **project harness** (section 4) |
| blueprint | a known plan template for this kind of task, or none |
| complexity 1..5 | feeds the decision policy |
| split | one item, or several; which personas |
| architect / project manager | `not needed` when a blueprint covers the task; otherwise `deferred` |
| context budget | tokens per work card |

- **Blueprint known** (the calculator, today): the plan is instantiated and the workflow starts. No architect, no PM, no frontier.
- **No blueprint**: the workflow enters **`awaiting-plan`** and writes `needs-plan.md` in its sandbox: the task, the triage, the harness
  catalogue and the exact plan-2 syntax, sized so a frontier model (or a human) can answer with one file. The owner supplies it with
  `sub0squad project plan <id> --file plan.json`. Nothing is sent anywhere; the "deferral" is a file handoff. This is how the **syntax of
  the work split and the context size** are iterated before any real frontier integration exists.
- Roles are on demand: **architect** only produces or revises a plan (deferred when triage says so); **project manager** only replans a
  workflow whose items keep failing (not built yet); everything else is the developer and tester personas plus the harness.

## 2. Personas

A persona is data: a system preamble, the file patterns it may write, its default gate and how its card is shaped. It is not a different
model; it is a different job with a different contract.

| Persona | Writes | Contract | Gate |
|---|---|---|---|
| `tester` | test files only | write tests for the stated contract, no implementation | tests load, there are at least N, and **all fail against the harness stub** (they test something real); later also pass against the reference |
| `developer` | implementation files | implement the contract so the tester's tests pass | the tester's tests (the trusted evaluator) |
| `reviewer` (reserved) | nothing | check the diff against the card's acceptance list | rule checks now, a model later |

**Test-first handoff.** The tester item has no dependencies and writes the tests from the contract; the developer item depends on it and its
card **includes the accepted test file**. The developer is graded by tests it did not write.

## 3. Work cards (the context packs)

A work card is the whole input of one small item, and the unit whose size and syntax we tune:

```json
"card": { "context": "one or two sentences of why", "contract": "the interface, as code or a table",
          "acceptance": ["observable checks"], "include": ["tests/eval.test.mjs"], "notes": [] }
```

`include` names files from accepted dependency outputs that the harness inlines (size-capped). The plan validator **estimates card tokens**
and rejects a card over the plan's budget (default 1,500 tokens), so a planner (small model or frontier) learns the size it must hit.
Everything stable across items (the output format, the persona preamble, the project harness description) is a **short fixed system block**
the harness supplies, identical for every item so a server can cache it.

## 4. The project harness (implicit, cheap, mostly code)

The harness is what makes a small model's work small. It is **code, not an agent task**: when triage names a project type the harness
creates the workspace and scaffolding itself (for `web-static`: `package.json` with the test script, a static `serve.mjs`, a tiny fake
DOM for UI tests, a stub `src/calc.js` that throws "not implemented", the test runner wrapper). Only an *unknown* project type needs a
harness-setup item, and that item is the architect's (deferred). The harness API the agent never has to touch:

- builds the item workspace (scaffold + accepted outputs of dependencies + this attempt's files) and tears it down;
- parses the model's reply (`=== FILE: path ===` blocks), drops anything outside the item's allowlist, and writes the files;
- runs the gate and reduces its output to a short failure excerpt (names and messages, not logs);
- on retry, hands the next attempt that excerpt as the only extra context.

## 5. Measuring actual work versus harness and boilerplate

Every agent job records (and the ledger keeps) where its tokens went, so card size, preamble size and retry cost can be tuned from data:

| Metric | Meaning |
|---|---|
| `tokens.card` | prompt tokens of the work card and included files (context the item needs) |
| `tokens.system` | the fixed system block (harness overhead; shrinks with caching and brevity) |
| `tokens.feedback` | prompt tokens of the previous failure excerpt (retry cost) |
| `tokens.work` | completion tokens that are content of allowlisted deliverable files |
| `tokens.waste` | completion tokens that are not deliverable (prose, fences, rejected files, thinking) |
| `harness.bytes` | bytes the harness supplied itself (scaffold, stubs, fake DOM), i.e. work the agent did not have to do |

KPIs (DESIGN section 21 style, data not code): **work ratio** = accepted `tokens.work` over all tokens (prompt and completion) of the
workflow; **rework** = `tokens.work` of attempts that were not accepted; **harness leverage** = `harness.bytes` over deliverable bytes;
**overhead per item** = `tokens.system + tokens.feedback`. Estimates are marked as such: segment token counts come from the server's
tokenizer, the split of the completion by characters.

## 6. The sample project: a scientific calculator as a web page

Deliverables of the `calculator-web` blueprint (a plan-2 written by hand, standing in for the frontier architect):

| Item | Persona | Files | Depends on |
|---|---|---|---|
| `eval-tests` | tester | `tests/eval.test.mjs` | none |
| `engine` | developer | `src/calc.js` | `eval-tests` |
| `ui-tests` | tester | `tests/ui.test.mjs` | none |
| `ui` | developer | `index.html`, `src/ui.js`, `style.css` | `ui-tests`, `engine` |

Exercise it: `node serve.mjs 8080` in the project directory, open the page; `npm test` (node's built-in runner) runs everything. The
integrator merges accepted items, runs the whole suite and fetches the page from the static server.

## 7. Local model verification

The model is Qwen3.8-27B at IQ1_S on `llama-server` (the Sub0Llm CPU profile). It is a very aggressive quantisation: expect poor first
attempts, which is the point: the harness, retries and the stub-frontier escalation are what is being exercised, and the ledger shows
what each cost. Slots are one, so item agents must not run concurrently against it (the scheduler does not yet know the engine's cap:
the arbiter and `EngineCaps` are M2/M5), hence `--max-jobs 1` for model runs. The server is itself started as a squad process job, so
its teardown is the verified-cancel path.

## 8. Results of the first real-model verification (2026-11-09)

Engine: the Ubuntu `qwen3-6` snap (Qwen 3.6 35B-A3B on llama.cpp b9482, CPU, about 8.3 decode tokens/s), thinking off, one slot, `--max-jobs 1`.
Two full runs of the `calculator-web` blueprint through `sub0squad do ... --agent llm` (about 25 minutes each). **Neither run produced a
finished web page**; both ended `failed` at the `engine` item. That is a real result, not a harness bug:

| | Run 1 | Run 2 (testers on engine profile `qwenA`, developers on `qwenB`, same snap) |
|---|---|---|
| `eval-tests` (tester) | accepted first attempt, 2,975 completion tokens | accepted first attempt, 2,872 tokens |
| `ui-tests` (tester) | accepted first attempt, 1,315 tokens | accepted first attempt, 757 tokens |
| `engine` local attempts 1 and 2 | 2 of 67 tests failed (`-2 ^ 2` precedence with spaces; `log2` read as identifier `log`) | 34 of 65 failed (parser error `Expected ) but got undefined`), identical on the retry |
| `engine` attempt 3 (`frontier-stub` = reference) | **failed 1 test the model wrote**: `factorial of negative throws` (the test expects `(-3)!`-style input to throw; the contract says nothing of the kind) | **failed 2 tests the model wrote**: `min(1,2,3)` and `max(1,2,3)`, contradicting the card ("two-argument functions") |
| `ui` | never started | never started |

What the numbers say (ledger, estimates): the system block was 167 tokens, a tester card 350 to 365; the **developer prompt was about 3,000 tokens
because the included test file (65 tests, about 2,500 tokens) dominated it**, so the work ratio of developer attempts is a context-size question,
not a completion question; deliverable tokens were 97 to 99 % of each completion (the reply format held), decode 8.2 to 8.5 tok/s, prefill about 5 tok/s.
Persona routing was verified from the journal and ledger: tester jobs ran with `qwenA`, every developer job with `qwenB`, and the status output
splits tokens and decode speed per engine.

### Findings, in order of consequence

1. **Test-first with a small model is the weak link.** In both runs the model-written tests contradicted the contract in a way the reference
   implementation could not satisfy, so a correct developer would also have failed. The tester gate only proves the tests *run, are numerous and
   fail against the stub*; it cannot prove they are *right*. Proposed (not built): a **test dispute** path. When a stronger tier fails the same
   tests as the earlier attempts, the policy answers `reviseTests` instead of `fail`: the tester item is re-run with the disputed test names and
   messages as its only feedback, its dependants are re-queued, and the dispute is recorded. Where a reference exists (the mock, a prior
   implementation) the gate can instead drop the tests the reference fails. Contract wording matters too: say `min` and `max` take exactly two
   arguments *in the test acceptance list*, not only in the contract.
2. **Failure feedback quality decides whether a retry can work.** Run 1 used the first gate excerpt ("2 subtests failed"), which told the model
   nothing; the excerpt now lists leaf failures with their messages (fixed during this session, run 2 used it). It did not rescue run 2's
   structural parser bug: the second attempt failed the same 34 tests. A retry with feedback helps with a wrong detail, not with a wrong design.
3. **Context cost is dominated by what a card `include`s.** Inlining the whole accepted test file is simple and expensive; options are a
   test index (names only) for the first attempt with the full file only on retry, or a tester instruction to write fewer, denser tests.
4. **Throughput.** At this speed one item costs 3 to 8 minutes, a four-item project with one retry about 25 minutes; a single engine slot means
   no parallelism, so the arbiter and `EngineCaps` (M2, M5) matter as soon as engines are shared.
5. **A second real engine was not exercised.** The snap's server holds about 25 GB resident of this host's 29.6 GiB, so loading the Qwen3.8-27B
   IQ1_S model beside it was not safe and the snap was not stopped. Run 2 exercised the routing with two profiles pointing at the same snap.
   The 27B model remains to be tried when the snap is idle or in a session of its own.
