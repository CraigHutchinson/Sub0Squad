# Follow-up session: the layered document, slice 2 and the sub0canvas backbone

Status: **done** (written 2026-11-17, closed 2026-11-18; see "Outcome" at the end). Strand 2 continues in Sub0Canvas, where its design waits for the owner. Backlog: "Layered document" under *Next* in [../BACKLOG.md](../BACKLOG.md). Evidence:
[../LAYERED_DOCUMENT.md](../LAYERED_DOCUMENT.md) (the design, slice 1 as built, the tier 0 A/B and its trace); the run directories in
`out/runs/layers-ab-2026-11-17/` (their README says what each one is).

**Progress (2026-11-18):** strand 1 is done: the adaptive switch and inline detail are built (sub0canvas v0.2.1 `layerInline`;
`canvas_agent.cpp`), measured at 5 runs per arm in four arms ("Slice 2 on tier 0" in LAYERED_DOCUMENT.md: always-on regressed fix-many, bugs-3 and
bugs-6; adaptive at a threshold of 3 misfired on fix-many; at 6, the gate's own excerpt, nothing regressed), and the threshold is now 6. Strand 2:
the design is written, sub0canvas `docs/BACKBONE.md`, waiting for the owner to read it.

## Goal

Continue the layered document from slice 1. There are two strands. Do them in order, or in two sessions if the owner prefers:

1. **Slice 2 of the issues layer**, measured on tier 0. The A/B showed where the summary helps (long, clipped verdicts) and where it costs (short ones).
   Make it pay its way everywhere.
2. **The sub0canvas backbone and source adapters**, designed and not yet implemented. The owner calls this "a major item of work to plan and design".

Do not retune or restart the llama server, and do not start a second one on its port. The session that owns it is the one that started this brief's author
(Sub0squad windows porting); coordinate tier 0 runs with it.

## What is known

**Where the code lives (since 2026-11-17).** The canvas tooling is the **Sub0Canvas** library (github.com/CraigHutchinson/Sub0Canvas), with
`lib/include/sub0canvas/{outline,edits,canvas,doc_layers}.hpp`, namespace `sub0canvas`, CMake target `sub0canvas::sub0canvas`, and tests
`test_edits` and `test_doc_layers`. It was tagged **v0.2.0** when imported from Sub0Squad d54758f (v0.2.1 since slice 2).

Sub0Squad pins that tag in `cmake/dependencies.cmake`. Its `include/sub0squad/{outline,edits,canvas,doc_layers}.hpp` are forwarding headers
(`using namespace sub0canvas`). What stays in Sub0Squad:
* `src/canvas_agent.cpp` (the loop, `layeredVerdict`, READ ISSUE, the `SUB0SQUAD_ISSUE_LAYER` switch, and `layers/turn-N.json` per turn);
* `src/shim.cpp` (which passes the switch through);
* the gate's `failures.txt` (`src/harness_common.cpp`);
* `test_canvas_agent`;
* `test_blocks`, whose recorded replies in `tests/data/replies` exercise the edits code.

**Workflow across the two repositories:**
1. Change sub0canvas first, run its tests, and tag it (`v0.2.x`).
2. Bump `GIT_TAG` in Sub0Squad, build, and run at least `test_blocks`, `test_canvas_agent` and the full suite.
3. Push sub0canvas (main and the tag) before the Sub0Squad pin. A pin to a tag nobody can fetch breaks CI.

For local work, `. scripts/dev-env.sh` exports `CPM_sub0canvas_SOURCE` to the sibling checkout `../sub0canvas`.
The cut-over import script has since been removed from Sub0Canvas.

**Tier 0 results (one build, 3 runs per case and arm; suggestive, not significant).**
* Overall, the layer took defects per turn from 2.18 to 2.48 and prompt tokens down 10%. Completion tokens rose 36%.
* It gained on bugs-6 (1.5 to 2.0) and bugs-10 (2.31 to 3.75), where the gate's excerpt clips.
* It lost on bugs-3 (1.8 to 1.29) and fix-many (5.0 to 3.0), where the old text already fit.
* READ ISSUE was used once in 12 runs.
* A 105-line collateral in one bugs-10 run came from the applier's re-indentation, fixed on main as ea1feb3/38da8e7, and from a localiser that pointed
  on a shared `+`, fixed in slice 1. With both fixes, bugs-10 changed at most 21 lines in 3 runs, at 2.73 defects per turn.

**The design's open parts** are all in LAYERED_DOCUMENT.md:
* "Direction: sub0canvas as the squad's backbone": layers as inboxes, tags and state, events, subscriptions, claims, views.
* "Sources: an adapter abstraction, and the GitHub project view": document, items, events and write capabilities; source anchors resolved by the core;
  a revision-aware index replayed over git history; the GitHub mapping; slices G1 to G5.
* "Out of scope (for now)".

The viewer's schema requests (anchor columns, a proposed `fix` per item, `mergedFrom`, a `revision` on loose anchors, `layerMeta`) are listed in
the sub0canvas repository's README and docs, and are not built yet.

**A Windows test finding, not in this brief's scope.** `test_thrash` and `test_nearpass` fail with "daemon did not come up" in deep checkouts. The control
socket path under `<build>/test-tmp/s0q-<tag>-xxxxxx/run/control.sock` exceeds Windows' 107-byte limit. They pass with `SUB0SQUAD_TEST_TMP` set to a short
directory. This belongs to whoever owns the Windows port.

## Steps

**Strand 1: slice 2, measured.**
1. **Adaptive switch.** Use the summary only when the gate's text would be clipped or lists more than about 3 failures; otherwise send the gate's text
   unchanged. This is in `canvas_agent.cpp`'s `layeredVerdict`. Keep `SUB0SQUAD_ISSUE_LAYER` (0 off, 1 always on), and add 2 for adaptive, as the
   default to measure.
2. **Inline detail.** Show the first N issues (about 3) in full, with their function's code, under the summary; keep READ ISSUE for the rest. The
   rendering (`layerDetail`, `layerSummary`) is in sub0canvas `doc_layers`, and the choice of N is in `canvas_agent.cpp`.
3. **Measure.** Run the A/B as before, with three arms (off, on, adaptive) on bugs-3, bugs-6, bugs-10 and fix-many, at **5 runs per arm**. Use
   `validation/tiers/run_t0.sh` and `gate_t0.py`, and the archived `compare_layers.py` with its paths adjusted. Report every case, regressions included,
   and the collateral per run from `gate.md`.
4. Write the result into LAYERED_DOCUMENT.md as "Slice 2 on tier 0".

**Strand 2: the backbone design.**
1. Write the design in sub0canvas (`docs/BACKBONE.md` or similar). It covers the mechanisms (tags and state, events, subscriptions as inboxes, claims,
   item links, views), the adapter interfaces and the revision-aware index, how today's squad pieces become layers and adapters, and the extension to
   `layered-doc-1`.
2. Give ordered slices, each with a measurable check. G1 to G5 in LAYERED_DOCUMENT.md are a first cut.
3. Cover the consumers outside Sub0Squad named by the owner: a text editor, a collaborative editor with review, comments and explainers, and the GitHub
   project view.

## Done means

* **Strand 1:**
  * the adaptive switch and the inline detail are built, with unit tests in sub0canvas (`test_doc_layers`) and Sub0Squad (`test_canvas_agent`);
  * the pin is bumped;
  * the full suite passes;
  * LAYERED_DOCUMENT.md has a "Slice 2 on tier 0" table with 5 runs per arm and every regression named.
* **Strand 2:** a design doc in sub0canvas that the owner has read, with interfaces, the format extension, slices and checks. No implementation.

## Environment and constraints

* Windows 11, MinGW-w64 GCC (WinLibs), Ninja, CMake ≥ 3.24.
* Build with `CPM_SOURCE_CACHE=D:\Craig\GitHub\Sub0Squad\out\cpm-cache` and `cmake --build out/build/mingw -j6`. Don't rebuild an exe that a tier 0 run
  is using.
* The engine is qwen35 at `http://127.0.0.1:8092/v1`, model `qwen35`, ctx 16384, 4 slots.
* Style: camelCase, Doxygen, `#pragma once`, `[[nodiscard]]`, std::format and std::print, `-Wall -Wextra -Werror`.
* The owner's standing rules:
  * commit as you go, and confirm with the owner before pushing;
  * never force-push;
  * in a shared checkout, commit only your own files, and never stash, reset or switch branches;
  * ask before downloads or installs;
  * nothing leaves the machine (frontier off).

## Outcome (2026-11-18)

**Strand 1, done.**
* Sub0Canvas `layerInline` (86e261d), released as v0.2.1 with the family CI (593dff3, full tier green).
* Sub0Squad 7ef8ff8: adaptive mode (`SUB0SQUAD_ISSUE_LAYER=2`, the default), `SUB0SQUAD_ISSUE_INLINE` (default 3), the pin bumped, `test_canvas_agent`
  in all three modes.
* 15baab1: the adaptive threshold raised from 3 to 6 (`kAdaptiveIssues`, the gate's own excerpt), after the threshold of 3 misfired on fix-many.
* 82f6fe3: "Slice 2 on tier 0" in [../LAYERED_DOCUMENT.md](../LAYERED_DOCUMENT.md), four arms at 5 runs each. Always-on regressed fix-many, bugs-3 and
  bugs-6; adaptive at 6 regressed nothing; bugs-10 rose from 3.12 to 4.17 defects per turn. READ ISSUE was used 0 times in 80 runs. The spread between
  identically treated arms reached 67%, so the gain is suggestive.

**Strand 2, designed; continues in Sub0Canvas.**
* `docs/BACKBONE.md` (ff19342, extended in cc99644 with scale to billions of lines and the S slices) waits for the owner's answers (its section 11).
* Everything needed to continue there without this repository was moved in on 2026-11-18: the general design (`docs/LAYERED_DOCUMENT.md`,
  `docs/CANVAS.md`, the owner's direction verbatim in `docs/DIRECTION.md`), use cases (`docs/USE_CASES.md`), the proving ladder (`docs/TESTING.md`), the
  loop with this repository (`docs/INTEGRATION.md`), golden fixtures from the slice 2 runs, a backlog, and briefs for S0 (scale harness), B0 (format
  catch-up) and G1 (git history). This repository's LAYERED_DOCUMENT.md now keeps only the squad's integration and its measurements.

**Left here:** measure the layer where it acts (10 runs per arm on clipped cases; inline against summary alone), in [../BACKLOG.md](../BACKLOG.md).
The Windows socket-path finding above was fixed by the Windows port (`1f45940`).
