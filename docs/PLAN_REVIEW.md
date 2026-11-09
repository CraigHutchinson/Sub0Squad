# Review of the M1 plan: gaps found (2026-11-09)

Method: re-read DESIGN Parts A and B and M1_PLAN against the new constraints (Windows and Linux first-class, macOS potential, a
diagnostics portal with metric/KPI access and liveness) and against what the neighbouring libraries actually ship [V: READMEs
checked: Sub0Pub CI covers GCC, Clang, AppleClang and MSVC on Linux, macOS and Windows; Sub0Math's sub0perf has Windows
paths; Sub0Log is a structured log that survives a hard kill]. Severity: **B** blocks M1 as written, **H** fix in M1, **M**
fix before the milestone named, **L** note.

## A. Platform (Windows + Linux now, macOS potential)

| # | Sev | Gap | Resolution (where recorded) |
|---|---|---|---|
| A1 | **B** | M1_PLAN is Linux-only by construction: `pidfd`, `PR_SET_CHILD_SUBREAPER`, `/proc` walking, `setsid`/process groups, `SIGTERM`/`SIGKILL`, `SO_PEERCRED`, `XDG_RUNTIME_DIR`, `fsync`, `flock`. Windows has none of them. | A **platform seam** (DESIGN section 20): `IProcessSupervisor` (spawn into a kill-able group, wait handle, tree enumeration, verify-empty), `IIpcEndpoint` (control channel + peer identity), `IRuntimePaths`, `IDurableFile` (append, flush, atomic replace), `IHostFacts` (cores, memory, affinity). Linux backend first, Windows backend with its own gate. M1_PLAN gains a platform section. |
| A2 | **B** | "Cancel leaves no child" has no Windows definition. | Windows backend uses a **Job Object** (`KILL_ON_JOB_CLOSE`, `JobObjectBasicProcessIdList` to verify empty, `TerminateJobObject` after a cooperative stop). Descendants cannot escape a job (no subreaper trick needed). Linux keeps subreaper + `/proc`; **macOS** would use a process group + `kqueue EVFILT_PROC` + `libproc` and cannot guarantee escapee capture (documented limit, not a gate). |
| A3 | H | Graceful stop is POSIX signals. Windows has no SIGTERM for arbitrary children. | Cooperative stop is **a protocol, not a signal**: a stop message on the event channel (the worker's control side) and a stop file/event the shim watches; signals / `CTRL_BREAK_EVENT` are the courtesy step, forced kill is the last. Workers must opt in; non-cooperating ones get the forced path after the grace period. |
| A4 | H | Event fd (`fd 3`) is POSIX. | Event channel = an **inherited pipe handle** named by `SUB0SQUAD_EVENT_FD` (POSIX fd number) / `SUB0SQUAD_EVENT_HANDLE` (Windows handle value); same NDJSON. |
| A5 | H | Control socket path and identity. | POSIX: unix socket, 0600 dir, `SO_PEERCRED`. Windows: `AF_UNIX` is available on current Windows but peer identity is weaker; the plan uses a **named pipe with an owner-only ACL** and `GetNamedPipeClientProcessId` + token check behind `IIpcEndpoint`. Frames stay `FrameReader` length-prefixed. |
| A6 | H | Pid reuse defence uses `/proc/<pid>/stat` starttime; liveness uses pidfd. | `ProcessIdentity{pid, startTime}` abstracted: Linux starttime ticks, Windows `GetProcessTimes` creation time + a process handle kept open by the shim; macOS `proc_pidinfo` start time. |
| A7 | H | Durability: `fsync`, `rename`, `flock`. | `IDurableFile`: Linux `fdatasync` + `rename`; Windows `FlushFileBuffers` + `ReplaceFileW`/`MoveFileExW`, and the single-instance lock via a named mutex or exclusive-open lock file. Journal segment naming and torn-tail rule are platform-neutral. |
| A8 | M | Paths: `$XDG_RUNTIME_DIR`, `~/.cache`. | `IRuntimePaths`: Linux XDG; Windows `%LOCALAPPDATA%\Sub0Squad` (state) and `\\.\pipe\` (control); macOS `~/Library/Application Support`. Never a hard-coded path in logic. |
| A9 | M | Core sets and memory budget (M2) differ: Windows has processor groups (more than 64 logical CPUs) and no cgroups; affinity masks are per group. | `IHostFacts` returns a **topology** (groups, cores, SMT siblings, NUMA), the arbiter leases topology ids, not raw masks. This host is 32 threads, one group. Recorded for M2. |
| A10 | M | Portal binds loopback on all platforms, but on Windows (and any multi-user host) another local user can reach a loopback port; a unix socket is protected by file mode. | Portal always requires the **per-session token** for everything except `/healthz`; Host/Origin check against DNS rebinding; token file ACL'd to the user. |
| A11 | H | **No way to test Windows from this machine.** The earlier dev machine was Windows, but this session cannot run it. | Honest rule: M1 is "done on Linux" only when its gates pass here; "done on Windows" needs a Windows run. That needs a CI workflow and therefore a remote (owner decision) or a Windows host. Until then Windows code is written to the seam and compiled in CI only when CI exists; it is **not claimed working**. |
| A12 | M | sub0perf is Python and the lock lives under `.git/sub0perf`; the dev workflow assumes Python is installed on every platform. | Accepted (it is the family tool). The squad itself has **no Python dependency at runtime**; Python is needed only for the performance tooling and for the Python harness it wraps. |
| A13 | L | macOS: no subreaper, no cgroups, `kqueue`, SIP/entitlement limits on affinity (no hard pinning). | Out of priority. Seam keeps it possible; capabilities are *reported* per platform (`HostFacts.canPinCores=false`), so the arbiter degrades visibly rather than lying. |

## B. Diagnostics portal: metrics, KPIs, liveness

| # | Sev | Gap | Resolution (DESIGN section 21) |
|---|---|---|---|
| B1 | **B** | The portal is served by the daemon, so a **wedged or dead daemon cannot diagnose itself**. | **Offline diagnostics**: `sub0squad doctor` and `sub0squad portal --offline` read the journal, ledger, metric rollups and shim records read-only and show "daemon unreachable, state as of T", including `lost` jobs. Same renderer as the live portal. |
| B2 | **B** | No definition of **liveness vs readiness vs work-liveness**. | Three layers: `/healthz` answered from the reactor thread (proves the loop turns), `/readyz` (journal writable, disk headroom, control endpoint bound, engines probed), and per-job **work liveness** (last-event age against the tool's declared `expectedEventIntervalSec`). All exposed as metrics and on the portal. |
| B3 | H | Metric access is described as pages, not as a **contract**: no versioned API, no query shape, no export. | Versioned `/api/v1`: `/jobs`, `/jobs/{id}`, `/jobs/{id}/events` (SSE, resumable with `Last-Event-ID` = journal seq), `/metrics` (Prometheus text), `/metrics/query?name&labels&from&to&step`, `/kpis`, `/ledger?group_by`, `/resources`, `/engines`, `/budget`, `/decisions`, `/audit`, `/tools`, `/health`. The CLI (`sub0squad metrics|kpis|export`) and MCP (`squad_metrics`, `squad_kpis`) are read-only clients of the same `ControlApi`. |
| B4 | H | **KPIs are undefined**: "frontier share", "cost per accepted candidate" are named but not specified. | A **KPI registry as data** (`kpi-1`): name, formula over metric and ledger names, window, unit, thresholds, plain-language definition; the portal shows the formula and inputs next to every value (transparency). Built-ins listed in DESIGN section 21; tools add their own through the manifest. |
| B5 | H | **Stale data looks live.** | Every API value carries `asOf` and `source` (`live` / `journal` / `rollup`); SSE sends a keepalive comment every 15 s and the page marks itself stale after two missed; the offline path says so loudly. The same rule as "never `running` when `lost`". |
| B6 | H | Event overload: a chatty worker could flood the daemon or block on a full pipe. | Event channel is non-blocking on the worker side with a **drop counter** reported in the next event; the daemon reads with a bounded queue and exports `squad_events_dropped_total`; a per-job rate limit is a manifest field. |
| B7 | M | Portal idle cost conflicts with live charts. | Sampling is demand-driven (already in section 15): 1 s only while a client is attached; the portal sends a `watching` heartbeat, not a poll storm. Measured with sub0perf in M3. |
| B8 | M | No retention/size policy for rollups, ledger, logs. | Retention config with defaults (rollups per section 15, logs size-capped with rotation, ledger never rewritten but compactable into signed segments); disk-low shows in `/readyz`. |
| B9 | M | Daemon's own **diagnostic log** is unspecified; risk of a homegrown logger. | Use **Sub0Log** (typed records, no producer formatting, survives a hard kill) for the daemon's diagnostics; `SIGUSR1` (Windows: a control-pipe command) dumps internal state into it. Open question: whether the journal can also sit on Sub0Log's durable stream rather than its own writer (evaluate in M1 step 2, do not duplicate if it fits). |
| B10 | L | Wedge detection from outside. | Optional systemd `Type=notify` + `WatchdogSec` (Linux user unit example) and a Windows service recovery policy; both documented, off by default. |

## C. Other gaps in the M1 plan itself

| # | Sev | Gap | Resolution |
|---|---|---|---|
| C1 | **B** | **Threat model for `submit`**: a command line submitted over the control channel is arbitrary code execution as the user. Fine for the owner's CLI, **not** for the MCP identity or any agent. | Two submit paths: owner CLI may submit a raw command (M1); the **agent/MCP path may only submit a registered tool manifest by name with typed parameters**, never a raw command. Manifests are owner-installed. Recorded in DESIGN section 16 and M1_PLAN. |
| C2 | H | The **job spec** (submit payload) is not specified: cwd, env allow-list, argv, stdin, resource hints, owner, tool, tier, data class, wall budget, resume command. | `job-spec-1` JSON schema written in M1 step 1 and versioned; environment is allow-listed, not inherited wholesale (secrets). |
| C3 | H | **Log capture** unspecified: stdout/stderr merge, timestamps, size cap, rotation, binary garbage, follow semantics. | Per-job `stdout.log`/`stderr.log` with a capture byte cap and rotation, tail/follow by offset, never parsed for state. |
| C4 | H | **Protocol and journal versioning**: client/daemon skew, journal schema upgrades. | Handshake carries `protocol` and `journal` versions; unknown newer journal is refused read-write, offered read-only; migrations are append-only upgrade records. |
| C5 | H | **Single instance and daemon lifecycle**: duplicate `serve`, `SIGTERM` to the daemon, restart storms. | Single-instance lock; clean shutdown **leaves jobs running** (the shim owns them) and snapshots; `serve --stop-jobs` is explicit; restart reconciles as already planned. |
| C6 | H | Without the arbiter (M2) nothing limits concurrency in M1. | A simple `maxParallelJobs` (default 2) in M1, replaced by leases in M2; recorded so it is not mistaken for scheduling policy. |
| C7 | M | Time: wall clock steps would corrupt durations and quotas. | Durations from the monotonic clock, timestamps wall-clock UTC; ledger stores both. |
| C8 | M | Wall-time budgets and per-attempt timeouts are not enforced in M1. | Enforced by the shim (kill after `wallSeconds`, state `failed:timeout`, distinct from cancel). |
| C9 | M | Disk full, journal write failure, permissions. | Journal failure puts the daemon into **refuse-new-work, keep-supervising** mode, visible in `/readyz`; never silently drops transitions. |
| C10 | M | Resume semantics for non-harness tools. | `resumable` + `resumeCommand` live in the manifest; resume of a non-idempotent tool requires confirmation flag. |
| C11 | M | Sub0Pub cross-process layout compatibility remains [U]. | M1 does not put Sub0Pub on the wire: the control channel is JSON frames. Sub0Pub stays in-process until M3 verifies layout checks. |
| C12 | M | Provenance for accountability: who ran which tool version. | Job record stores tool manifest hash, binary hash of the command (as the harness pins identities), host facts snapshot. |
| C13 | L | Test portability of the e2e gates. | The e2e scenarios drive the `mock-worker` through the `IProcessSupervisor` seam, so the same scenarios run on each backend; only the "escapee" scenario differs in mechanism. |

## Changes made because of this review

DESIGN: new sections 20 (platform seam) and 21 (diagnostics access, KPIs, liveness); section 16 gains the agent-submit rule.
M1_PLAN: platform section and revised steps (seam first, job-spec, logs, handshake, single instance, offline doctor).
DECISIONS: items 19 to 21. ROADMAP: M1 split into M1 (Linux backend, all gates) and M1w (Windows backend, gated on a Windows run).
