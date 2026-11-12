# Knowledge store: scope and information outline for the engineering work

Status: draft 2026-11-12 for owner review. This is the **input the architect step and the workers need**: what is in and out, the exact information the
project is about (entities, formats, grammar, formulas, signatures), the non-functional limits, the test oracles, and the first slice as work packages. It
refines `docs/KNOWLEDGE_STORE.md` (why and layers) into something a plan can be written from. Working title only; the name is the owner's call. It is a sandbox
project under `examples/`, comparable to the squad's register (`docs/WORK_GRAPH.md` section 19), never linked into the daemon.

## 1. Purpose, users, success

* **Purpose:** a small embeddable library and command line that remembers what a team of workers learned: short entries (lesson, pitfall, fact, decision, question)
  with evidence, searchable by words and scope, with a lifecycle (supersede, archive, challenge, decay).
* **Users:** a program (the library API) and a person at a terminal (the CLI); a viewer page reads an export.
* **Success (the project):** the layers below exist with their tests; the crash property and the model-based property hold on 1,000 random cases each; a corpus of
  100,000 entries answers a typical query within the performance floor (section 6); a recorded run of the squad loads and answers the fixed comparison questions.
* **Success (as a proving ground):** the squad builds it with no manual mends on the 35B, and the per-item numbers (attempts, rework share, tokens, time) are recorded.

## 2. Scope

| in | out (explicitly) |
|---|---|
| append-only durable log with crash recovery; one writer, many readers in one process | a network service, multi-process writers, replication |
| entries with scope, kind, evidence, confidence, basis; supersede, archive, challenge, settle, expire, decay | user-defined schemas, SQL, joins, transactions across entries |
| full-text search (BM25) with scope, kind, author, time, state filters; stable ranking | stemming, synonyms, language detection, fuzzy matching, embeddings |
| compaction; export to JSON for the viewer | encryption, access control, compression |
| CLI (`know`); a static web viewer over an export | a GUI toolkit, pixel capture |
| standard library only, C++20, no exceptions across the public API | third-party dependencies (CRC and BM25 are written here) |

## 3. Information outline

### 3.1 Entities

**Entry** (immutable once written; changes are new log records that refer to it)

| field | type | rule |
|---|---|---|
| `id` | string | `e-` + 8 lowercase hex digits, assigned by the store from a counter; unique, never reused |
| `scope` | string | `/`-separated path of lowercase `[a-z0-9_.-]` segments, 1 to 6 segments, at most 120 characters (`project/harness/persona/node`); the empty scope is not allowed |
| `kind` | enum | `lesson`, `pitfall`, `fact`, `decision`, `question` |
| `text` | string | 1 to 600 bytes of UTF-8, no control characters other than a single `\n`; leading and trailing space trimmed |
| `evidence` | list of `Evidence` | 0 to 8 items |
| `confidence` | number | 0.0 to 1.0, two decimals (stored as an integer 0 to 100) |
| `by` | string | author role, `[a-z0-9_-]` 1 to 32 characters |
| `at` | integer | seconds since the Unix epoch, UTC, supplied by the caller's clock |
| `basis` | `Basis` | `{conditional: bool, condition: string}`; condition is 1 to 120 bytes iff conditional |

**Evidence**: `{file: string (1 to 200 bytes, relative path), line: integer >= 1}`.

**Challenge** (a record about an entry): `{entryId, by, at, text (1 to 300 bytes)}`; an entry has at most one open challenge.

**State of an entry** is derived, never stored: `current`, `challenged` (current with an open challenge), `archived` (superseded or expired). A superseded entry
keeps `supersededBy` (an entry id) and `reason` (1 to 200 bytes).

### 3.2 Invariants (each one a test)

1. Ids are unique and increase with the order of `add` records. 2. An archived entry never returns to current. 3. `supersededBy` forms no cycle and points to an entry
that exists and is current or itself archived by a later supersede. 4. Reading a log never yields a record that was not completely written. 5. The state of every
entry after `open` equals its state before the process stopped, up to the last completely written record. 6. Queries are deterministic: same log, same query, same
`now` gives the same result in the same order.

### 3.3 Storage format `record-1`

A log is a directory of segments `segment-000001.know`, `segment-000002.know`, ... Each segment starts with the 6 bytes `KNOW1\n`, then records back to back:

```
u32 length (little endian)   payload size in bytes, 1 to 65,536
u32 crc    (little endian)   CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320, init and final xor 0xFFFFFFFF) of the payload
payload                      one canonical JSON object, no newline inside
```

Golden vector: CRC-32 of the ASCII bytes `123456789` is `0xCBF43926`. The writer rolls to a new segment when a segment reaches 4 MiB (a setting). Opening
reads segments in order; the first record whose length is out of range, whose bytes are missing, or whose CRC differs ends the log of that segment, and the
bytes after it are ignored (a later append truncates them away). A bit flip anywhere in a record is detected.

Payload objects have a `t` field naming the operation, keys in this fixed order: `add` `{t,id,scope,kind,text,evidence,confidence,by,at,basis}`,
`supersede` `{t,id,by,at,newId,reason}`, `challenge` `{t,id,by,at,text}`, `settle` `{t,id,by,at,outcome}` with outcome `upheld` (the entry stays) or `withdrawn`
(the challenge is dropped), `expire` `{t,id,at}`, `use` `{t,id,at}` (an entry was read into a prompt; feeds decay). Unknown `t` values are skipped, which is how
`record-2` stays readable by `record-1` readers (versioning rule: new fields are optional, new operations are new `t`).

### 3.4 Lifecycle state machine

`add` creates **current**. `challenge` makes it **challenged** (only if there is no open challenge). `settle upheld` returns it to current; `settle withdrawn`
returns it to current and records that the challenge was withdrawn. `supersede` (allowed from current or challenged) archives it and records the replacement.
`expire` archives a current entry whose effective confidence fell below the threshold. No other transitions exist; an operation from a state that does not allow
it returns an error and writes nothing.

### 3.5 Time-dependent values (pure functions of the entry, `now` and the harness version)

* `ageDays = max(0, (now - lastUsedOrAt) / 86400)`, where `lastUsedOrAt` is the latest `use` record or `at`.
* `halfLifeDays`: lesson 90, pitfall 180, fact 365, decision 365, question 30.
* `effectiveConfidence = confidence * 0.5^(ageDays / halfLifeDays)`; if the entry is conditional and the caller states its condition no longer holds, multiply by 0.25.
* An entry **expires** when `effectiveConfidence < 0.10`.

### 3.6 Query language

```
query    := term*
term     := filter | word
filter   := key ":" value
key      := "scope" | "kind" | "by" | "since" | "state"
word     := one or more characters other than whitespace and ':' at the first position of a filter
```

* `scope:a/b` matches entries whose scope is `a/b` or below it (`a/b/c`); repeated `scope:` filters are alternatives. `kind:` and `by:` take one value each; a repeated
  filter of the same key is an alternative. `since:` takes an integer epoch seconds or `Nd` meaning N days before `now`. `state:` is `current` (default), `challenged`,
  `archived` or `any`.
* Different keys combine with AND. Words are tokenised the same way as entry text: lowercase, split into maximal runs of `[a-z0-9]`, keep runs of length 2 or more, no
  stemming, no stop words. A query with no words returns the filtered entries ordered by `at` descending then `id` ascending.
* **Ranking (BM25)**: for a query term set Q (distinct tokens) and an entry document d (its text plus the scope segments as tokens):
  `score(d) = sum over q in Q of idf(q) * f(q,d) * (k1 + 1) / (f(q,d) + k1 * (1 - b + b * |d| / avgdl))`, with `k1 = 1.2`, `b = 0.75`,
  `idf(q) = ln(1 + (N - n(q) + 0.5) / (n(q) + 0.5))`; N is the number of **current-or-challenged** entries and n(q) the number of them containing q. Entries with score 0 are
  dropped when the query has words. Order: score descending, then `at` descending, then `id` ascending. Scores are compared after rounding to 6 decimals.
* **Worked example** (three entries, scope tokens ignored here): e1 "gate failed because the export is missing" (7 tokens), e2 "the export names must match the contract export" (8), e3 "retry the same
  file wastes an attempt" (7); avgdl = 7.3333. Query `export contract`: e2 = 1.5758, e1 = 0.4789, e3 not returned. Query `retry attempt`: only e3, 1.9988.

### 3.7 Public API (the contract between layers; namespace `know`)

```cpp
struct Evidence { std::string file; int line; };
struct Basis { bool conditional = false; std::string condition; };
enum class Kind { lesson, pitfall, fact, decision, question };
enum class State { current, challenged, archived };
struct Entry { std::string id, scope, text, by; Kind kind; std::vector<Evidence> evidence; int confidence; long long at; Basis basis;
               State state; std::string supersededBy, reason; };
struct Hit { Entry entry; double score; };
template <class T> struct Result { bool ok; T value; std::string error; };     // no exceptions across the API

class Store {
public:
  static Result<Store> open(const std::filesystem::path& dir, Options opts = {});   // creates the directory; recovers the log
  Result<std::string> add(NewEntry e, long long now);                               // returns the id
  Result<bool> supersede(const std::string& id, const std::string& newId, std::string by, std::string reason, long long now);
  Result<bool> challenge(const std::string& id, std::string by, std::string text, long long now);
  Result<bool> settle(const std::string& id, std::string by, bool upheld, long long now);
  Result<bool> use(const std::string& id, long long now);
  Result<std::vector<Hit>> query(const std::string& text, long long now, int limit = 20) const;
  Result<Entry> get(const std::string& id) const;
  Result<int> compact();                    // rewrites live segments; returns bytes saved; every current entry and the archive survive
  Result<std::string> exportJson(long long now) const;   // schema know-export-1
};
```

### 3.8 Command line

`know --dir D add --scope S --kind K --by B [--confidence N] [--evidence file:line]... TEXT` prints the id. `know --dir D query [--now T] [--limit N] QUERY...` prints one line per hit:
`<score 4 decimals>  <id>  <kind>  <scope>  <text, first 80 characters>`. `know --dir D supersede ID NEWID --by B --reason R`, `challenge ID --by B TEXT`, `settle ID --by B upheld|withdrawn`,
`export` (JSON to stdout), `verify` (reads every record, prints `ok N records` or the first bad offset and exits 1). Errors go to standard error as `know: <message>` with exit 1. Every command
takes `--now` (epoch seconds, default the system clock) so transcripts are reproducible.

### 3.9 Export schema `know-export-1` (what the viewer reads)

`{ "schema": "know-export-1", "now": <int>, "entries": [ { "id","scope","kind","text","by","at","confidence","effectiveConfidence","state","supersededBy","reason","challenge": {"by","at","text"}|null,
"evidence":[{"file","line"}], "basis":{"conditional","condition"} } ], "scopes": [ "a", "a/b", ... ] }`, entries ordered by `at` descending then `id`; `scopes` is the sorted set of every scope prefix.

## 4. Non-functional requirements

* C++20, standard library only; warnings are errors (`-Wall -Wextra -Werror`); clean under AddressSanitizer and UndefinedBehaviorSanitizer; the store facade clean under ThreadSanitizer (slice 4).
* No exceptions cross the public API; a failed operation changes nothing and says why.
* Determinism: no call reads the clock; time is an argument.
* Memory: the index for 100,000 entries stays under 200 MiB. Durability: after `add` returns, the record is written and flushed to the OS; `fsync` is an option (default on `compact`, off per `add`).
* Portability: Linux; paths via `std::filesystem`; little-endian explicit encode and decode (no struct dumps).

## 5. Test strategy and oracles (what testers must write, what the harness supplies)

| layer | tests the tester writes | oracle the harness or architect supplies |
|---|---|---|
| codec | round trip for generated entries; the CRC golden vector; every single-bit flip detected | the golden vector above; a generator with a fixed seed |
| log | append, reopen, many segments; **truncate at every byte offset and reopen: a prefix of the appended entries, never a partial or reordered one** | the in-memory list of appended entries |
| index | postings for generated entries match a scan | a naive linear scan |
| query | tokenisation cases; the worked example above; filters; tie-breaks; limit | the worked example numbers; a naive reference ranker |
| lifecycle | each allowed transition; each refused transition; decay and expiry arithmetic | **model-based**: a random sequence of 500 operations against an in-memory model, compared after each step and after a reopen |
| store | reader sees a consistent snapshot while a writer appends; compaction keeps every current entry and the archive | TSan stress; the model |
| cli | golden transcripts with fixed `--now` | the transcripts |
| viewer | DOM tests over a fixed export | a golden export file |

Tests print the seed of a failing random case and are repeatable from it.

## 6. Performance floor (a regression gate, not a benchmark)

100,000 entries of 200 bytes: `open` under 2 s from a compacted log; a three-word query under 50 ms; `add` under 1 ms on average; on this host, measured in a release build.

## 7. Slice 1 as work packages (the architect refines; ids follow `test-NAME`, `dev-NAME`)

| id | persona | files | depends on | gate |
|---|---|---|---|---|
| `test-codec` | tester | `tests/codec.test.cpp` | none | at least 8 tests incl. the CRC golden vector and the bit-flip property; at least 70 % fail on the stub |
| `dev-codec` | developer | `src/codec.cpp` (`include/know/codec.hpp` is the contract, supplied) | `test-codec` | all pass |
| `test-log` | tester | `tests/log.test.cpp` | none | at least 8 tests incl. the truncate-at-every-offset property; fail on the stub |
| `dev-log` | developer | `src/log.cpp` | `test-log`, `dev-codec` | all pass |
| `test-scan` | tester | `tests/scan.test.cpp` | none | at least 6 tests: add then linear-scan query, filters by scope and kind |
| `dev-scan` | developer | `src/store.cpp` (slice-1 facade: `open`, `add`, `get`, scan `query`) | `test-scan`, `dev-log` | all pass |
| `test-cli` | tester | `tests/cli.test.cpp` | none | at least 5 golden transcripts |
| `dev-cli` | developer | `src/main.cpp` | `test-cli`, `dev-scan` | all pass |
| `int-slice1` | integrator | none | all of the above | whole suite under ASan and UBSan; a 10,000-entry add then query smoke; `know verify` ok |

The contract headers (`codec.hpp`, `log.hpp`, `store.hpp`) are **harness-supplied** (section 3.7), so every developer item's tests import names that exist (the interface-gap defect of catalogue
rows 25 and 28). The harness supplies the CMake file, the check framework and `app::run` (the C++ platform's scaffold) plus a generator helper `tests/helpers/gen.hpp`
(seeded entries and random operations).

## 8. What each role is given (the briefing outline)

* **Architect:** sections 1 to 7, the layer table of `KNOWLEDGE_STORE.md`, the platform brief (`cpp-cmake`), the plan-2 syntax and the naming rule.
* **Tester (per layer):** the layer's contract text from sections 3.3 to 3.9 (only its own), the signatures it may call, its golden vectors and the generator, the minimum counts and the stub rule.
* **Developer (per layer):** the same contract text, the accepted tests (whole when they fit, else a folded view with `READ`), the signatures of the layers it depends on (not their bodies), the failure text and numbered draft on a retry.
* **Integrator:** the gate list, the sanitizer and smoke commands, the report template.
* **Every role:** at most the few hundred tokens that apply; nothing about layers it does not touch.

## 9. Definition of done (per slice)

All items accepted without a manual mend; the integration gate passes under the sanitizers; the properties pass on their full case counts; the performance floor holds (from slice 2); the run is archived with
its build sha; the per-item numbers are in the report; catalogue rows exist for every step-in.

## 10. Open decisions for the owner

1. The product name (working title only). 2. Segment size 4 MiB and the decay half-lives (3.5) are starting values; confirm or change. 3. Whether slice 5's viewer should be read-only (recommended) or editable.
4. Whether `use` records (prompt reads) belong in the first slice or wait for the lifecycle slice (recommended: wait; slice 1 ignores `use`).
