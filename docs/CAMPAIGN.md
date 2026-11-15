# Campaign: the same project, many seeds

Status: 2026-11-15. Driver built and smoke-tested with the mock agent (`scripts/campaign.py`); the first real baseline campaign is `baseline` (20 seeds). Complements `docs/METRICS.md` (what to measure) and `docs/CONTEXT_AND_PLANNING.md` (the arms that follow).

## 1. Questions

1. **How variable is success?** The runs so far (v8 to v12) were one seed each, on a build that changed between them, so they cannot say whether a run fails because of the model's luck, the harness, or the plan. Twenty seeds of ONE pinned build give a success rate with an interval, and the spread of outcomes.
2. **Which failures repeat?** A class that appears in many seeds is a harness or plan defect; one that appears once is model variance. The histogram of first causes is the work list.
3. **What works?** The seeds that succeed show the path that works (rungs, mends, how many attempts), and what the successes have in common.
4. **What are we missing?** The records (first failures, best partial results, ticks) show which signals we lack.

## 2. Design (the baseline arm)

* Task `write a calculator in C++` (the calculator-cpp blueprint: the PLAN is fixed and hand-written, so planning quality is held constant in this arm), the 35B (`Qwen3.6-35B-A3B`, 4 slots of 8192), executor `fork`, frontier OFF, seeds 1000 to 1019, one pinned binary (the driver copies it into the campaign directory; a rebuild elsewhere cannot change the campaign).
* Runs are sequential on one daemon: one project at a time uses the whole engine, so seeds do not distort each other.
* **Not varied here, on purpose:** the plan, the model, the executor, the context given to workers. The arms of `docs/CONTEXT_AND_PLANNING.md` section 6 vary those, each as its own campaign against this baseline.

## 3. What is recorded per run (`campaign.jsonl`, one line per finished run)

Result and state; wall time; items accepted of items; attempts; decisions by kind (retry, mend, sections, reviseTests, rearchitect, think, fail ...); mends applied; ledger tokens AND the tokens in the fork attempts' own records (the ledger misses most of them until `docs/METRICS.md` slice 0 is built);
the best test result any fork attempt reached on the failing item (`bestTests`, the near-pass signal); the **first-failure signature** and a heuristic **class**; the engine's tick counters before and after the run (decode ticks, decoded tokens, prompt tokens, and their ratio, the achieved concurrency), when the engine was started with `--metrics`; and an archive of the workflow directory
(`archive/run-NN-seed-S.tar.gz`: plan, every attempt's conversations, gate texts, canvas logs, the finished project). `campaign-report.md` is rewritten after every run: success rate with its 95 % Wilson interval, the table, the failure-class histogram, wall time.

## 4. The failure taxonomy (a heuristic first cause; each class is confirmed by reading the run)

| class | meaning | typical signature |
|---|---|---|
| **P** plan or contract | the plan, contract or card was wrong, thin or contradictory | interface mismatch; a test importing a name no contract states |
| **D** decomposition | the architect's division was invalid or over-reached | a fragment that adds work the parent never had; tester items divided |
| **A** assembly | the parts do not fit together (sections never accepted, undefined members, unused helpers) | link errors, `-Wunused`, a section unaccepted |
| **S** implementation | one function is wrong (semantic slips) | tests failing with a value or an exception |
| **T** tests | the tests are wrong or too few | a tester failing, a dispute upheld |
| **H** harness | the instrument misled or failed (no failure record, a crash, accounting) | `no failure record`, harness errors |
| **M** model limit | the model cannot do this at this size | repeated identical replies, the same refusal many times |
| **E** environment | memory, engine, disk | engine stopped answering, shed jobs |
| **B** budget | wall or token budget ended a run that was converging | timeouts |

The driver assigns A/P/S/T/B from the gate text and the item's persona; **H and M, and the split between P and A, need a reader.** The report says "heuristic" for that reason; the failures worth reading first are those in the largest classes and the ones with the best partial result.

## 5. Budget and safety

* Observed wall time per run on this host: about 30 to 190 minutes (a fork attempt is 20 to 40 minutes; a failing run spends 5 to 8 attempts). Twenty runs is therefore **about 10 to 60 hours of engine time**; it runs unattended as a user service (`sub0-campaign`), resumable (`campaign.jsonl` is the state: run the same command again), stoppable after the current run (SIGINT or `systemctl --user stop`).
* The engine is the contained service (memory cap, no swap); the campaign unit has its own memory cap; the driver waits for free memory before each run, stops if the engine stops answering, and stops after three harness errors in a row. Frontier is off. Nothing leaves the machine.
* Wall time is **load dependent**; the tick counters are not. Compare campaigns on success rate, tokens, ticks and failure classes, not on minutes.

## 6. How to run it

```sh
. scripts/dev-env.sh
# the engine, with the tick counters on (--metrics), as the contained service of docs/STATUS.md section 6
scripts/campaign.py run --name baseline --runs 20                  # real model; resumes where it stopped
scripts/campaign.py run --name smoke --runs 2 --mock --no-portal   # no model: a one-minute check of the machinery
scripts/campaign.py report --name baseline                         # rewrite the report from campaign.jsonl
```

## 7. What the results decide

* **A success rate near 0** with the failures concentrated in A and P: the harness and the plan are the work (the context and planning design); more seeds add little.
* **A success rate between 20 and 80 %** with a spread of classes: variance is large, which makes paired comparisons need more seeds, and the repeated classes are the first fixes.
* **Most failures in S and M:** the model is the limit at this task size: smaller sections, stronger hints, a stronger model for the hard functions (with the owner's switch), or a different executor.
* **Successes that need many attempts** show the ladder is doing the work: the cost per success (tokens, ticks) is the number to improve.
* Any class that appears in at least three seeds becomes a catalogue row, a fixture and a mend or a prompt change **before** the next campaign; the next campaign is then compared with this one using the interval rule.
