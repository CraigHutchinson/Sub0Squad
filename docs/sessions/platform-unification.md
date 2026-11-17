# Follow-up session: one shared platform library for the Sub0 projects

Status: in progress (2026-11-17). Name decided: **Sub0Platform** (owner, 2026-11-17). A local repository
`D:\Craig\GitHub\Sub0Platform` holds the Python seed; the GitHub repository is not created yet (needs the org and the
owner's go-ahead). Other decisions below still open. Evidence: a review
of the CraigHutchinson and Kismotech repositories (2026-11-16, Haiku agent, main claims re-checked against the local
checkouts); Sub0Squad [../PLATFORM.md](../PLATFORM.md); Sub0LlmEngine `sub0llmengine/platform.py`.

## Goal

Stop solving the same operating-system problems in each project. Put the platform layer (processes, host facts,
paths, user directories, files and locks, sockets and local IPC, memory mapping, CPU topology) in one repository with
a C++ library and a Python package that share names and conformance tests, tested on Windows and Linux in CI. Domain
code stays where it is. Do not create the repository, rename anything or move code before the owner decides the name
and the open questions.

## What is known

**Two seeds already exist.** Sub0Squad's layer 0 (`include/sub0squad/platform_*.hpp` with `_posix`/`_win32` backends,
the layering rule in `cmake/CheckLayers.cmake`, Windows builds under MinGW-w64) for C++; Sub0LlmEngine's
`sub0llmengine/platform.py` (2026-11-16: `IS_WINDOWS`/`IS_WSL`, `localPath`, `spawnDetached`, `alive` by exact pid,
`terminate` of the process group, per-platform config layers; 51 tests pass on Windows and WSL; CI on both) for Python.

**Duplication found** (verified unless marked):

| Concern | Where it is solved today |
|---|---|
| Windows drive path to WSL `/mnt/x` | Sub0LlmEngine `platform.localPath`; Sub0LlmModelRegistry `store.py` `defaultRoot()`; NestNinja.pro `scripts/local-ci.ps1` |
| Windows argv quoting (CommandLineToArgvW rules) | Sub0Log `include/sub0log/child.hpp` (with round-trip tests); Sub0Squad `src/platform_process_win32.cpp` `quoted()` (no tests there) |
| Child processes: spawn, detach, stop, liveness | Sub0Squad `platform_process`/`platform_proc`; Sub0Log `child.hpp`; Sub0LlmEngine `platform.py`, `bench.py`; Sub0Math `tools/perf/sub0perf/platform_proc.py`; PowerShell launchers (Llm-OpenVino-ModelServer `serve.ps1`, Gemma4-LlamaCpp-AutoScript) |
| llama-server lifecycle | Sub0LlmEngine (Python); Gemma4-LlamaCpp-AutoScript and Llm-OpenVino-ModelServer (PowerShell); Sub0Squad `scripts/start-campaign.sh` (systemd) |
| Atomic replace (Windows retry on a file being read) | Sub0LlmEngine `activity.py` retries; its `bench.py`, the registry and Sub0TieredCache do not; Sub0Squad lists it as undecided |
| Host memory and CPU facts | Sub0Squad `platform_host` (in-process); Sub0LlmEngine `machine.py` (PowerShell per fact, up to 20 s each); Sub0Squad `scripts/campaign.py` (`/proc` only); Sub0Math; Sub0Llm `residency.cpp` |
| CPU affinity and topology | Sub0Llm `cpu_affinity.hpp`; Sub0Math; Sub0ECS `cpu_topology.hpp` |
| Memory mapping and uncached reads | Sub0Log, Sub0Pipeline `shared_region.cpp`, Sub0Llm `file_map.hpp`/`uncached_file.hpp`, Sub0MemPage (four rule sets) |
| Sockets | Sub0Squad `platform_net`; Sub0TieredCache `http_client.hpp` (own Winsock/POSIX code) |
| Locks | Sub0Squad `FileLock`; Sub0LlmEngine `GpuLock` (O_EXCL; since 2026-11-17 it takes over a crashed owner's lock) |
| Build tooling | CPM.cmake vendored in four repositories (Sub0Log, Sub0h264, NestNinja.pro, QuantumVoxelCraft); `git-semver` `.sh`/`.cmd` twins; `cmake-install` `.sh`/`.bat` twins (Sub0Pub) |

Not a conflict: Sub0LlmModelRegistry keeps *config* in `%APPDATA%` and Sub0TieredCache keeps *cache* in
`%LOCALAPPDATA%`; that is the usual Windows split (roaming config, local cache). The shared `dirs` module should
encode it (`userConfigDir` roaming, `userCacheDir`/`userStateDir` local).

**CI today**: Sub0Squad and Sub0LlmModelRegistry have none (Sub0LlmEngine gained Windows and Linux CI on
2026-11-17). Sub0Pipeline, Sub0Log, Sub0Pub, Sub0MemPage, Sub0TieredCache, Sub0ECS and others run Windows jobs.

**Defects found in passing** (not yet fixed):
- Sub0Squad `scripts/campaign.py` `mem_available_mib()` returns 1 TiB when `/proc/meminfo` cannot be read, so a memory guard passes silently.
- Sub0Squad `scripts/start-campaign.sh` defaults `MODEL` to a `/mnt/development/...` path that exists on one machine only (the integration brief replaces it with registry aliases).
- Sub0Pub `cmake-install.sh` starts `#/bin/bash -e` (missing `!`).
- Sub0LlmEngine `machine.py` starts PowerShell once per fact (slow; the shared `host` module should read them in-process).

## Proposal (names are the owner's call)

One repository, **Sub0Platform** (decided 2026-11-17), with three parts:

* **C++ library** (from Sub0Squad layer 0): `error`; `process` (spawn, wait, terminate, kill, captured runs, pipes,
  the Windows quoting from Sub0Log with its tests); `proc` (self path, start time, `isAlive(pid, start)`, descendants);
  `host` (memory, CPU times, load, free disk, GPU); `topology` (CPUs, cores, pinning); `file` (read, write,
  `writeFileAtomic` with the Windows retry, `FileLock`, append); `mapping` (read-only map, uncached reads, page-cache
  evict); `net` (TCP stream and listener, loopback); `ipc` (local sockets or pipes, same-user check); `wait`; `dirs`;
  optional `shm` (Sub0Pipeline's shared region, only on request).
* **Python package** (standard library only, Python 3.10+, from Sub0LlmEngine `platform.py`): `proc` (spawn
  detached, stop, alive, CPU seconds), `host` (memory, CPU model, GPU via nvidia-smi, VRAM used), `paths`
  (Windows/WSL mapping both ways), `dirs`, `fs` (atomic replace with retry, directory links), `lock` (exclusive lock
  with crashed-owner recovery).
* **tooling/**: one CPM bootstrap (replaces the vendored copies), the git-semver rule behind its two shims, toolchain
  probes (MSVC, clang-cl, MinGW, LLVM), and **conformance vectors** (JSON) that both packages' tests read: path
  mapping, argv quoting, pid and liveness cases, atomic replace.

Keep Sub0Squad's layering rule and its CMake check in the new library.

## Steps (after the decisions)

1. Create the repository with CI on Windows and Linux (MSVC and MinGW for C++; Python 3.10 and 3.12), and write the
   conformance vectors from today's behaviour.
2. Python first (lowest risk): move Sub0LlmEngine `platform.py` in, then `machine.py` facts and `activity.py`
   CPU-time sampling to `host`, `bench.py`/`activity.py` writes to `fs.atomic_replace`; Sub0LlmModelRegistry's path
   and config-root functions to `paths`/`dirs`; Sub0Squad `campaign.py` memory to `host.memory()` (fixes the 1 TiB
   default). Engine and registry depend on the package; keep a vendored fallback only if "standard library only"
   must hold for them.
3. C++: lift Sub0Squad layer 0 with its tests into the library once the other session's platform work in Sub0Squad
   has landed; Sub0Squad consumes it through CPM; add Sub0Log's quoting with its tests.
4. C++ consumers, one at a time with their CI green: Sub0TieredCache (`file`, `dirs`; its HTTP client stays and uses
   `net`), Sub0Llm (`mapping`, `topology`), Sub0ECS and Sub0Math (`topology`).
5. Shell twins: NestNinja.pro `local-ci` and install hooks become one Python entry point; Sub0Pub install scripts one
   `cmake -P` script; the vendored CPM copies point at the shared bootstrap.
6. The PowerShell llama-server launchers (Gemma4-LlamaCpp-AutoScript, Llm-OpenVino-ModelServer) either retire into
   Sub0LlmEngine backends or stay as Windows front ends: owner's call.

## Decisions needed from the owner

1. ~~The project's name~~: Sub0Platform (decided 2026-11-17). Still open: its org (CraigHutchinson or Kismotech) and
   visibility.
2. The C++ floor: C++23 with `std::expected` (Sub0Squad) or C++20 for Clang below 19 (Sub0Log's constraint); which
   Windows toolchains CI must cover (MinGW-w64 GCC and MSVC at least).
3. Whether Sub0Log depends on it (gains process and file dependencies) or keeps its own copy of the rules.
4. Whether the Python packages may depend on it, or must stay standard-library-only with a vendored copy.
5. The fate of the PowerShell launchers and of Sub0Pipeline's shared-memory module.
6. Kismotech repositories: licence and ownership before any code moves (KismoEngine is described as AGPL-3.0 or
   commercial; board-support repositories stay out of scope).

## Done means

The repository exists under the chosen name with Windows and Linux CI green; the Python package and C++ library pass
the shared conformance vectors; Sub0LlmEngine, Sub0LlmModelRegistry and Sub0Squad use it (their duplicate code
removed, their tests green on both systems); this brief gets its `## Outcome`.
