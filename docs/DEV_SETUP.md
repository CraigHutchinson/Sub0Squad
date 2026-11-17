# Development setup

Toolchain checked on the dev machine (2026-11-09): clang 21.1.8, CMake 4.4.5, Ninja 1.13.2, C++23 with `std::expected`.

## Dependencies: CPM, pinned for CI, overridable for local work

`cmake/dependencies.cmake` adds each Sub0 library with `CPMAddPackage` pinned to an exact revision, so a fresh machine or CI
builds reproducibly. For day-to-day work CPM honours `CPM_<NAME>_SOURCE` (the NAME given to `CPMAddPackage`, case-sensitive,
verified in CPM.cmake 0.43.2): the package then comes from that directory and **the pin is ignored**, so every project builds
against the local checkouts under the GitHub folder.

On the Sub0 dev machine this is set for the nine libraries by `~/.config/sub0/cpm-env.sh` (sourced from `~/.bashrc` and
`~/.profile`, and listed in `~/.config/environment.d/60-sub0-cpm.conf` for desktop-launched apps after a re-login):

| Variable | Checkout |
|---|---|
| `CPM_Sub0Pipeline_SOURCE` `CPM_Sub0Pub_SOURCE` `CPM_Sub0Log_SOURCE` `CPM_Sub0ECS_SOURCE` `CPM_Sub0Math_SOURCE` | `$SUB0_GITHUB_DIR/<Name>` |
| `CPM_Sub0MemPage_SOURCE` `CPM_Sub0TieredCache_SOURCE` `CPM_Sub0HexGrid_SOURCE` `CPM_Sub0Physics_SOURCE` | `$SUB0_GITHUB_DIR/<Name>` |

`SUB0_GITHUB_DIR` is the directory that holds the sibling checkouts; `. scripts/dev-env.sh` sets it to this repository's parent and exports every `CPM_<Name>_SOURCE`, so no machine-specific path is written anywhere (set `SUB0_GITHUB_DIR_OVERRIDE` to use another directory). A configure with the overrides active logs
`CPM: 'Sub0Pipeline' version overridden by environment variable`. To build the pinned revisions instead, unset the variables
(`env -u CPM_Sub0Pipeline_SOURCE ...`) or remove the sourcing lines from the shell startup files.

Trade-off to keep in mind: a local override can differ from the pin, so a result built that way is not reproducible by someone
else; record the library revisions with any measurement, and let CI use the pins.

Only projects that use CPM see these variables. Sub0Llm currently pulls Sub0MemPage and Sub0TieredCache with `FetchContent`,
which does not read them; converting those to CPM is a separate change.

## Checkouts

`Sub0Pipeline Sub0Pub Sub0Log Sub0ECS Sub0Math Sub0MemPage Sub0TieredCache Sub0HexGrid Sub0Physics` are cloned next to this
project; update with `git -C <dir> pull`.

## CPM source cache

`CPM_SOURCE_CACHE=$HOME/.cache/CPM` is exported by `~/.config/sub0/cpm-env.sh` (and listed in `environment.d/60-sub0-cpm.conf`
for desktop-launched apps after a re-login), so every build directory and CPM project shares one download. It lives on the
internal disk, outside any repository. It matters most when Boost's CMake tarball (about 102 MiB) is added in M3/M5; today it
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
| `platform` | the platform library: processes, files, sockets, waits, host facts | test_platform_process, test_platform_file | proc, host, net, wait |
| `kernel` | durable jobs: job spec, journal, job store, shim, idle, guards, archive | test_job_spec, test_journal | job_store, shim, idle, host_guard, archive, adapter, m0_graph_growth (POSIX) |
| `workflow` | plans, the policy ladder, workflow stages, processes, predicates, stances, clarify | test_policy, test_workflow, test_plan | workflow_stages, workflow_mends, process, predicate, stance, clarify |
| `harness` | the project harnesses and their gates: diagnostics, edits, real builds | test_diagnostics, test_edits | platforms, project |
| `agents` | executors from recorded replies: session, canvas, fork, planner, mends, blocks, turn control | test_agent, test_mends | canvas_agent, fork, planner, blocks, stream |
| `portal` | the portal page and the wizard | test_portal, test_portal_page (needs node) | wizard |
| `e2e` | a real daemon and CLI: scenarios, the README path, healing, near pass, thrash | test_e2e | readme, scenarios, executors, heal, nearpass, thrash |

```sh
ctest --test-dir out/build/dev -L smoke              # smoke tier: 18 tests, about 30 s native (every change, before a push)
ctest --test-dir out/build/dev                       # full tier: every test, about 13 min native (milestones, releases, before a PR merges)
ctest --test-dir out/build/dev -L full               # only the long suites
ctest --test-dir out/build/dev -L agents             # one area, both tiers (combine: -L smoke -L agents is the smoke tests of one area)
ctest --test-dir out/build/dev --print-labels        # the labels in use
```
With the presets (`CMakePresets.json`, build directory `out/build/default`): `cmake --preset default`, `cmake --build --preset default`, then
`ctest --preset quick` or `ctest --preset full`. A new test goes in one `sub0squad_label_tests` line of `cmake/TestTiers.cmake`: `smoke` only
when it is fast (a few seconds) and its area needs it.

**CI** ([.github/workflows](../.github/workflows), callers of the reusable workflows in `CraigHutchinson/.github`, pinned by commit):

| Workflow | Trigger | Runs |
|---|---|---|
| `ci.yml` (smoke) | every push to `main` and every pull request, except documentation-only changes | Linux GCC 14, warnings as errors, `ctest --preset quick` |
| `ci-full.yml` (full) | tags `v*` and `milestone/*`, manual dispatch, a pull request labelled `ci:full`, weekly when `main` changed since the last full pass | Linux GCC 14 and Clang 19, Windows MinGW-w64 UCRT; `ctest --preset full` |

Pull request labels: `ci:full` runs the full tier, `ci:local-verified` accepts a `local-ci/full` status in its place, `ci:release` requires
the full tier, `ci:skip` runs no tier. The repository variable `CI_MODE` (`off`, `smoke`, unset) switches CI off or down to smoke.

**Model tiers** (a local quality gate: they need the local engine). A release or milestone also needs the **tier 0 gate** to PASS on the proving engine for
the build being released; tier 1 and above are run only on a tier 0 PASS ([BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md), "Model tiers and the tier 0 gate"):
```sh
python validation/tiers/make_t0.py . --list                                        # the cases
bash validation/tiers/check_t0.sh . <a cpp-cmake workspace>                         # every seed fails as its case says, no model
bash validation/tiers/run_t0.sh . 'qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=16384,slots=4' 3   # 3 runs of every case, then the gate
python validation/tiers/gate_t0.py . --engine qwen35                                # the gate alone: out/runs/t0/gate.md, exit 0 PASS / 1 FAIL / 2 INCOMPLETE
bash validation/tiers/run_tier.sh . 1 'qwen35=...'                                  # tier 1, refused unless tier 0 passed (--force "why" to override)
```

End-to-end tests start a real daemon and the real CLI under `SUB0SQUAD_HOME` inside the build tree. Run timing targets alone on a
quiet machine. sub0perf git hooks and the after-build check are **off** by default (`SUB0MATH_PERF_GIT_HOOKS`,
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
$env:CPM_SOURCE_CACHE = "$PWD\out\cpm-cache"
cmake -S . -B out/build/mingw -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/mingw -j6
ctest --test-dir out/build/mingw --timeout 900 --output-on-failure -L smoke   # the smoke tier; drop -L smoke for the full suite
```

MinGW's GCC has no AddressSanitizer or UBSan runtime: the C++ harness probes for them and integrates without them there (its report says so). The
benchmarks' sub0perf baselines are per host name; a WSL instance on the same machine needs its own name (`-DSUB0MATH_PERF_HOST=...` at configure, for example `croglegion` natively
and `croglegion-wsl` in WSL), since both baselines live in the same `perf/baselines/` folder.

### The engine on a Windows host

The model runs natively on Windows (CUDA), not in WSL: [Sub0LlmEngine](https://github.com/CraigHutchinson/Sub0LlmEngine), a sibling checkout, holds the
engine configuration, the per-machine bench history and the tuning page, and finds the weights in the machine's model registry (`D:\LlmModels`, see its
`README.md`). From the Sub0LlmEngine folder on Windows: `python -m sub0llmengine start` (and `status`, which prints the `--engine` line, the slot count
and the context per request to use; `serve` for the tuning page). The squad and the campaign in WSL then use that engine as it is: `scripts/start-campaign.sh`
starts no engine when one already answers at `ENGINE_HOST:ENGINE_PORT` (pass `SLOTS` and `SQUAD_CTX` from `status`).

WSL must be able to reach the engine: with WSL's default NAT networking, Windows' `127.0.0.1` is not WSL's. Use mirrored networking
(`networkingMode=mirrored` under `[wsl2]` in `%UserProfile%\.wslconfig`, then `wsl --shutdown`), after which `127.0.0.1:8092` is the same port on both sides.
