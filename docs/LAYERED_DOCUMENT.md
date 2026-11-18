# The layered document in Sub0Squad: the issues layer, as integrated and measured

Status: slices 1 and 2 of the issues layer are BUILT and measured on tier 0 (slice 2's adaptive mode is the default since 2026-11-18). The library
(stable block ids, layers, the issues layer, the `layered-doc-1` writer) lives in
[Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas), consumed here at a pinned tag (`cmake/dependencies.cmake`). This document keeps what is
the squad's: how the canvas loop uses the layer, the switches, and the tier 0 evidence. The general design moved to Sub0Canvas on 2026-11-18.

## Where the design lives (Sub0Canvas)

| document | what it holds |
|---|---|
| [docs/LAYERED_DOCUMENT.md](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/LAYERED_DOCUMENT.md) | the model, block identity (how an id survives edits, rewrites, orphans, scope), edit addressing and its alternatives, the summary and the query, localisation, prompt cost, how layers are added, duplicates, history, the formats, prior art |
| [docs/DIRECTION.md](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/DIRECTION.md) | the owner's direction of 2026-11-17, verbatim: sub0canvas as the squad's backbone, source adapters and the GitHub project view |
| [docs/BACKBONE.md](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/BACKBONE.md) | the design written from it: state and tags, events, subscriptions as inboxes, claims, the store, adapters, the revision-aware index, scale to billions of lines, slices with checks, open questions for the owner |
| [docs/CANVAS.md](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/CANVAS.md) | the library half of [CANVAS.md](CANVAS.md): the revisioned document, transactions, folded views and reads |
| [docs/INTEGRATION.md](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/INTEGRATION.md) | how a library change reaches this repository: a tag there, a pull request here bumping the pin, the evidence each change needs |
| [README.md](https://github.com/CraigHutchinson/Sub0Canvas#readme) | the viewer, which opens an attempt's `layers/` and `canvas/` directly, and the optional format fields it already reads |

## What stays here

* `src/canvas_agent.cpp`: the loop's use of the layer (`layeredVerdict`, `=== READ: ISSUE n ===`, `layers/turn-N.json` per turn, `issueLayer`,
  `issueReads` and the layer's counters in `agent.json`), and the switches:
  * `SUB0SQUAD_ISSUE_LAYER`: 0 off, 1 always on, 2 adaptive (the default: the layer only when the gate's text would be clipped or it lists more than
    `kAdaptiveIssues` = 6 failures, the gate's own excerpt);
  * `SUB0SQUAD_ISSUE_INLINE`: how many issues are shown in full under the summary (default 3; 0 for the summary alone).
* `src/shim.cpp` passes both switches through a job's rebuilt environment (without that, an A/B measures the default twice).
* `src/harness_common.cpp`: the gate's `failures.txt` (every failing test, where `gate.txt` keeps six).
* Tests: `test_canvas_agent` ("The issues layer", in all three modes) and `test_blocks` (recorded replies through the library's edits). The library's
  own tests (`test_doc_layers`, `test_edits`) run in Sub0Canvas.
* The evidence below and its run directories (`out/runs/layers-ab-2026-11-17/`, `out/runs/layers-slice2/`). Sub0Canvas keeps three of these runs as golden
  fixtures (`fixtures/golden/`).

Next here: measure the layer where it acts (off against adaptive on bugs-10 and other clipped cases at 10 runs per arm; inline detail against the
summary alone), in [BACKLOG.md](BACKLOG.md). The sections below are as written at the time; paths such as `src/doc_layers.cpp` in "Slice 1 as built"
are now `lib/src/doc_layers.cpp` in Sub0Canvas.

## Slice 1 as built

* `BlockIndex` (ids, reconciliation, history, JSON) and the issues layer (`parseIssues`, `layerSummary`, `layerDetail`, `layerFocus`, `parseIssueReads`,
  `layeredJson`), in `src/doc_layers.cpp`, pure data.
* The gate writes `failures.txt`: every failing test, where `gate.txt` keeps six (`Tap::all`, `src/harness_common.cpp`). The C++ and Python runners
  carry it through.
* The canvas loop (`src/canvas_agent.cpp`) shows the summary for every failing verdict (the starting files, a staged revision that does not compile, a
  failing commit, a reverted commit), answers `=== READ: ISSUE n ===` as a read, writes `layers/turn-N.json`, and records `issueLayer` and `issueReads` in
  `agent.json`. `SUB0SQUAD_ISSUE_LAYER=0` turns it off, so one build can be measured both ways. A job's environment is rebuilt by the shim, so the
  shim passes this switch through (`src/shim.cpp`). Without that, an A/B silently measures the default twice, which is what the first attempt at this
  measurement did. The verdict about an earlier attempt's draft (attempt 2 and
  later) is still the gate's text.
* The summary header names the query only. It does not tell the worker to fix everything in one reply: that instruction is the prompt session's, so the two
  effects can be measured apart.
* Tests: `test_doc_layers` (the identity rules, orphans, revival, a cross-file move, Python blocks, the issues layer including 1-N and loose anchors,
  rendering after a rewrite, the JSON) and the `test_canvas_agent` block "The issues layer" (summary, READ ISSUE, the per-turn JSON, and the switch off).

## Slice 1 on tier 0

Measured 2026-11-17 on build `0.0.0-151-g44db273` (slice 1 plus the shim switch on base 973b9e6; the branch was later rebased, and the later commits change
only the item key, JSON and docs). Engine `qwen35` (llama server on port 8092, ctx 16384, 4 slots), 4 jobs at once. One build, two arms, chosen per run by
`SUB0SQUAD_ISSUE_LAYER` (0 = the gate's clipped text as before, 1 = the issues summary). Every run's own `agent.json` records which arm it ran in, and every
run matches its arm. Three runs per case per arm, 24 runs in total, and all 24 passed. Defects per turn is the gate's measure: defects × passed runs ÷ turns
of the passed runs. Tokens are means per run. Seconds are not compared, because the runs shared the server with other sessions.

| case | defects | off: turns per run | on: turns per run | off: defects/turn | on: defects/turn | off: first attempt | on: first attempt | prompt tokens off → on | out tokens off → on | issue reads (on) |
|---|---|---|---|---|---|---|---|---|---|---|
| bugs-3 | 3 | 1 1 3 | 2 1 4 | 1.80 | **1.29** (worse) | 3/3 | 3/3 | 8,342 → 11,749 | 446 → 1,349 | 0 |
| bugs-6 | 6 | 4 5 3 | 4 3 2 | 1.50 | **2.00** | 3/3 | 2/3 | 20,074 → 15,128 | 1,073 → 1,518 | 0 |
| bugs-10 | 10 | 4 3 6 | 1 4 3 | 2.31 | **3.75** | 1/3 | 3/3 | 21,699 → 14,121 | 2,304 → 2,524 | 1 |
| fix-many | 5 | 1 1 1 | 1 3 1 | 5.00 | **3.00** (worse) | 3/3 | 3/3 | 5,106 → 8,550 | 590 → 621 | 0 |
| all | 72 | 33 turns | 29 turns | 2.18 | 2.48 | 10/12 | 11/12 | 13,805 → 12,387 (−10%) | 1,103 → 1,503 (+36%) | 1 |

What this shows, and what it does not:

* **Where the layer was meant to help, it helped.** On bugs-6 and bugs-10 the gate's excerpt keeps six failures and clips the rest. There, defects per turn
  rose by 33% and 62%, and prompt tokens fell by 25% and 35% (the summary replaces the clipped text and its repeats). bugs-10 also passed on the first
  attempt in all three runs, against one in three without the layer.
* **Where the old text already listed everything, it did not help.** bugs-3 and fix-many got worse: one extra turn in one run of fix-many, and in two runs of
  bugs-3. Their prompts grew, because the summary's localisation (the function's line range) is added to a verdict that was already short. Completion tokens
  rose in every case.
* **A quality regression, traced.** In bugs-10 with the layer on, run 2 changed 105 lines where 10 were needed, 45 of them collateral (quality_t0.py).
  Without the layer, the worst bugs-10 run changed 18 lines, 1 of them collateral. Traced in the run's replies, the 105 lines break down as:
  * **73 lines of re-indentation.** The worker's SEARCH text came back indented by 5 or 6 spaces where the file has 4. The edit applier matched it
    anyway, because its matching ignores whitespace, and then wrote the REPLACE text verbatim, so five whole functions moved right. This is an
    edit-applier finding, not a layer finding: a match that ignored indentation should carry the file's own indentation onto the REPLACE. It can happen
    in either arm.
  * **17 lines of an invented function.** `check_end_of_subexpression()` was added and never called. It came from the summary: issue 10
    (`ev("(1+2")` did not throw) was localised to "likely in 2 places: expr(), unary()", but the defect is the unchecked `)` in `primary()`. The
    worker followed the pointer, then spent a READ ISSUE and a fourth turn before fixing `primary()`. The localiser scores expression tokens against
    block literals, and punctuation such as `+` and `(` points at the wrong blocks. Rule for the next slice: a suspect needs a test-name match or a
    non-punctuation token, otherwise the issue says "location unknown". A wrong pointer costs more than no pointer.
  * **32 lines remain once indentation is ignored**, against 10 to 18 in the other five runs.

  So the summary does not invite a whole-file rewrite. It invited one wrong edit, and the applier's indentation turned three small edits into whole-function
  diffs.
* **The worker hardly asks for detail.** It read one issue in 12 runs (`=== READ: ISSUE n ===`). The summary alone carried the effect. The query costs
  nothing when it is not used, but its value is not shown yet.
* **The noise is as large as the effect.** Three runs per arm, and per-run turns vary from 1 to 6 within one arm (bugs-10 off: 4 3 6). The overall gain,
  2.18 to 2.48 defects per turn (+14%), is not significant at n = 3. An earlier build of the same slice (before the prompt session's changes, layer on only)
  measured bugs-3 1.29, bugs-6 2.0 and bugs-10 3.33 (two runs), which agrees with this arm.
* **A first attempt was void.** The switch was read in the agent's own process, but the shim rebuilds a job's environment, so every "off" run in that attempt
  ran with the layer on. The shim now passes `SUB0SQUAD_ISSUE_LAYER` through (commit "Shim: pass SUB0SQUAD_ISSUE_LAYER through"), and those runs are not
  counted here.

**After both fixes (re-run, build `0.0.0-162-g2f52498`).** The applier fix (origin/main ea1feb3: a REPLACE takes the matched lines' indentation) and the
localiser fix (an operator character points at a block only when one block holds it) were applied, and bugs-10 was run again with the layer on, three runs:

| bugs-10, layer on | turns per run | defects/turn | first attempt | changed/ideal lines per run (collateral) |
|---|---|---|---|---|
| before the fixes | 1 4 3 | 3.75 | 3/3 | 10/10; 105/10 (45); 14/10 |
| after the fixes | 3 5 3 | 2.73 | 2/3 | 10/10; 21/10 (6); 10/10 |

The collateral is gone: the worst run went from 105 changed lines to 21. Defects per turn moved from 3.75 to 2.73, which is still above the off arm's 2.31
and still within the noise at n = 3; the one run that needed a second attempt accounts for most of the drop. Read it as: the quality regression was the
applier and the localiser, and both are fixed.

Next, from this:
* **Switch the layer on adaptively.** Use the summary only when the gate's text would be clipped, or when more than about 3 failures are listed;
  otherwise send the gate's own text. bugs-3 and fix-many would then behave exactly as in the off arm. This is one more A/B arm.
* **Put the detail inline.** Show the first N issues (about 3) in full, with their code, under the summary instead of waiting for READ ISSUE, which was
  used once in 12 runs.
* Done since: the localiser no longer points on a shared operator, and the applier re-indents a REPLACE (origin/main ea1feb3); see the re-run above.
* **Measure with five runs per arm.** Read nothing into a gap smaller than about 30%. At n = 3 and temperature 0.7, this table is suggestive, not
  significant.

## Slice 2 as built

* **Three modes**, chosen per run by `SUB0SQUAD_ISSUE_LAYER` (passed through by the shim): `0` off (the gate's clipped text), `1` always on, and `2`
  adaptive, the default: the layer is used only when the gate's text would be clipped (longer than the verdict's clip, 1.2k or 1.5k characters) or the
  verdict parses into more than 6 issues (`kAdaptiveIssues`, gate.txt's own excerpt; it was 3 until the A/B below); otherwise the gate's text is sent unchanged, with the gate's own focus, exactly as in mode 0.
  The layers are written to `layers/turn-N.json` in modes 1 and 2 either way, and READ ISSUE is answered in both.
* **Inline detail.** Where the layer is used, the first issues are shown in full under the summary, each with its function's current code
  (`layerInline` in sub0canvas v0.2.1: the first `SUB0SQUAD_ISSUE_INLINE` issues, default 3, within 3,000 characters, the first always; a function
  shown for an earlier issue is named, not repeated). The rest are left to `=== READ: ISSUE n ===`. `SUB0SQUAD_ISSUE_INLINE=0` is slice 1's summary
  alone. For compiler diagnostics, the compiler's own words follow unless every issue is already shown in full.
* `agent.json` records `issueMode`, `issueInline`, `layerShown` (failing verdicts shown through the layer) and `layerPassed` (those adaptive mode left
  as the gate's text), beside `issueLayer` and `issueReads`.
* Tests: `test_doc_layers` (`layerInline`: the count, the budget, the empty cases, a function shared by two issues) and the `test_canvas_agent` block
  "The issues layer", now run in all three modes (mode 1 shows the issue in full in the verdict; mode 2 passes a short single failure through unchanged
  and still writes the layers).
* The A/B drivers and runs are in `out/runs/layers-slice2/` (`measure_slice2.sh`; `compare_slice2.py`, which also reads each arm's per-run lines
  and collateral from its `gate.md`; `table.md`; the README says which build each arm ran).

## Slice 2 on tier 0

Measured 2026-11-18. Engine `qwen35` (port 8092, ctx 16384, 4 slots). Cases bugs-3, bugs-6, bugs-10 and fix-many, **5 runs per case per arm**,
80 runs, **all 80 passed**. Arms off, on and adaptive (threshold 3) ran on build `0.0.0-178-ga03fb86`; the fourth arm, adaptive at threshold 6, on
`0.0.0-180-g15baab1`, which differs only in that constant. Every run's `agent.json` records its mode, and every run matches its arm. Defects per turn
is the gate's measure (defects × passed runs ÷ turns of the passed runs). Tokens are means per run.

| case | off | on | adaptive (>3) | adaptive (>6) | layer shown, adaptive >6 (verdicts) |
|---|---|---|---|---|---|
| bugs-3 | 1.50 (3 3 2 1 1) | **1.07** (1 5 2 5 1) | 1.67 (1 1 3 1 3) | 2.50 (2 1 1 1 1) | 0 of 6 |
| bugs-6 | 2.73 (1 5 1 3 1) | 2.31 (3 3 2 3 2) | **1.88** (3 3 3 2 5) | 3.00 (2 3 3 1 1) | 0 of 10 |
| bugs-10 | 3.12 (3 3 3 2 5) | 3.85 (2 4 4 2 1) | 3.57 (2 2 3 2 5) | 4.17 (3 3 3 1 2) | 5 of 12 |
| fix-many | 5.00 (1 1 1 1 1) | **2.50** (2 3 1 3 1) | **3.57** (2 1 1 2 1) | 5.00 (1 1 1 1 1) | 0 of 5 |
| all | 2.86 (42 turns) | 2.40 (50 turns) | 2.61 (46 turns) | 3.64 (33 turns) | |
| first attempt | 18/20 | 19/20 | 18/20 | 20/20 | |
| prompt tokens | 10,565 | 13,897 (+32%) | 12,077 (+14%) | 8,494 | |
| out tokens | 811 | 1,097 (+35%) | 950 (+17%) | 862 | |
| issue reads | 0 | 0 | 0 | 0 | |

(Turns per run in brackets. Bold: the regressions against off named below.)

**Every regression, named.**
* **Always on (mode 1) lost on three of four cases.** fix-many halved (5.0 → 2.5: three of five runs needed 2 or 3 turns where off needed 1),
  bugs-3 fell 29% and bugs-6 15%; overall 2.86 → 2.40, with 32% more prompt and 35% more completion tokens. The inline detail makes a short verdict
  much longer (bugs-3's prompts 10.0k → 15.6k). This repeats slice 1's finding on fix-many and bugs-3, now at n = 5.
* **Adaptive at threshold 3 misfired.** fix-many's five short failures fit in the gate's text, but 5 > 3, so the layer was shown on the first verdict
  of every run, and fix-many fell to 3.57. bugs-6 (six failures) fell to 1.88: its run 1 changed 40 lines where 6 were needed, **21 of them
  collateral**, and run 5 changed 10 lines (4 collateral, a repeated edit of 3 lines); the layer was shown on 5 of its 14 verdicts.
* **Collateral elsewhere** (gate.md's quality column): off, bugs-6 run 2 changed 10 lines (1 collateral); on, fix-many run 1 changed 7 (3 collateral)
  and bugs-10 run 5 changed 17 (1 collateral), and on's bugs-6 runs 2, 4 and 5 changed 14, 14 and 10 lines where 6 were needed. Adaptive at 6 had
  none: its worst run changed 14 lines of 10 on bugs-10, no collateral flagged.
* **Adaptive at threshold 6 regressed nothing**, and it is the one arm where every case is at or above off.

**What this shows, and what it does not.**
* **The noise is measured, and it is large.** Adaptive at 6 never showed the layer on bugs-3, bugs-6 or fix-many: those 15 runs got exactly the off
  arm's treatment. Yet bugs-3 scored 2.50 against off's 1.50 and bugs-6 3.00 against 2.73. Identical treatment, 67% apart on bugs-3. So no
  difference in this table below about 60% on one case is evidence, and adaptive-at-6's overall 3.64 against 2.86 is mostly that noise.
* **The only treatment difference at threshold 6 is bugs-10**, where the gate clips: 4.17 against off's 3.12 (+34%), 5/5 first attempts against
  4/5, no collateral. Always on scored 3.85 there and adaptive at 3 scored 3.57. Three arms that show the layer on bugs-10 are all above off, which is
  consistent with slice 1, but each alone is inside the noise.
* **The worker never asked for detail**: 0 READ ISSUE in 80 runs (1 in 12 in slice 1). Inline detail replaces the query rather than complementing it.
* **The baseline moved.** The off arm is far better than slice 1's (2.86 against 2.18; the applier, localiser and prompt changes since), so there is
  less for any layer to gain.

**Decision taken here:** adaptive's threshold is 6 (`kAdaptiveIssues`, commit "Issues layer: adaptive mode shows the layer only where the gate
clipped"), and adaptive stays the default: it costs nothing where the gate's text fits (it is then mode 0) and is the best-measured arm where it
clips. Always-on with inline detail should not be used on short verdicts.

Next, from this:
* Measure the layer where it acts: bugs-10 and larger clipped cases only, more runs (10 per arm), off against adaptive. The short cases add only noise.
* Inline detail versus summary alone where the gate clips (`SUB0SQUAD_ISSUE_INLINE=0` against 3 on bugs-10), since READ ISSUE is never used.
