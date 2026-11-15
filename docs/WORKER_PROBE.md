# Worker probe: a Haiku worker with no knowledge of the system

Status: 2026-11-15. First probe, one task, one worker, one round trip plus a debrief. Method and findings; the numbers are anecdotes, the defects are real.

## Method

* **Worker:** a Haiku subagent, no file or shell tools (the only tool it holds is WebFetch, which it was told not to use; one tool call was recorded per run, the hand-back itself). It cannot read this repository. **Isolation is partial:** it can still see the working directory path and recent commit subjects of the host session. It used none of that.
* **Harness:** this session, by hand: the real `dev-engine` card of campaign seed 1000 (task, contract, the tester's tests verbatim, the same system instructions), **without** the model-made skeleton and section split, so the worker writes the whole file. The gate was the real workspace (`experiments/worker-probe/gate.sh`: the campaign's CMake, `-Wall -Wextra -Werror`, the same test runner). The gate text went back unedited.
* **Then a debrief** from the worker's seat.
* A tool-less CLI route (`claude -p --tools ""`) was not available: the CLI is not logged in, and logging in is the owner's step.

## Result

Turn 1: a complete 300-line engine; it compiled with no warnings; 8 of 11 tests passed. The worker said, unprompted, that four test literals contradict the contract's own arithmetic: `2 + 3 * 4` expects 7 (14), `(2 + 3) * 4` expects 10 (20), `5 % 2` expects 0 (1), and `asin(1)*180/pi` in degrees mode expects 45 (about 5156.6). It refused to bend the engine to them.
Turn 2 (the gate text, unedited): it worked out by itself that **the gate's expected/actual labels are swapped** and left the engine alone.

The campaign archives show the same test defects in the real runs: seed 1000's tester wrote exactly these four wrong literals (attempt 1; attempt 2 fixed the asin one); seeds 1001 and 1002 wrote correct values but in the other argument order, and 1001 calls `asin(1, true)` with two arguments against a one-argument contract.

## Findings (worker's perspective)

1. **The tests are called the specification and nobody checked them.** A tester item's output is trusted. Nothing runs the tests against an independent reference or against the contract before a developer is graded by them. For seed 1000 the developer could never pass; the ladder spent ten attempts on an unsatisfiable target. (Class T/P, the largest suspect for the dev-engine failures.)
2. **The gate lies about direction.** `CHECK_NEAR(a, b)` prints `expected` = b and `actual` = a. The tester item is never told which argument is which, and seeds differ (seed 1000 wrote the literal first, seeds 1001 and 1002 second). With the literal first the failure text is inverted ("expected 1, actual 0" when the engine returned 1). A 35B model reading that text has no reason to doubt it. Haiku saw through it only because it knew the arithmetic.
3. **The worker had no channel to say "your test is wrong".** The reply format ("exactly one block and nothing else") has no place for a spec conflict; Haiku wrote a note outside the block, which the real parser would discard. The worker's own words: raise a conflict before writing code and wait for an answer, or be told whether the tests were checked. This is the CONCERN channel of `docs/CONTEXT_AND_PLANNING.md` section 5, now with a concrete case.
4. **The gate reports one failure per test case** (the first assertion), so "8 of 11 passed" listed 3 failures and hid the fourth.
5. **Without the arithmetic knowledge** the worker says it would have changed the engine until the tests passed (return 0 for `5 % 2`): the failure mode a small model cannot recover from, and what the fork's retries show.
6. **Confabulation to watch for:** the worker claimed to have written a file it cannot write and called the code "no warnings" without compiling. Claims need the gate, not the reply.

## What this means for the campaign

The two failures so far both stuck at `dev-engine` with 2 of 4 items accepted. Seed 1000 is explained by finding 1 plus 2. Seeds 1001 and 1002 had correct tests, so they need their own reading (the swapped labels and the skeleton split are the suspects). Hypothesis to check on the rest: the dev-engine failures are test and gate defects, not model limits.

## Next (not built; the campaign binary is pinned, so none of these changes it)

* **Neutral, documented gate labels** (`left`/`right`, or one stated argument order in the tester card) and all failures per case: a harness fix, catalogue rows 76 and 77.
* **A test check before the developer is graded**: run the tests against the contract (a reference implementation written by a different worker, or a mutation check; the cheap first step is a tester-side consistency review) and a spec-check turn for the developer.
* **A conflict reply form** for workers (a CONCERN block) and a keeper that answers.
* **Repeat the probe** over the other archived tester outputs (seeds 1001 and 1002) and with a section-split card (the real 35B shape) to see whether a strong worker also fails there, and score each run with the habits of `docs/METRICS.md` section 4a.
