# Handoff: state, findings and plan (saved 2026-11-09)

> **Read `docs/ITERATION_PROGRESS.md` first** (2026-11-12): it is the running log of the Fable iteration (what is built, what is proven without a model, what remains,
> the commands for the real-model measurements). The text below predates it and is kept for the history of the runs.


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
- **Project workflows** (the header-only sandbox harness and its tasks were removed 2026-11-12; see the README "First use"): `project submit` / `do "<task>"`: planner, level-triggered workflow engine (restart safe), per-item gates, integrator, ledger, stub frontier tier.
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
- Models on this drive (outside the repo, in `Models/` next to the `GitHub/` directory): `Qwen3.8-27B-GGUF/Qwen3.8-27B-UD-IQ1_S.gguf` and `Qwen3.8-27B-Q4_K_M-GGUF/Qwen3.8-27B-Q4_K_M.gguf` (sha256 verified).
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

## 5a. Performance investigation of llama.cpp on this host (2026-11-09; posted to Sub0Llm #95 and #98)

Research by a Sonnet agent (local read-only probes plus web), with the key local facts re-checked by me. **Verified here:** two 8-core CCDs (CPUs 0-7 and 8-15 on separate L3, 16-31 SMT siblings), AVX-512 with VNNI and BF16, `performance` governor, boost to 5.46 GHz,
THP `madvise`, memlock 8 MiB; the iGPU is a Raphael gfx1036 (2 CUs) with 1 GiB VRAM and 14.8 GiB GTT, no discrete GPU, no ROCm userspace; our `llama-server` was built `GGML_NATIVE=ON` with `-march=native`, OpenMP and CPU repack on; the snap launches with `--no-warmup`.
**Not verified:** RAM speed and channels (unreadable without root; the ceilings assume DDR5-5600 dual channel, 89.6 GB/s); the web-sourced iGPU numbers.

- **The ISA is not the gap.** Decode ceiling is about bandwidth / bytes per token: about 4 tok/s practical for the 16.8 GB dense Q4_K_M (measured 1.78, so up to about 2x headroom) and 30 to 50 for the 3B-active MoE (measured 8.3, so the MoE gap looks like software: expert gather and the hybrid Gated-DeltaNet CPU kernels).
- **Accelerators:** Vulkan on a 2-CU iGPU shares the same DDR5 (decode unchanged in every report found; prefill at best about 2x on larger iGPUs), ROCm for gfx1036 is unofficial with no success report: both low value, try Vulkan last on a small model.
- **Ranked experiments (none run yet; one variable at a time, warmed, machine otherwise idle, `llama-bench -p 2048 -n 128 -r 3` then one end-to-end run):** (1) decode threads 8/12/16 and CCD placement, including one thread per core across both CCDs
  (the Sub0Llm harness profile pins decode to CPUs 0-7, one CCD); (2) warm start instead of `--no-warmup` (the snap's cold 5 tok/s prefill); (3) `-b/-ub` for prefill; (4) Q4_0 (online repack) versus Q4_K_M versus IQ4_XS, MoE first; (5) flash attention and q8_0 KV;
  (6) n-gram speculation for repetitive code edits (flag name unverified against our build's `--help`); (7) one thread per core versus SMT; (8) Vulkan; (9) ROCm: skip.

## 5b. Ceilings measured (2026-11-09; supersedes the experiment order in 5a)

Measured with `sub0llm-machine --roofs all` (Sub0Llm branch `feature/78-machine-roofs`, worktree `Sub0Llm-78`, **local, not pushed**; built as three Haiku-implemented increments, reviewed and re-run here; two runs agree; posted to Sub0Llm #78 and #95):
DRAM read **32 to 35 GB/s, flat from 1 to 32 threads in every placement** (write 13.7, copy 29, triad 19.6); compute **2.6 TFLOP/s f32, 10.5 TOPS int8, 5.2 TFLOP/s bf16** (96 to 97 % of the assumed Zen 4 peaks).
**Confirmed by the owner (2026-11-09): only one DRAM slot is populated**, so the machine runs single channel (DDR5-5600 single channel peaks at 44.8 GB/s) and the measured plateau is a real hardware ceiling, not a harness artifact. A second DIMM (dual channel) would about double every decode number.

| | Ceiling | Measured | Verdict |
|---|---|---|---|
| Decode, Qwen3.8-27B Q4_K_M (16.81 GB per token) | 33 / 16.81 = 1.96 tok/s | 1.78 | **91 % of the ceiling: at the wall** |
| Decode, same model at IQ1_S (6.19 GB) | 5.3 tok/s | not run | quality unknown |
| Decode, 3B-active MoE (about 2 GB per token, estimate) | about 16 tok/s | 8.3 | about half: software headroom |
| Prefill, 27B dense (54 GFLOP per token) | 48 (f32), 97 (bf16), 195 (int8 dot) tok/s | 20 | 42 % of the f32 ceiling, about 10 % of the int8 one: large headroom |

**Consequences for the plan.** Threads, CCD placement and flash attention cannot raise dense decode (bandwidth-bound and flat). The levers that can: (1) **fewer bytes per token** (lower-bit quant, MoE); (2) **more tokens per weight read**: several sequences per batch (`-np N`: the aggregate decode ceiling scales with slots while compute has headroom, directly useful to parallel squad agents) and speculative or multi-token prediction
(the Qwen3.8 GGUF contains `blk.64.nextn.*`, an MTP head; our `llama-server` build shows only draft-model speculation flags, so MTP needs a newer build: unverified); (3) **prefill kernels** (int8 path, ubatch) where 5x headroom exists; (4) hardware: **populate the second DRAM slot (dual channel) or use another machine**.
Revised experiment order, each measured against these ceilings: **(a) aggregate tokens/s versus slots (1, 2, 4) on the 27B Q4_K_M and on the MoE; (b) MTP/n-gram speculation on a newer llama.cpp build (the snap's b9482 is the candidate to test; its `llama-server` path was not found under `runtimes/`); (c) quant tiers by bytes per token and quality; (d) prefill: ubatch and thread scaling; (e) Vulkan last (an iGPU shares the same single channel).**
Still to build (Sub0Llm increments): `sub0llm-fit` (#95) consuming the machine JSON; a squad job kind that runs `llama-server` sweeps and reports measured/ceiling; AVX2 path test; the Sub0Llm default test suite has not been run on the branch.

## 6. Resume

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release && cmake --build out/build/dev
ctest --test-dir out/build/dev --output-on-failure                    # about 2 minutes; run it with no model job active
out/build/dev/sub0squad serve --max-jobs 4 &                           # mock work: seconds
out/build/dev/sub0squad do "write a scientific calculator as a web page" --inject eval-tests:1 --inject engine
out/build/dev/sub0squad guard --memory-mib 17500                       # before loading a model
```


## 7. Update 2026-11-10: concurrency, memory, and the plan-act-iterate direction

- Built and pushed (7df930b): engine-slot leases (`submit --lease`, engine jobs lease their endpoint at `slots=`), near-pass repair (gate exit 3), retries seeded from the best earlier code.
- Measured on the Qwen3.6 snap (4 server slots): aggregate decode 6.95 tok/s at 1 request, 12.0 at 4 (1.7x, warm). **But 4 concurrent long prompts pushed the server to 25.4 GB resident on 29.6 GiB, 8 GB swap filled and decode fell to 0.6 tok/s.** Use `slots=2` here until leases are memory-aware (docs/WORK_GRAPH.md section 5).
- The modular calculator plan (`docs/calculator-web-modular.plan.json`, supplied with `project plan`) is untested end to end: the run was cancelled while swapping. No finished calculator page exists yet.
- Owner direction: per-item plan-act-iterate with size verdicts, strong delegation (skeleton plus signature delegation, rearchitect and respond back), a work graph plus an information graph: docs/WORK_GRAPH.md.
