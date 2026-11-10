# Feedback loops: the pillar (a wide review)

Status: review, 2026-11-10. Owner direction: *the work an agent does is not a one-shot; it is an attempt and a check.* Compiling with diagnostics,
sanitizers, tests, QA, measurement: a validated bar at every step, a DevOps loop through the whole workflow, and the result of every check
fed back into the next iteration. This review lists the loops that exist, what the real failures of this project say about the ones that are
missing, and a model for putting them in one place.

## 1. Principles

1. **Nothing unchecked flows downstream.** Every output of a worker, an architect or a harness step is followed by a check before anything depends on it.
2. **Cheapest first, fail fast, in a ladder.** Parse before test, test before integrate, integrate before measure. A check that costs seconds runs before
   one that costs minutes, and the first failure ends the rung with its own diagnostic.
3. **A diagnostic is structured, located and small.** File, line, token, expected against actual; never a log. It is the next attempt's input as much as a
   report (the failures that were opaque were the ones that never got fixed: "1 test ran", "test failed").
4. **The same loop at every layer.** A function, a part, a divided parent, the project, the engine and the squad itself: each has its bar and its check.
5. **Checks are deterministic and cheap to rerun alone.** That is what makes iteration fast (`replay`), and it needs seeds, fixtures and recorded inputs.
6. **Gate or advisory, declared.** A *gate* stops acceptance (it is a precondition of "done"); an *advisory* is a finding that is communicated and acted on
   by the design, not a veto (the QA head). The plan says which each check is for each node.
7. **Slow checks run on idle capacity** and feed advisories: sanitizers, fuzzing, benchmarks against a baseline, mutation checks.
8. **The loop itself is measured**: time to feedback, attempts per node, which check caught what, how often a check is wrong (flaky or contradicting the
   contract). A check that is often wrong is a defect to fix, not a fact of life.

## 2. What exists today (inventory)

| Stage | Check | Gate? | Cost | Feedback form |
|---|---|---|---|---|
| Reply | the file-block format; files outside the allowlist rejected; missing or empty files | gate | ms | one line naming the file |
| Reply | cut off at the token limit (finish `length`) | continues the file; hands back for division if still cut | ms | the tail of the cut file |
| **Syntax (new)** | `node --check` on every file written | gate | tens of ms per file | file, line, token: the compiler's own message |
| Unit | `node --test` of the item's test file: counts, passes; for testers at least 70 % must fail against stubs | gate | seconds | leaf failures with messages; a test file that fails to load shows the runner's error |
| Attempt | the self-check: the gate again inside the attempt, a few repair turns with edits | part of the attempt | seconds + decode | the gate's excerpt, the draft |
| Attempt | near pass (few failing tests) / the same tests failing twice (two different attempts) | policy input | free | repair, or the tests go back to the tester |
| Part | coordinator integration gate over the merged parts; failure sent down to a part | gate | seconds | the failure, to the part it points at |
| Project | whole suite over the merged project; the page and its modules fetched from the static server | gate | seconds | the failing tests / the missing resource |
| Escalation | overseer (mechanical causes), manager strategies, harness-expert briefing | n/a | free | guidance, a briefing |
| Plan | plan-lint (width, critical path, serial tail); the plan validator | advisory / gate | ms | numbers and warnings |
| Quality | QA scan: duplicated blocks, the same helper declared twice, long functions | advisory | ms | findings with locations and a suggestion |
| Engine | the portal: live rates, memory, slots; the memory guard | n/a | free | figures, shedding |
| The squad itself | 13 tests (unit, end to end, idle, shim), `-Wall -Wextra -Werror` on every build, Sub0Perf benchmark baselines | gate | minutes | test output |

**Missing or thin:** a *test oracle* (nothing checks that a tester's tests are right before developers are graded by them); lint beyond parsing; a real
browser smoke test of the page; sanitizers, static analysis and coverage on the squad's own code (until this review: none); QA wired into the workflow
(it is a command, not yet a step); performance measurement inside the workflow (the squad's own Sub0Perf baselines exist, workflow outputs have none);
measurement of the loop itself; a place where a node declares its bar.

## 3. What the real failures say (checks that would have caught them)

| Failure seen in a run | Cost | The check that catches it earlier |
|---|---|---|
| A test expected token positions 2, 5, 7 where the text gives 1, 4, 6 (two developers failed it the same way) | a ladder, a dispute, a re-run tester | **Test oracle**: run a tester's tests against an independent reference (the 35B-built calculator is one) or against two independent attempts; a test that a reference fails is suspect |
| A page test expected `mount(document)` where the contract says `mount(document, evaluate)` | two attempts, then a wrong division | Test oracle again; or a **contract-consistency lint**: names and signatures in the tests must appear in the contract |
| A file cut off at the token limit, or two formats mixed in one file (`<<<<<<<` markers in code) | a retry, a stalled repair | **Syntax check** (built): file, line, token at once; continuation (built) |
| A test file reading `style.css` at load, crashing before any test | 4 attempts | the gate now shows the runner's error; a stub for the file (built); **lint** for top-level side effects in tests |
| The 9B wrote the same function twice in one file ("Identifier already declared") | an attempt | Syntax check (built) |
| A division missing the part that implements a file whose tests it wrote | 23 minutes of identical corrections | the harness completes it (built); **division lint** against the card's file list before accepting |
| A long single function (`evaluate`, 170 lines) in the 35B-built page | latent: hard to read, test and change | **QA scan** (built): simplicity finding, fed to the next iteration |
| A dangling pointer in a new helper (found by chance as a segfault) | a debugging detour | **Sanitizers** in the squad's own CI (section 5) |

## 4. The bar model

Each node carries a **bar**: an ordered list of checks, each with a level and a budget. The plan declares it (a harness supplies defaults), the engine runs
it as a ladder, and every check yields the same shape of result: `{check, level, passed, diagnostic, evidence}`. Today a node's bar is one hard-coded
function (`runHarnessGate`); the model turns it into data.

```
bar: [ {check: "syntax",        level: "gate"},
       {check: "tests",         level: "gate",     of: ["tests/lexer.test.mjs"]},
       {check: "oracle",        level: "gate",     against: "reference"},      // for testers: the tests agree with an independent implementation
       {check: "qa",            level: "advisory"},
       {check: "lint",          level: "advisory"},
       {check: "perf",          level: "advisory", baseline: "sub0perf"} ]
```

**Where each runs:** in the attempt (the self-check: the cheap gates), at the node's gate (all gates), at the coordinator (the tests of the level above, the QA
findings across the parts), at the project (the whole suite, the served page, a browser smoke test, the measured run), and on idle capacity (the slow ones).
A finished node never loses its checks: they are recorded with their results in the node's history, so "which checks did this pass, on what evidence" is a
question with an answer.

**For other harnesses the same ladder, different checks.** For C++: compile with the project's warning level as errors; clang-tidy; unit tests; AddressSanitizer
and UBSan; ThreadSanitizer for anything with threads; coverage as an advisory; benchmarks against Sub0Perf baselines on a measuring lock; format. The
harness-expert's facts say which apply; the manager adds the strategy "turn on the heavier check" when a defect smells of memory or ordering.

## 5. The squad's own loop (dogfooding)

The squad is software with the same need. Today: `-Wall -Wextra -Werror`, 13 tests, Sub0Perf baselines, and no sanitizers, no static analysis, no CI.
Done in this review: an AddressSanitizer plus UBSan build of the whole project and a run of the suite (result below). Wanted next: a ThreadSanitizer build (the
daemon, the portal sampler and the replay tool are threaded), clang-tidy with a short profile, a CI workflow that runs build, tests and the sanitizer builds on
every push, and the benchmark comparison on a quiet machine.

## 6. DevOps through the workflow

The loop is **plan, build, check, run, measure, report**, at each scale:

| Scale | Build | Check | Run | Measure | Report |
|---|---|---|---|---|---|
| a function | write the body | syntax, its tests | the test | time, tokens | the attempt's trace |
| a part | its files | gate, QA findings | the unit under test | attempts, tokens, time | the node card in the portal |
| a divided parent | merge parts | integration tests | the merged module | rounds, send-downs | the coordinator's history |
| the project | integrator | whole suite, serve check, browser smoke | the page served | pass rate, work ratio, wall time | `report.md`, deliverables, the portal |
| the engine and the host | the server job | health, slot counts | the load | tok/s, memory, swap | the system panel, shed events |
| the squad | CI build | tests, sanitizers, tidy | the daemon | benchmarks | the backlog, the design notes |

## 7. Prioritised gaps

1. **Test oracle** for testers (highest value per cost: wrong tests cost more attempts than any other single cause in these runs).
2. **The bar as data** and a single result shape for checks; record every check result in the node's history.
3. **QA as a workflow step** on idle capacity, findings into the inbox as non-blocking proposals and into the next attempt's feedback.
4. **Browser smoke test** of the integrated page (a headless run that loads it, presses keys, reads the result), so "served OK" means "works".
5. **Lint profile** for the web harness (top-level side effects in tests, unused names, duplicate declarations), advisory first.
6. **Loop metrics**: time to feedback, attempts per node, catch rate per check, flakiness; shown in the portal.
7. **CI for the squad**: build, tests, ASan and UBSan, TSan, clang-tidy on every push.
8. **C++ harness** with the compile, tidy, sanitizer and Sub0Perf ladder as its bar.

## 8. The debugger worker: observed values, not speculation (owner direction)

Moving on with some tests still failing is fine. **Not knowing which tests failed, or how, is not.** Today a developer gets the runner's assertion text and
guesses the rest. The fix is a tool in the box: a **debugger worker**, a sister to the developer that the developer (or the harness) can request when a
check fails, running in parallel with the debugger open on the failing case, so the tool process is handled once, efficiently, and only the useful output
reaches the developer.

- **Request:** the developer replies `NEED debug <test or expression>` (a skill, section 5 of PROMPT_REVIEW), or the harness raises it itself on a repeated
  failure (instead of another blind repair turn). It is a normal request in the inbox shape, answered by a worker, not the owner.
- **Work:** the debugger worker runs the failing test under the inspector (`node --inspect-brk` driven over CDP; for C++, gdb/lldb batch mode or a
  sanitizer report), sets a breakpoint at the failing assertion and at the entry of the function under test, and records: the stack, the **actual values**
  of arguments, locals and the returned value against the expected one, the first line where behaviour diverges from the test's expectation, and for a
  crash or sanitizer finding the symbolised trace. It does not edit code.
- **Reply:** a small structured record (test name, expected, actual, the divergence point with file and line, the stack trimmed to project frames, the
  values at the divergence). It goes into the developer's next turn as feedback, and into the node's history as evidence; the oracle (section 3) uses it
  to say whether the test or the code is wrong.
- **Parallelism:** it runs on an idle slot while the developer is already drafting, and one debugger can serve several failing tests in one pass. It
  is cheap in tokens (the tool does the work; the model only chooses where to look) and bounded in time.
- **Cheaper first:** before a model worker, a mechanical debugger: run the failing test with the harness's own tracing (a `console.log` proxy for each
  call of the function under test) and attach the values. The worker is for what that cannot show.
- **Always know which tests, and how:** whenever a node is accepted with failing tests (near pass), the node's record lists each failing test with its
  expected and actual values, and the portal shows them. A node with unknown failures is not accepted.

Added to the gaps list as 1b, next to the test oracle.

## 9. Result of the first sanitizer pass (2026-11-10)

ASan + UBSan build (`out/build/asan`, RelWithDebInfo, `-fno-sanitize-recover=undefined`): **no sanitizer finding** in the 13 tests. Two apparent
failures were artefacts of the instrumented build, not defects: `test_shim` expects the crashing child to die by SIGSEGV, but ASan's handler turns that
into an exit code; `test_e2e` failed once on the memory-shed timing check under ASan's 3x slowdown and passed on rerun (timing-sensitive; to be made
tolerant). Not yet done: TSan build, clang-tidy, leak detection (disabled: the daemon tests fork).

## 10. Built so far from this review (2026-11-10)

| Item | State |
|---|---|
| Failure detail (the mechanical first step of the debugger idea) | **Built.** The web gate's feedback now includes, for each failing test, the runner's expected and actual values and the assertion's file (unit-tested). A model-driven debugger worker (inspector over CDP, stack and locals) is not built. |
| Test oracle | **Built, off by default.** `SUB0SQUAD_ORACLE_DIR=<reference project>` runs a tester's tests against the reference; `oracle.txt` records the verdict; advisory, or a gate with `SUB0SQUAD_ORACLE_MODE=gate` (more than 15 % disagreeing). Checked by hand on the real lexer tests (agrees) and a corrupted copy (disagrees, gate mode fails it); no automated test yet. |
| QA in the workflow | **Partly.** The integrator runs the QA scan and puts findings in `report.md` and `qa.txt`. Not yet run on idle slots during the run, nor fed into the next attempt. |
| Sanitizers | ASan+UBSan clean (section 9). TSan, clang-tidy, CI: not built. |
| Bar as data, browser smoke test, web lint, loop metrics, debugger worker, C++ harness | Not built. |
