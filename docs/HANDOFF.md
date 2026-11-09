# Handoff: state, findings and plan (saved 2026-11-09)

Repository: https://github.com/CraigHutchinson/Sub0Squad (public, MIT, `main`). Linux only; Windows is deferred by decision. Read
[DESIGN.md](DESIGN.md) (source of truth), [DECISIONS.md](DECISIONS.md), [ROADMAP.md](ROADMAP.md) first; this file is the current picture.

## 1. Why work paused

The workflow machinery works, but **LLM throughput on this host is the bottleneck** and blocks real-model iteration:

| Engine on this CPU (Ryzen 9 8945HX, 29.6 GiB, no GPU) | Decode | Prefill | Practical cost of one 3,000-token item |
|---|---|---|---|
| Qwen 3.6 35B-A3B (Ubuntu `qwen3-6` snap, MoE, 3B active) | about 8.3 tok/s | about 5 tok/s cold | 5 to 7 minutes |
| Qwen3.8-27B Q4_K_M (dense, llama.cpp from the Sub0Llm reference build) | **1.8 tok/s** | 20 tok/s | **about 28 minutes** |
| Qwen3.8-27B IQ1_S (1-bit) | not measured | | earlier harness run: `no_proposal` x6 (quality) |
| native thinking on the 3.6 model | | | 14,000 tokens of reasoning, no answer, 32 minutes |

Decision of the owner: **optimise the LLM engine on this hardware in a new session, or move development to a different machine.** Until then, develop the
harness with the mock agents (seconds, deterministic) and use real models only to measure.

## 2. What exists and is verified (13 ctest tests, all passing at the last full run (2026-11-09, idle machine))

- **M1 (Linux):** daemon (epoll reactor, one Sub0Pipeline graph per job), CLI, journal (torn-tail safe), per-run shim (own session, child subreaper, verified process-tree teardown),
  `lost` detection, adoption after a daemon crash, event-channel progress, state-file adapter (`optimization-run-1`), offline `status`, idle at 0 CPU ticks (`test_idle`).
  Gates met: cancel leaves no child, dead supervisor is `lost`, status without logs, idle. Partly met: resume on the real harness (see section 5).
- **Sandbox harness (docs/SANDBOX.md):** `project submit` / `do "<task>"`: planner, level-triggered workflow engine (restart safe), per-item gates, integrator, ledger, stub frontier tier.
- **Web project (docs/SQUAD_UX_AND_HARNESS.md, RUNNING_PROJECTS.md):** triage-first `do`, plan-2 (personas tester/developer, work cards with a token budget, declarative gates), the `web-static` harness
  (scaffold, fake DOM, node test runner), test-first handoff, `awaiting-plan` + `project plan` (the deferred architect is a file handoff, nothing is sent), the `calculator-web` blueprint.
- **Escalation ladder (docs/OPTIMIZATION.md):** retry, **decompose** (model decides the division, plan amended with dependency rewiring, depth 2), **send back to the producer** (tester disputed and re-run),
  **rearchitect** (card rewrite job), escalate to `frontier-stub` once, fail at 8 attempts. Unit-tested as a pure function and through the daemon with mock agents.
- **Agents and engines:** `llm-agent` (work card + fixed system block + previous-failure excerpt, per-segment token accounting, prompt treatments `baseline|index|planfirst`, in-attempt
  **verify-and-repair loop** with stagnation stop), `llm-architect`, engine profiles (loopback only; recommended sampling per mode, `min_p`, `presence_penalty`, `reasoning_effort`), persona routing per engine,
  `optimize-job` (one item in isolation under each treatment, gated without publishing).
- **Guards (explicit, new):** `sub0squad guard` (free memory and disk with a reserve, exit 1 when refused), `submit --memory-mib N` (refused at submit with the numbers), `fetch-model` (guarded, resumable, sha256-verified
  Hugging Face download, run as a job; it uses curl because TLS is not built in yet).
- **Performance verification:** Sub0Math's sub0perf (`Sub0Squad_Bench`, baseline `perf/baselines/crogbuntu1.json`); git hooks are off by default.

## 3. Real-model findings (details: SQUAD_UX_AND_HARNESS.md section 8, OPTIMIZATION.md sections 3 to 5)

1. Both real workflow runs failed at `engine`: the tester items passed first time, but the model-written tests **contradicted the contract** (so even the reference implementation failed them) and the developer attempts left 1 to 34 of 12 to 65 tests failing.
2. Temperature 0.2 made retries reproduce the same mistake; fixed to the cards' recommended sampling. Qwen3.6 has no thinking levels; Qwen3.8 has `reasoning_effort` xhigh|medium|low (checked in the templates).
3. External verify-and-repair works (7 failing, then 1 after one turn) but stagnates on a stubborn belief (`-2^2`); stagnation stop added. Input treatments change the failure profile but run-to-run variance (1 to 11) exceeds their effect at 2 samples each.
4. A model-proposed division was rejected by the validator (one item had no files): the safety net worked, JSON discipline did not.
5. **An engine kept as a job occupies a scheduler slot.** With `--max-jobs 1` a `llama-server` job blocked the experiment job for 10 minutes. Engines must be leased services outside the job slots (M2/M5: arbiter, EngineCaps).

## 4. Environment facts for the next session or machine

- Libraries are consumed through CPM with `CPM_<Name>_SOURCE` overrides (`~/.config/sub0/cpm-env.sh`, shared cache `CPM_SOURCE_CACHE=~/.cache/CPM`). Pinned-only build verified once; re-check before any release.
- Models on this drive (outside the repo): `/run/media/craig-hutchinson/54D4C5011B8AF586/Models/Qwen3.8-27B-GGUF/Qwen3.8-27B-UD-IQ1_S.gguf` and `.../Qwen3.8-27B-Q4_K_M-GGUF/Qwen3.8-27B-Q4_K_M.gguf` (sha256 verified).
  The `llama-server` used is Sub0Llm's reference build: `.../Sub0Llm/out/build/llama-optimizer/bin/llama-server` (0.9.0-dev, commit cf659f2). The Ubuntu snap `qwen3-6` serves on 127.0.0.1:8342 and unloads after idle; it holds about 25 GB while loaded.
  Stopping the snap needs root (`sudo snap stop qwen3-6`); the owner gave permission, but it was not needed once the idle unload happened.
- Test and experiment state lives under `out/` (gitignored): `out/build/dev/*-home`, `opt1` (an isolated `engine` item with correct fixed tests), `real1-home`, `real2-home`. Safe to delete.
- Leftover from a development accident (a recursive spawn bug, since fixed with a recursion guard): about 63,000 small directories `/tmp/s0q-*`. The auto-mode classifier blocked the wildcard delete; clean them with
  `find /tmp -maxdepth 1 -name 's0q-*' -exec rm -rf {} +` when convenient.

## 5. Plan, in priority order

**A. Make iteration affordable (the blocker).**
1. Decide: new machine (GPU or faster CPU) versus engine work here. For engine work, Sub0Llm issues #90 (concurrency, EngineCaps), #95 (fit predictions), #100 (model providers), #101 (chat template and API feedback) are filed.
2. Streaming responses in `llm-agent` (a non-streaming 30-minute call is silent) with progress and cancel-on-close; bounded repair re-prompts (latest code plus failure only) instead of replaying history.
3. Model selection by data: run the isolated `optimize-job` loop per engine and quant with at least 5 rounds, report pass rate per cost; try Qwen3.8 `reasoning_effort` low and medium.

**B. Close the loop on the real model.** Run the full `calculator-web` workflow with the ladder and repair loop (not run yet); build the test-dispute path against real tester output (the mock proves the mechanics only); decide on tester-test validation without a reference.

**C. Finish the foundation.** M1 leftovers: remote sub-graph smoke (`SUB0PIPELINE_BUILD_REMOTE`), `continue` (new run from a stalled harness's continuation packet; running the real `iterate.py` needs a sandbox clone of Sub0Llm), cooperative stop message, log rotation, Sub0Log for the daemon, `doctor` and the offline portal.
M2 arbiter (core sets, memory budget, model slots **separate from job slots**, measurement window taking the sub0perf lock) and the ledger as the quota source. M3 metric registry, tool manifests, SSE, portal v0 (Boost.Beast + Asio behind `IHttpFront`). M3b quotas and share cap, the owner-only permission gate for any live frontier tier, MCP tools.

**D. Owner decisions still open:** the final name (working title Sub0Squad), enabling any real frontier tier (never without an explicit owner switch), Windows timing.

## 6. Resume

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release && cmake --build out/build/dev
ctest --test-dir out/build/dev --output-on-failure                    # about 2 minutes; run it with no model job active
out/build/dev/sub0squad serve --max-jobs 4 &                           # mock work: seconds
out/build/dev/sub0squad do "write a scientific calculator as a web page" --inject eval-tests:1 --inject engine
out/build/dev/sub0squad guard --memory-mib 17500                       # before loading a model
```
