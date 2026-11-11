# Interventions catalogue: where a person (or Claude) stepped in, and how the harness should mend itself

Status: started 2026-11-11. Rule from the owner: **every manual fix is a missing self-mending rule.** The harness should detect the situation, classify it, and
apply the repair itself; a human step-in is recorded here and is not finished until it has (a) a regression fixture, (b) a detector, (c) a mending action.
`Mend` says what the harness should do on its own; `State` is honest about whether it does.

Classes: **H** harness defect · **P** plan/architect defect · **M** model weakness the harness must absorb · **C** contract/test defect · **I** infrastructure/host · **O** operation (human mistake).

| # | Symptom seen | Root cause | Class | What was done by hand | Detector (signal) | Mend (harness does it) | State |
|---|---|---|---|---|---|---|---|
| 1 | Test expected token positions 2,5,7; two developers failed the same way | A tester's expectation was wrong | C | Dispute path added | same FAIL lines in two different attempts | send tests back (`reviseTests`), oracle check against a reference | dispute built (exit 5); oracle built, off by default |
| 2 | Tester stubs threw, so valid tests were rejected | Stubs threw instead of returning undefined | H | Stubs changed | tester ratio check failing valid tests | n/a (fixed at source) | fixed; no guard against a regression |
| 3 | Repeated-failure dispute fired on misunderstanding, not on bad tests | Threshold too loose | H | Restricted to ≥5 tests, ≤40 % failing | dispute with majority failing | n/a | fixed |
| 4 | Whole files re-emitted on each repair (81 % unchanged) | Repair protocol | M | Edit-style replies, `applyEdits` | measured changed-line share | edits first, full file fallback | built |
| 5 | Replies cut at the token limit | Output budget | M | Continuation of a cut file; hand back for division if still cut | `finish=length` | continue, then divide | built, unvalidated on a real run |
| 6 | Context overflow on the repair prompt | Conversation growth | M | Flat window (latest reply and feedback only) | prompt tokens near slot size | trim, or raise context | window built; context raising not built |
| 7 | `card.notes` as a string, `persona` typos, arrays as strings | Model JSON sloppiness | M | Tolerant plan parsing with position in the error | plan parse error | accept the near form, say where it failed | built |
| 8 | `<<<<<<<` edit markers inside a file block | Format mixing | M | Read such a block as edits | markers in a file | apply as edits | built |
| 9 | Code with the same declaration twice, cut-off functions | Model | M | `node --check` pre-test gate with file/line/token | parse failure | gate stage, located diagnostic | built |
| 10 | Division omitted the part that implements a file whose tests it wrote (23 min of identical corrections) | Architect | P | `completeDivision` adds the part | file in parent not covered | harness completes the division | built, unit-tested |
| 11 | Dangling pointer in `completeDivision` (segfault) | My bug | H | Copied strings before `push_back` | crash in test | n/a | fixed; ASan run clean afterwards |
| 12 | **Division graded a developer by a test file no tester writes** (run 1, `ui-js.ui-dom`, 7 attempts wasted) | Architect | P | `completeDivision` adds a tester | gate test file missing in workspace | harness adds the tester | built, unit-tested; **found only by a person reading the run** |
| 13 | **One failed item cancelled every other branch** (run 1) | Workflow engine | H | Failed branch now stops only itself | n/a | continue other branches, fail when nothing can move | built; **not yet seen in a real run** |
| 14 | Report blamed `engine`, which was never attempted | Failure attribution | H | Not fixed | root cause vs cancelled-victim | name the first cause, not the last item | **open** |
| 15 | Page stub rejected tests because it read `style.css` at load | Scaffold | H | Empty `style.css` stub | runner crash on load | show runner error (built) | fixed |
| 16 | I stopped the daemon/portal with an owner request open | Operation | O | Apology, notice, rule | n/a | daemon-down notice in Inbox; never stop with open requests | rule + notice built |
| 17 | `pkill -f` matched my own shell, twice | Operation | O | Anchored `pgrep` | n/a | n/a | rule |
| 18 | Four slots on one machine exhausted RAM and killed the session | Host | I | Shed jobs below a memory floor, OOM priority | available memory | shed; requeue | shed built; **requeue not built** |
| 19 | Division hung waiting for an engine that was cancelled | Scheduling | H | None yet | job waiting for the engine for minutes | reserve capacity for structural/integration jobs; NOT strict priority: structure and leaf drafts co-evolve (BUILDING_BLOCKS.md) | **open** |
| 20 | Run launched on a stale daemon binary | Operation | O | Restart on new build | daemon sha ≠ binary sha | `run-info.json` and a version check at submit | version stamping built; check **not built** |
| 21 | Build directories and daemon homes piling up | Operation | O | Archive and clean | n/a | `archive` command, one live home | manual, in backlog |
| 22 | `test_e2e` memory-shed check fails intermittently when the host is loaded (live 9B run) and under ASan | Timing-sensitive test (12 s wait, host load) | H | None yet (it passed on rerun) | test fails, then passes on the same binary | tolerant wait scaled by measured load, or a deterministic clock for the shed watcher | **open** |
| 23 | Harness chosen by comparing `"web-static"` in 8 files | No registry | H | `Harness` registry (`harness.hpp`, `harness_registry.cpp`) | layering/grep | one entry per harness | built; layer check guards it |
| 24 | TAP excerpt read `FAIL ... \| ok 7 - factorial \| ok 8 ...`: a failing test's error text ran on into following passing tests (first C++ and Python runs) | The reader assumed every runner ends a diagnostic with a line it skips; a new emitter exposed it | H | `parseTap` ends the error block at `ok ...` and `...` lines (in `harness_common`) | excerpt holds lines that are not part of a failure | n/a (fixed in the shared reader) | fixed; covered by `test_platforms` |
| 25 | Run w-000002 `engine.functions` failed after 6 attempts: the tester's file imports a named export `e` from `src/functions.js`; the developer cannot know it | Contract does not name the exports a test imports; the card gave behaviour, the test depends on API names | C | None yet (recorded) | gate text: `does not provide an export named` | the gate detects an interface gap (missing export, import, member or symbol), lists the names the tests import, exits 6; the policy goes straight to `rearchitect` (the card is rewritten to name every export) before any further leaf attempt | fixed 2026-11-11 for web, C++ and Python; covered by `test_platforms` and a policy test; not yet seen on a real run |
| 26 | Run w-000002 `ui-page` failed 8 attempts on 2 of 9 page tests (a button for every key; the module script calls `mount(document)`) | The 9B did not produce a complete key list and the test regex is strict about call shape; both tests are checks of text, not behaviour | M/C | None yet (recorded) | the same two FAIL lines on three attempts | after the same failing lines twice, quote the failing assertion and the contract line it checks in the card (exit 5 path exists, it sent tests back but the tests were not wrong) | open |
| 27 | C++ app tests failed to compile twice on the 9B (`"\"" + CALC_CLI_PATH` adds two string literals; `fclose` on a `popen` handle) | The scaffold left a small model to hand-roll process spawning; the Python platform already had a helper | M | None (recorded from run-v6 w-000001) | compile diagnostics in `cli-tests` attempts 1 and 2 | scaffold supplies `tests/helpers/app.hpp` (`app::run(args)` returning output and exit code) and the card says to use it | built 2026-11-11; not yet seen on a real run |
| 28 | Interface-mismatch detector (row 25's mend) fired on a compile error whose root was an unterminated string literal: the cascade "was not declared in this scope" matched (first 35B run, `dev-engine` attempt 2), so a rung was spent rewriting a card that was fine | The detector searched the whole output for signatures; a compiler's follow-on errors are noise after the first | H | None yet (found on a real run; fixed in source) | first error line carries no interface signature | classify by the FIRST error only (same rule as row 14, attribute the first cause) | fixed in source 2026-11-11, covered by `test_platforms`; live daemon still runs the old detector |
| 29 | Retries were whole-file rewrites: the prompt asked for edits but showed the earlier draft cut off at 7,000 characters (`... (truncated)`), so the 35B answered that it could not edit a truncated file and rewrote 340 lines, which often hit the token limit (`finish length`) (run-v7 `dev-engine` attempts 2 to 5) | The draft was clipped without regard to what an edit needs; SEARCH blocks need the exact text, which a small model cannot reproduce | H | Owner asked whether we ask for a rewrite each iteration | the worker's reply says it cannot edit; a reply of the whole file for an item that has a draft | the earlier draft is shown whole with line numbers (windows around the lines the gate named when too big, never cut mid-code); repair turns show the numbered current files; edits name lines (`LINES a-b`, `AFTER n`), several apply together against the draft's numbering; a SEARCH copied with its `N|` column still matches | built 2026-11-11, unit-tested; not yet seen on a real run |\n
## What the table says

1. **Rows 12, 13, 14, 19 were all found by reading a failed run, not by the harness.** The harness spent about five hours of 9B time on one
   missing test file. A mending loop would have caught row 12 on the *first* gate failure: "the gate test file does not exist in the workspace"
   is not a model failure; it is a plan defect with an exact, mechanical signature.
2. **Many fixes are rules about plans** (rows 10, 12): the division should be **validated before it is accepted**, not repaired after it
   fails. Validation is cheap and exact; wasted attempts are not.
3. **Missing: a mending ledger.** Each failure should be classified (H/P/M/C/I/O) by the harness, with the signal that classified it, and counted. A class with
   a repeating signature and no mend is a backlog item automatically; a mend that does not reduce its signature is wrong.

## The self-mending loop (to build)

```
failure → classify by signature (table above, as data) → known? apply the mend, record {signature, mend, outcome}
                                                      → unknown? record a case with its evidence, ask the manager strategies, then the owner
every case → becomes a fixture (recorded inputs/replies) → a regression test → the signature becomes a detector
```

Signatures are data (`mends.json`): `{id, class, match: gate-text regex | event | state predicate, action, evidence}`. The harness-expert briefing
already receives the harness facts; it should also receive the ledger, so an escalation can say "this is catalogue row N; the mend did not work because ...".

## Immediate follow-ups from this catalogue
- Row 14: failure attribution names the first failed cause. Row 19: a lease class for structural jobs. Row 20: refuse to submit when the daemon's sha differs from the binary's.
- Validate every division at acceptance (rows 10 and 12 as one validator: every developer part has a tester that writes its gate file; every file of the parent is
  delivered; no part graded by the parent's whole-suite test file).
- Write `mends.json` from rows 1-21 and make the gate/escalation path look signatures up there.
