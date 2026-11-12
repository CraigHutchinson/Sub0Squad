# Recorded-reply cases (block 1 of docs/BLOCK_PROVING_GROUNDS.md)

One `.case` file is one reply (or a reply cut off at the token limit and its continuations) with the files before and the outcome that must follow. They are
run by `test_blocks` and by `sub0squad block run --block 1`; every reply form the workers use has a case, and every malformed reply must be refused with a
named error and change nothing. The cases are written after the reply shapes seen on the real runs (docs/INTERVENTIONS.md rows 4, 5, 8, 29, 34, 35, 36); they are
not byte-for-byte captures. Add a case whenever a run shows a new shape.

```
# name: lines-edit
# classic: yes                 (the classic applier `applyEdits` must agree)
# expect-error: text           (the reply must be refused with a message containing this; nothing changes)
# expect-reads: 2              (the reply holds only read requests, this many)
# allowed: a b                 (the files the worker may write; default: those of base and want)
--- base <path>                (a file before; several allowed)
--- reply [cut]                (a reply; `cut` marks one that ended at the token limit, the next reply continues it)
--- want <path>                (a file after)
```
A section's text runs to the next `--- ` line; a reply loses its final newline (a cut-off reply ends mid-line).
