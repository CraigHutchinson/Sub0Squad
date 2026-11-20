# Development setup

Toolchain checked on the dev machine (2026-11-09): clang 21.1.8, CMake 4.4.5, Ninja 1.13.2, C++23 with `std::expected`.

## Dependencies: CPM, pinned for CI, overridable for local work

`cmake/dependencies.cmake` adds each Sub0 library with `CPMAddPackage` pinned to an exact revision, so a fresh machine or CI
builds reproducibly. For day-to-day work CPM honours `CPM_<NAME>_SOURCE` (the NAME given to `CPMAddPackage`, case-sensitive,
verified in CPM.cmake 0.43.2): the package then comes from that directory and **the pin is ignored**, so every project builds
against the local checkouts under the GitHub folder.

**On the Windows dev machine** these are user-level environment variables (set once; every new shell, IDE and agent session sees them):
`CPM_SOURCE_CACHE=%USERPROFILE%\.cache\cpm`, and `CPM_<Name>_SOURCE=<GitHub folder>\<Name>` (the folder holding the sibling checkouts) for every Sub0 checkout (the nine libraries below,
`Sub0Canvas`, `Sub0Squad`, `Sub0h264`) and `Version.cmake`. Local sources are the default for all the owner's projects; build a pin only on purpose
(clear the variable for that shell: `$env:CPM_Sub0Canvas_SOURCE = $null`). To add one:

```powershell
[Environment]::SetEnvironmentVariable('CPM_<Name>_SOURCE', '<GitHub folder>\<Name>', 'User')   # then open a new shell
```

On the Sub0 Linux dev machine this is set for the nine libraries by `~/.config/sub0/cpm-env.sh` (sourced from `~/.bashrc` and
`~/.profile`, and listed in `~/.config/environment.d/60-sub0-cpm.conf` for desktop-launched apps after a re-login):

| Variable | Checkout |
|---|---|
| `CPM_Sub0Pipeline_SOURCE` `CPM_Sub0Pub_SOURCE` `CPM_Sub0Log_SOURCE` `CPM_Sub0ECS_SOURCE` `CPM_Sub0Math_SOURCE` | `$SUB0_GITHUB_DIR/<Name>` |
| `CPM_Sub0MemPage_SOURCE` `CPM_Sub0TieredCache_SOURCE` `CPM_Sub0HexGrid_SOURCE` `CPM_Sub0Physics_SOURCE` `CPM_Sub0Canvas_SOURCE` | `$SUB0_GITHUB_DIR/<Name>` |

`SUB0_GITHUB_DIR` is the directory that holds the sibling checkouts; `. scripts/dev-env.sh` sets it to this repository's parent and exports every `CPM_<Name>_SOURCE`, so no machine-specific path is written anywhere (set `SUB0_GITHUB_DIR_OVERRIDE` to use another directory). A configure with the overrides active logs
`CPM: 'Sub0Pipeline' version overridden by environment variable`. To build the pinned revisions instead, unset the variables
(`env -u CPM_Sub0Pipeline_SOURCE ...`) or remove the sourcing lines from the shell startup files.

Trade-off to keep in mind: a local override can differ from the pin, so a result built that way is not reproducible by someone
else; record the library revisions with any measurement, and let CI use the pins.

Only projects that use CPM see these variables. Sub0Llm currently pulls Sub0MemPage and Sub0TieredCache with `FetchContent`,
which does not read them; converting those to CPM is a separate change.

## Checkouts

`Sub0Pipeline Sub0Pub Sub0Log Sub0ECS Sub0Math Sub0MemPage Sub0TieredCache Sub0HexGrid Sub0Physics Sub0Platform` are cloned next to this
project; update with `git -C <dir> pull`.

## CPM source cache

`CPM_SOURCE_CACHE=$HOME/.cache/CPM` is exported by `~/.config/sub0/cpm-env.sh` (and listed in `environment.d/60-sub0-cpm.conf`
for desktop-launched apps after a re-login), and on Windows `CPM_SOURCE_CACHE=%USERPROFILE%\.cache\cpm` is a user-level variable, so every
build directory, worktree and CPM project shares one download. It lives outside any repository on purpose: a cache under `out/` is lost to a
clean build and has to be filled again in every worktree. CPM downloads what is missing into it. It matters most when Boost's CMake tarball (about 102 MiB) is added in M3/M5; today it
holds simdjson and the pinned Sub0 libraries from a pinned-only build.

## Pinned-only build check (no local overrides)

```sh
env $(env | grep -o '^CPM_Sub0[A-Za-z]*_SOURCE' | sed 's/^/-u /') cmake -S . -B out/build/pinned -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
```
(then build and `ctest` with the same `env -u ...` prefix). Verified 2026-11-09: configures, builds and passes with the pins.

## Pin policy

Use the **latest release or default-branch head** of every dependency, recorded as an exact commit with its date. Checked
2026-11-09: Sub0Pipeline `3d11ffe`, Sub0Pub `504d772`, Sub0Math `1d20313` are the heads of `main`; simdjson v5.1.1; CPM.cmake 0.43.2;
Boost 1.92.0 to be pinned when first used. Bump deliberately, one dependency per commit.

## Tests and benchmarks

```sh
cmake --build out/build/dev && ctest --test-dir out/build/dev --output-on-failure     # about 50 tests, 6 to 12 minutes at -j1 under a process cap (STATUS.md section 3)
cmake --build out/build/dev --target Sub0Squad_Bench_check                             # sub0perf: judge against perf/baselines/crogbuntu1.json
cmake --build out/build/dev --target Sub0Squad_Bench_baseline                          # only with a reason, once a baseline exists
```
### Test tiers

Every test carries two ctest labels ([cmake/TestTiers.cmake](../cmake/TestTiers.cmake), checked at configure: a test without them fails it):
a **tier**, `smoke` or `full`, and an **area**. The smoke tier is a few fast tests from every area, run on every change; the full tier is
the rest, the long suites, run at the quality gates. The family's CI strategy (`CraigHutchinson/.github`, `docs/ci-strategy.md`) defines
when each runs; `ctest --preset quick` there means "everything not labelled `full`", which is exactly the smoke set here.

| Area | What it covers | Smoke | Full adds |
|---|---|---|---|
| `structure` | layering, link proofs, test reporting, the binary's selftest | layering, test_reporting, dependency_selftest, link_presentation | the other link proofs |
| `kernel` | durable jobs: job spec, journal, job store, shim, idle, guards, archive | test_job_spec, test_journal | job_store, shim, idle, host_guard, archive, adapter, m0_graph_growth (POSIX) |
| `workflow` | plans, the policy ladder, workflow stages, processes, predicates, stances, clarify | test_policy, test_workflow, test_plan | workflow_stages, workflow_mends, process, predicate, stance, clarify |
| `harness` | the project harnesses and their gates: diagnostics, edits, real builds | test_diagnostics (test_edits is in Sub0Canvas) | platforms, project |
| `agents` | executors from recorded replies: session, canvas, fork, planner, mends, blocks, turn control | test_agent, test_mends | canvas_agent, fork, planner, blocks, stream |
| `portal` | the portal page and the wizard | test_portal, test_portal_page (needs node) | wizard |
| `e2e` | a real daemon and CLI: scenarios, the README path, healing, near pass, thrash | test_e2e | readme, scenarios, executors, heal, nearpass, thrash |

```sh
ctest --test-dir out/build/dev -L smoke              # smoke tier: 15 tests, about 30 s native (every change, before a push)
ctest --test-dir out/build/dev                       # full tier: every test, about 13 min native (milestones, releases, before a PR merges)
ctest --test-dir out/build/dev -L full               # only the long suites
ctest --test-dir out/build/dev -L agents             # one area, both tiers (combine: -L smoke -L agents is the smoke tests of one area)
ctest --test-dir out/build/dev --print-labels        # the labels in use
```
With the presets (`CMakePresets.json`, build directory `out/build/default`): `cmake --preset default`, `cmake --build --preset default`, then
`ctest --preset quick` or `ctest --preset full`. A new test goes in one `sub0squad_label_tests` line of `cmake/TestTiers.cmake`: `smoke` only
when it is fast (a few seconds) and its area needs it. Tests run in parallel (`-j`) and beside a real daemon and portal, so a test never takes a
fixed port or a shared path: it binds port 0 and keeps the listener, or passes port 0 to the program (`--portal-port 0`, `SUB0SQUAD_PORTAL_PORT=0`)
and reads back the port it got, and it keeps its state in its own temporary `SUB0SQUAD_HOME`.

**CI** ([.github/workflows](../.github/workflows), callers of the reusable workflows in `CraigHutchinson/.github`, pinned by commit):

| Workflow | Trigger | Runs |
|---|---|---|
| `ci.yml` (smoke) | every push to `main` and every pull request, except documentation-only changes | Linux GCC 14, warnings as errors, `ctest --preset quick` |
| `ci-full.yml` (full) | tags `v*` and `milestone/*`, manual dispatch, a pull request labelled `ci:full`, weekly when `main` changed since the last full pass | Linux GCC 14 and Clang 19, Windows MinGW-w64 UCRT; `ctest --preset full` |

Pull request labels: `ci:full` runs the full tier, `ci:local-verified` accepts a `local-ci/full` status in its place, `ci:release` requires
the full tier, `ci:skip` runs no tier. The repository variable `CI_MODE` (`off`, `smoke`, unset) switches CI off or down to smoke.

**Local first** (the family standard, `CraigHutchinson/.github` [docs/ci-strategy.md](https://github.com/CraigHutchinson/.github/blob/main/docs/ci-strategy.md)):
hosted CI covers what this machine cannot (other compilers and systems, a clean-room build), and the complete matrix runs only at releases and milestones.
Everything else is verified locally before it is pushed:
* **The pre-push gate** (`.githooks/pre-push`; activate once per clone with `git config core.hooksPath .githooks`). Every pushed commit is scanned for
  private repository names, because this repository is public: `private_names.py` uses the untracked list in `~/.config/kismo/private-names.txt`. Then
  `out/build/default` is built and the quick tier run. A documentation-only push skips the build. Prove the hook with
  `python ../CraigHutchinson.github/tools/ci/hook_check.py --repo .` (a sibling clone of that repository).
* **Local attestation** (`.ci/local.json`): `python ../CraigHutchinson.github/tools/ci/local_ci.py --level quick|full` builds with GCC
  (MinGW-w64 on Windows; Linux Clang is covered by the full CI tier), runs the tier, sweeps for private names and checks the hook. It then posts a `local-ci/quick` or `local-ci/full` status
  on the pushed commit, which `ci:local-verified` accepts. Run `full` before a release or milestone, together with the tier 0 gate below.

**Model tiers** (a local quality gate: they need the local engine). A release or milestone also needs the **tier 0 gate** to PASS on the proving engine for
the build being released; tier 1 and above are run only on a tier 0 PASS ([BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md), "Model tiers and the tier 0 gate"):
```sh
python validation/tiers/make_t0.py . --list                                        # the cases
bash validation/tiers/check_t0.sh . <a cpp-cmake workspace>                         # every seed fails as its case says, no model
bash validation/tiers/run_t0.sh . 'qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=16384,slots=4' 3   # 3 runs of every case, then the gate
python validation/tiers/gate_t0.py . --engine qwen35                                # the gate alone: out/runs/t0/gate.md, exit 0 PASS / 1 FAIL / 2 INCOMPLETE
bash validation/tiers/run_tier.sh . 1 'qwen35=...'                                  # tier 1, refused unless tier 0 passed (--force "why" to override)
```

End-to-end tests start a real daemon and the real CLI under `SUB0SQUAD_HOME` inside the build tree. The fixture sets `SUB0SQUAD_EXIT_WITH` to
the test's own process (and passes `serve --exit-with`), so a test that ctest kills on its timeout leaves no daemon behind: the daemon stops
within a second of the test ending. The CLI gives up on a daemon that does not answer within 30 s (`SUB0SQUAD_REPLY_TIMEOUT` seconds; `wait`
is not bounded) with an error that says it may be hung. Run timing targets alone on a quiet machine. sub0perf git hooks and the after-build check are **off** by default (`SUB0MATH_PERF_GIT_HOOKS`,
`SUB0MATH_PERF_ON_BUILD`); an existing build directory keeps the values it cached, so pass them with `-D` there.

## A Windows host (WSL)

The program runs on Linux and natively on Windows (MinGW-w64, below). To run it the Linux way on a Windows host, build inside WSL (Ubuntu 24.04 with GCC 15 or clang, Ninja, CMake): clone inside WSL, or sync a Windows
checkout into a WSL directory, never build on `/mnt/c` or `/mnt/d` directly (slow, and the tests create Unix sockets). `.gitattributes` makes every text file
LF on checkout; a working tree made before it, or with `core.autocrlf=true`, still has CRLF files: the configure fails (`cmake/git-semver.sh`) and the golden
tests fail byte for byte. Convert them in the copy (`grep -rlIZ $'\r' . | xargs -0 sed -i 's/\r$//'`) or re-checkout. If the first configure failed, delete
the empty `$CPM_SOURCE_CACHE/version.cmake` it left. GCC is supported: two flow-based GCC diagnostics in third-party and generated code are warnings rather than
errors (`CMakeLists.txt`).

### A native Windows build

The program builds and runs natively: the daemon, shims, jobs, workflows and the CLI, and the full test suite runs ([PLATFORM.md](PLATFORM.md) section 6
lists what it took and what is left). Toolchain: MinGW-w64 with the UCRT runtime (GCC 16.2 is what was tested), CMake and Ninja on
`PATH`. Configure uses `cmake/git-semver.cmd` in place of the shell script. From PowerShell in the checkout:

```powershell
# CPM_SOURCE_CACHE and the CPM_<Name>_SOURCE overrides come from the user environment ("Dependencies" above); nothing to set here
cmake -S . -B out/build/mingw -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/mingw -j6
ctest --test-dir out/build/mingw --timeout 900 --output-on-failure -L smoke   # the smoke tier; drop -L smoke for the full suite
```

MinGW's GCC has no AddressSanitizer or UBSan runtime: the C++ harness probes for them and integrates without them there (its report says so). The
benchmarks' sub0perf baselines are per host name; a WSL instance on the same machine needs its own name (`-DSUB0MATH_PERF_HOST=...` at configure, for example `croglegion` natively
and `croglegion-wsl` in WSL), since both baselines live in the same `perf/baselines/` folder.


### Compact run monitoring

Use validation/tiers/watch_run.py for agent-friendly, one-line JSON status. It reads the existing artifacts and reports progress, observed PP/TG,
missing usage, and bounded events without loading complete traces into the caller's context. The gate and workflow ledger remain the authorities on
acceptance. A workflow snapshot identifies its status source. An archive or local state file reports the last recorded state.

- T0 snapshot: python -B validation/tiers/watch_run.py OUT_DIR
- Wait for a new result or terminal event: add --after CURSOR --wait 30
- Wait for the next behavior failure or terminal event: add --after CURSOR --wait 30 --wake failure
- Drill into one T0 result: add --details CASE/run-N
- Tier 1, Tier 2, or any live Squad workflow: pass its home and --exe PATH. Select the workflow with --workflow w-000001.
- Product tiers: pass the trial directory with workflow.json or verdict.json. For a workflow item add --details ITEM.
- Campaigns: pass the directory with campaign.jsonl. Use --after CURSOR for incremental records and --details SEED for one run.

Carry the returned cursor into the next poll on that same run. A response with wait_expired is an idle interval. The command does not cancel a workflow.
Use --limit to bound returned events. T0 and campaign cursors retain omitted failure events for the next page. T0 reports a completed behavior pass over
a frozen token ceiling as a token-overrun event and checks calibrated fixtures for retry warnings. These diagnostics appear in failure-only polls without triggering an early wake. Tier 1 and 2
require an enforced T0 report. A measurement-only cohort cannot satisfy their normal admission check.

### The engine on a Windows host

The model runs natively on Windows (CUDA), not in WSL: [Sub0LlmEngine](https://github.com/CraigHutchinson/Sub0LlmEngine), a sibling checkout, holds the
engine configuration, the per-machine bench history and the tuning page, and finds the weights in the machine's model registry (`D:\LlmModels`, see its
`README.md`). From the Sub0LlmEngine folder on Windows: `python -m sub0llmengine start` (and `status`, which prints the `--engine` line, the slot count
and the context per request to use; `serve` for the tuning page). The squad and the campaign in WSL then use that engine as it is: `scripts/start-campaign.sh`
starts no engine when one already answers at `ENGINE_HOST:ENGINE_PORT` (pass `SLOTS` and `SQUAD_CTX` from `status`).

WSL must be able to reach the engine: with WSL's default NAT networking, Windows' `127.0.0.1` is not WSL's. Use mirrored networking
(`networkingMode=mirrored` under `[wsl2]` in `%UserProfile%\.wslconfig`, then `wsl --shutdown`), after which `127.0.0.1:8092` is the same port on both sides.
