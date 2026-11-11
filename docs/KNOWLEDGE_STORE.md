# The knowledge store: the larger proving-ground project, and a core Sub0Squad component

Status: design, 2026-11-11 (owner choice: something useful and relevant to Sub0Squad, the knowledge database). Working title only; the product name is the
owner's call. It realises the knowledge register of `docs/WORK_GRAPH.md` section 19 (designed, never built) and replaces the line-grep in
`src/recall.cpp`.

## 1. Why this one

* **Useful to us:** the squad forgets everything between small jobs. The store is how it remembers lessons, pitfalls, facts, decisions and open
  questions, with evidence, so later plans and briefings start from experience (section 19).
* **A real proving ground:** it is a native C++ library with durable storage, an index, a query language, lifecycle rules, a CLI and a web viewer:
  five or six layers with contracts between them, both harnesses (`cpp-cmake` for the core, `web-static` for the viewer), and checkable behaviour
  without a person.
* **It dogfoods:** once it passes its own suite the squad adopts it (the register, the dreamer's output, recall). A defect found by the squad's own use
  becomes a new test in the suite. The squad then keeps building the thing it depends on.
* **Staged, so it measures rework:** each slice adds a layer or changes a contract the earlier slices assumed.

## 2. What it stores (from section 19, made exact)

An **entry**: `{id, scope, kind, text, evidence[], confidence, by, at, basis, state, supersededBy?, challenge?}`.

* `scope`: node, persona, file type, harness or project (hierarchical, `project/harness/persona/node`).
* `kind`: `lesson | pitfall | fact | decision | question`.
* `evidence`: file and line in the trace, so a claim can be checked.
* `basis`: unconditional, or conditional with the condition that reopens it (engine, model or harness version).
* `state`: `current | archived`; an entry is superseded by naming its replacement and a reason, and moves to the archive. Readers see current entries only;
  recall can ask for the archive.
* `challenge`: an open challenge flags the entry to readers until settled.
* Confidence decays with age and with harness changes; the store computes the effective value at query time (a pure function of the entry and `now`).

## 3. Layers (each a unit with its own tests; contracts are versioned)

| layer | unit | contract in one line | oracle or property |
|---|---|---|---|
| 0 | `codec` | entry to bytes and back; length and CRC framing | round trip for generated entries; any single bit flip is detected |
| 1 | `log` | append-only segment files; open recovers the longest valid prefix | **crash property:** truncate the file at every byte offset; reopening yields exactly a prefix of the appended entries, never a partial or reordered one |
| 2 | `index` | scope tree, kind, token postings, by-evidence | differential against a naive scan of all entries |
| 3 | `query` | a small query language (words, `scope:`, `kind:`, `by:`, `since:`, `state:`) to a ranked list | differential against a reference ranker; BM25 ordering on a fixed corpus; stable tie-breaks |
| 4 | `lifecycle` | supersede, archive, challenge, settle, decay, expiry, merge duplicates | model-based test: a random sequence of operations against an in-memory model must agree with the store after every step and after a reopen |
| 5 | `store` | the public facade: thread-safe readers over a snapshot, one writer, compaction | readers never see a half-applied operation (stress test under TSan); compaction preserves every current entry and the archive |
| 6 | `cli` | `know add|query|supersede|challenge|export|verify` | golden transcripts |
| 7 | `viewer` (web) | a static page over an exported JSON: search box, scope tree, entry detail with its evidence and archive | the web harness's DOM tests; a golden render |

Seams where the V is exercised: the **query** layer's needs change the **index** contract (what the index must expose for ranking), and the
**lifecycle** layer's decay rule changes what **codec** must store (time of last use). Both are contract amendments that go up the V when a leaf is drafted,
the very case `docs/PROCESS_DESIGN.md` is built for.

## 4. Slices

1. **Slice 1, the log:** codec, log, a trivial scan query, the CLI `add` and `query`. Proves the crash property. About 12 items.
2. **Slice 2, the index and ranking:** postings, scope tree, BM25, the query language. The index contract is amended by the ranker. About 12 items.
3. **Slice 3, lifecycle:** supersede, archive, challenge, decay, expiry; the model-based test. Changes the codec (new fields): a versioned contract
   migration (`record-2`) with read compatibility for `record-1`. About 12 items.
4. **Slice 4, store facade and compaction:** concurrency, snapshots, compaction; the TSan stress test. About 10 items.
5. **Slice 5, the viewer:** the web part, built by the `web-static` harness over the exported JSON. About 6 items.
6. **Slice 6, adoption:** `recall` and the briefing read through the store; the dreamer writes into it. Done by the squad on itself.

Total roughly 50 to 60 items: the size at which the per-item numbers of a run (section 3 of `PROVING_GROUND.md`) mean something.

## 5. What the suite checks beyond "the tests pass"

* **Properties, not only examples:** the crash property, the model-based test, differential tests, and a fuzzer over the query parser. These are what a
  small model's own example tests will not contain, so they are written by the harness or the architect and handed to testers as the oracle (the same
  mechanism as the web calculator's reference implementation).
* **Sanitizers:** the integrator builds everything under ASan and UBSan, and slice 4 under TSan.
* **Performance floor:** a fixed corpus of 100,000 entries must query in a stated time on this host (a regression gate, not a benchmark).
* **Self-hosting check:** at slice 6 the squad's recall results over a recorded run must match the old line-grep on a set of fixed questions, or beat it
  on a stated measure.

## 6. Risks

* **Too hard for a 9B at the storage layers.** That is information, not failure: it tells us where the ladder, divisions and contracts must carry the
  work, and which leaves need the larger local model. Slices 1 and 2 are the test of whether the method scales.
* **A bug in the store corrupts the squad's memory.** The store is adopted only at slice 6, behind a flag that is off by default, with the old recall kept
  until the golden comparison passes. The journal stays the authority; the store is a derived index of what the squad learned.
* **Scope creep into a general database.** The entry shape is fixed; there are no user schemas, no SQL, no network service.

## 7. Prerequisites in the harness

All of section 4 of `docs/PROVING_GROUND.md` (the `cpp-cmake` harness and its generalisations), plus a property-test helper in the harness's test
framework (a seeded generator and a shrink-free counterexample printer, so a failure prints its seed) and a sanitizer build in the integrator.
