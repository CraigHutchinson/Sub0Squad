# Follow-up session: run the squad on Sub0LlmEngine and Sub0LlmModelRegistry

Status: open (written 2026-11-16). Backlog: [../BACKLOG.md](../BACKLOG.md), *Next*, "The engine and models from
Sub0LlmEngine and Sub0LlmModelRegistry". Evidence and tools: Sub0LlmEngine's `README.md` and `docs/TUNING_MAP.md` (the
consumer contract), Sub0LlmModelRegistry's `spec/LAYOUT.md`.

## Goal

Make Sub0Squad use the two sibling projects for its engine and models instead of building its own `llama-server` command
line: Sub0LlmEngine starts, tunes and reports the engine; Sub0LlmModelRegistry names and resolves the weights. Every run
then records exactly which weights, build and engine configuration produced it. Keep the current path working until the
new one has run a campaign; do not remove it in the same change.

## What is known

* `scripts/start-campaign.sh` (Linux/WSL, systemd user units) builds `llama-server` from `LLAMA=`
  (`../Sub0Llm/out/build/llama-optimizer/bin/llama-server`), `MODEL=` (a file path), `CTX`, `SLOTS`, `SQUAD_CTX`,
  `THREADS`. It already uses an engine that answers at `ENGINE_HOST:ENGINE_PORT` as it is, without managing it
  (uncommitted change in this checkout, 2026-11-16: check `git diff scripts/start-campaign.sh` first).
* Sub0LlmEngine (`../Sub0LlmEngine`, Python 3.10+, standard library only; Windows and Linux):
  * `python -m sub0llmengine [--config FILE] start | stop | status [--json]`. `status --json` / `start --json` print one
    JSON document (`sub0llmengine.engine.v1`: `running`, `url`, `alias`, `slots`, `nCtxPerSlot`, `squadCtx`, `model.id`
    and `model.path`, `build`, `config`, `matchesConfig`/`differences`, `benchLock`). `start` exits 0 when an engine
    answers, 1 on failure, 2 while a bench holds the GPU; it never starts a second engine on an answering port. The
    contract is in Sub0LlmEngine's README, "Using the engine from another project" (tested in `tests/test_process.py`).
  * `python -m sub0llmengine model <alias>` gives the registry id, path and GGUF facts (KV bytes per token).
  * The plain `status` also prints the line a client needs: `squad:  --engine <alias>=<url>,model=<alias>,ctx=<squadCtx>,slots=<n>`.
  * Config layers: `config/engine.defaults.json` < `engine.local.json` < `$SUB0LLMENGINE_CONFIG` < `--config FILE`.
    `squadCtx` is the context a client should plan per request.
  * `python -m sub0llmengine selection engine-selection.json --out chosen.engine.json` turns the tuning page's
    *Export selection + evidence* into a config layer; `--config chosen.engine.json start` serves it.
  * Benches take `out/bench.lock` and refuse while the engine serves; the engine binds 127.0.0.1.
  * `python -m sub0llmengine axes` gives the knobs and the engine's build label; the registry facts include KV bytes
    per token (`kvBytesPerTokenQ8`), which E2 (the window plan) needs.
* Sub0LlmModelRegistry (`../Sub0LlmModelRegistry`, registry folder `D:/LlmModels` on the reference machine,
  `$SUB0_MODEL_REGISTRY` elsewhere): `python -m sub0llmregistry resolve <alias>` gives the file path; ids look like
  `hf/<org>/<repo>@<revision>#<file>`. Aliases now: `qwen36-35b-a3b`, `qwen38-27b-q4_0`, `qwen2-0.5b`,
  `gemma4-26b-a4b`, `qwen38-flash-next` and others (`python -m sub0llmregistry list`).
* Models to avoid for now: Qwen3.8-27B UD-IQ3_XXS (wrong with CUDA offload; use `qwen38-27b-q4_0`) and
  Qwen3.8-Flash-Next (its first long prefill after a load differs from later ones). See Sub0LlmEngine `docs/issues/`.

## Steps

1. **Engine through Sub0LlmEngine.** In `scripts/start-campaign.sh`, an `ENGINE=sub0llmengine` mode (or the default once
   proven): start with `python -m sub0llmengine --config <squad layer> start --json`, read `slots`, `squadCtx`, `url` and
   `alias` from its JSON instead of `SLOTS`/`SQUAD_CTX`, refuse when `matchesConfig` is false (or offer to restart), and
   stop with `stop` on `stop-all`. Exit code 2 means a bench holds the GPU. Keep the systemd containment where the engine runs on this
   host; keep the "engine already answering" path.
2. **Models by alias.** Accept `MODEL=<alias or registry id>` and resolve it with `sub0llmregistry` (a plain file path
   still works).
3. **Record provenance.** `run-info.json` and the campaign report carry the registry id, the engine build label and the
   effective engine configuration (`model.id`, `build`, `config` from `status --json`, or the selection file it came from). Add it to the portal's run info if cheap.
4. **Window plan input (E2).** Read KV bytes per token from the registry facts (or Sub0LlmEngine's `axes`) rather than
   parsing the engine's start log.
5. **Bench lock.** A campaign start refuses while a bench runs: `start` exits 2 and `status --json` names `benchLock`.
6. **Prove it.** One short campaign (or one `sub0squad do` run) on the reference model through the new path, then an
   update to `docs/DEV_SETUP.md` and the README's engine section.

## Done means

`scripts/start-campaign.sh` can run a campaign whose engine Sub0LlmEngine started and whose model is named by registry
alias; `run-info.json` names the registry id, build and engine configuration; the old path still works; DEV_SETUP
describes both; the backlog item is updated and this brief gets its `## Outcome`.

## Environment and constraints

* Reference machine: Windows 11, RTX 5070 Laptop 8 GB, Ultra 9 275HX, 63 GB RAM; campaigns have run under WSL/Linux
  with systemd user units. Sub0LlmEngine and the registry sit beside this repo under `D:\Craig\GitHub`.
* Owner's rules: ask before any download or install; nothing leaves the machine (frontier stays off, engine on
  127.0.0.1); no commits without the owner's go-ahead (this checkout has uncommitted `docs/BACKLOG.md` and
  `scripts/start-campaign.sh` changes, and Sub0LlmEngine has uncommitted work); ask before changes to Sub0Pipeline or
  Sub0Pub; changes to Sub0LlmEngine or the registry needed by this work go in those repos, agreed with the owner first.
* One GPU job at a time: do not start a campaign while Sub0LlmEngine benches or spikes.
