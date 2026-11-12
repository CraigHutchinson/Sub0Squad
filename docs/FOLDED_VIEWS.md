# Folded views and read requests: focused tasks, context on demand

Status: built and unit-tested (`src/outline.cpp`); used by all three executors (`docs/CANVAS.md`, "As built"). First real use (35B, `dev-engine`) showed reads looping (catalogue row 36); reads are now bounded. Not yet proven on a real run.

## The problem

A small model has a small window (8,192 tokens per slot here) and cannot patch what it cannot see. The first 35B C++ run showed both failure modes: the earlier
draft was cut off mid-file, so the model said it could not edit and rewrote 340 lines, which then often hit the token limit. Showing the whole file is not the
answer either (it spends the window on code the fix does not touch). Owner direction (2026-11-11): give the structure, and let the worker ask for the lines it
needs, like an editor that collapses braced regions and expands them on demand.

## Prior art, and what we took from it

* **Aider's repository map**: a ranked outline of definitions (signatures without bodies) across the repository, sized to a token budget. Taken: the outline
  and the budget; not taken (yet): ranking across files.
* **SWE-agent's agent-computer interface**: the model sees a small window of a file and moves with `open`, `goto` and `scroll`, with a search command; its
  authors found that a deliberately limited, readable interface mattered more than raw file access. Taken: a window and an explicit request to move it.
* **Agentless-style localisation**: narrow in steps, file, then class or function, then lines, and edit only at the end. Taken: the same order, with the failure
  naming the first line.
* **Editors' code folding and LSP document symbols**: structure visible, bodies collapsed, expand on demand. Taken directly: the folded view.

## What is built

* `foldRegions`: function-like blocks (outermost), by braces for C++ and JavaScript and by indentation for Python; namespaces and classes stay open so their
  members show. Allman and K&R styles both work.
* `foldedView`: the file numbered (`  12| code`); whole when it fits the budget, otherwise each body collapses to one line,
  `... (41 lines folded: 7-47; name)`, with the closing line and everything outside functions kept. Regions that contain a line the gate named (compiler
  or test location) stay open, nearest first, while the budget allows.
* READ requests: a worker replies `=== READ: path LINES a-b ===` or `=== READ: path SYMBOL name ===` (one or several) and is shown those lines (at most
  160 per request). **Reads are bounded** (2026-11-12, after row 36): the first two read-only replies of an attempt cost no repair turn; from the third each
  one is a spent turn, and the answer to the second says the next reply must be an edit. A read that names only the path returns the **outline** (every
  body folded), never the whole file. A function of any length can be read by name (`foldRegions(..., all)`).
* `sectionView(text, python, openSymbols)`: every body folded except the named functions: the view of a section's owner in a fork's integration round.
* Edits name lines (`LINES a-b`, `AFTER n`, applied together against the numbering shown) or use search blocks; a worker that has read what it needs can
  patch a 340-line file with a few lines of reply.

## Where it goes next

* **Forks** (`docs/WORK_GRAPH.md` section 23): a primer writes the skeleton and each function is delegated to a session of its own. The folded view is that
  skeleton's natural form: the session for `parseUnary` receives the outline of the file (every other function folded) with its own function open, and asks
  for a neighbour with `READ ... SYMBOL`. The same view serves a reviewer reading a merged file.
* **Cross-file outline** (Aider-style): the signatures of the files a card includes, not only the item's own file, so a developer sees the interface it must match
  (the failing case of rows 25 and 28) without the other files' bodies.
* **Search request** (`=== SEARCH: text ===` returning matching lines with numbers) for names a worker cannot place.
* **Measure it**: tokens per attempt, rewrite share (lines changed over lines written), reads per attempt, and pass rate with and without folding, on the same
  plan; the replay tool (`sub0squad replay`) gives the paired comparison.

## Limits

Fold detection is heuristic (no parser): macros and unusual formatting can hide or expose the wrong lines, which costs a read, never correctness, because edits
apply to the real text. Template-heavy C++ and deeply nested lambdas fold as one function. A tree-sitter based outline is the upgrade if a platform needs it.
