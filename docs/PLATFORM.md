# Platform: one library for everything the operating system does

**Moved (2026-11-19).** The platform layer is Sub0Platform, a separate library (its `cpp/` directory, pinned in
`cmake/dependencies.cmake`). Sub0Squad includes its `sub0platform/<module>.hpp` headers (`error`, `process`, `proc`, `host`, `file`,
`net`, `ipc`, `wait`); its `platform` layer, its copies of those sources and its tests are deleted here. The record below describes the
layer as built in this repository, under its old `platform_*` names.

Status: 2026-11-16. Owner direction: good platform abstractions, first of all to keep a good shape to the code; a native Windows build is the second
reason, not the first. **Built:** the `platform` layer (layer 0, `cmake/Layers.cmake`, with its `link_platform` proof), the system-header check (slice 0)
and `platform_process` with its test (slice 1). The launchers of `harness_common`, `host_guard`, `wizard`, `replay`, `first_use` and `supervisor` run on it;
the shim followed in slice 5. Slice 2: `platform_proc` and `platform_host`, with tests; `process_tree` is gone, the four `/proc/self/exe` reads
and the two `/proc/meminfo` parsers are one each, and `ProcessId` replaced `pid_t` in the public headers. Slice 3: `platform_file`, with a test; the
journal, ledger, progress, frames and the event channel use it, and the eleven private copies of `slurp`/`readFile`/`writeFile`/`writeFileAtomic` in
`src/` are gone (two shared helpers, `harnessutil` and `agent`, forward to it). The tests' one `std::system` now runs through the launcher.
The 21 text-mode `std::ofstream` writes are gone (onto `writeFile`, `writeFileAtomic` and `appendToFile`, or binary mode for the streamed sinks).
**Windows backends** (2026-11-16): each built module has a `_win32.cpp` sibling, and the platform layer, its tests and its link proof
build and pass natively under MinGW-w64 (GCC 16.2, UCRT); the platform tests start a helper of our own (`tests/platform_child.cpp`) instead of `/bin/sh`.
Slice 4: `platform_net` and `platform_ipc` (one `SocketStream` and one `SocketListener` for TCP and the local socket) and `FileLock`, with POSIX and
Windows backends and a test; the engine client, the portal, the control client, the port probe, the daemon's lock, listener and peer check, and the
two socket fixtures of the tests use them, and `unix_socket.*` is gone. `findOnPath` and `isExecutable` joined `platform_proc`.
Slice 5: `platform_wait` (a `Waiter` for sockets, process exits, a wake-up and stop requests), pipes, a replaced environment and the event pipe in
`platform_process`, with POSIX and Windows backends and a test; the daemon's loop, the shim, the log follower and `mock_worker` use them, and
`KNOWN_SYSTEM_USERS` is empty: no file outside the platform layer includes an operating-system header. Evidence: a survey of `src/` and `tests/` on that
date, checked by an independent review of this page against the code; the Windows state is from a native build on that date.

## 1. Why, before any Windows

About 300 operating-system calls and 20 `/proc` paths sit in 44 files. Before this work 28 source files, 3 public headers and 8 test files included an
operating-system header. The cost today is not portability. It is **the same thing done many ways**:

| Concern | Done separately in | Ways |
|---|---|---|
| Start a child, capture its output, time it out | `harness_common.cpp` (fork, pipe, poll, kill the child only on timeout), `host_guard.cpp` (fork, pipe, no timeout), `wizard.cpp`, `replay.cpp`, `first_use.cpp`, `supervisor.cpp` (posix_spawn, each with its own file actions), `shim.cpp` (fork with pre-exec steps) | 7 launchers, 2 mechanisms, 3 capture loops, 2 timeout rules; only some closed inherited descriptors |
| The program's own path | `cli_tools.cpp` (twice), `scenario.cpp`, `build_id.cpp` | `/proc/self/exe` four times |
| Host memory, CPU, load | `host_guard.cpp`, `portal_sampler.cpp` | two parsers of `/proc/meminfo` |
| Process identity, trees and exits | (slice 2 moved the first two into `platform_proc`) `process_tree.cpp` (`/proc/PID/stat`), `first_use.cpp` (`/proc/PID/cmdline`), `daemon_jobs.cpp` (`pidfd_open` through a raw `syscall`, one `waitpid`), `shim.cpp` (subreaper, `waitpid(-1)`) | `pid_t` in three public headers |
| Sockets | `unix_socket.cpp` (the daemon socket: the `/proc/self/fd` trick around the path-length limit, `flock`, `SO_PEERCRED`), `engine.cpp` (HTTP client to the engine), `portal_impl.hpp` (HTTP server), `first_use.cpp` (port probe), `client.cpp` | raw descriptors and errno |
| Waiting for events | `daemon_impl.hpp` (epoll, eventfd, signalfd, timerfd), `shim.cpp` (epoll), `cli_jobs.cpp` (inotify to follow a log) | three loops |
| Durable files | `shim.cpp` (`fsync`), `journal.cpp`, `ledger.cpp`, `progress.cpp`, `frame.cpp`, `event_channel.cpp` (raw descriptors), `paths.cpp` (modes) | descriptor code inline |
| Tests | `check.hpp` (`mkdtemp`, used by every test), `test_scenarios.cpp:57` (`std::system`), tests that find processes by their `s0q-` names in `/proc` | |

Each copy had its own edge cases. One component fixes them once and tests them once. It also keeps `pid_t`, `errno` and `/proc` out of the rest of the
code, which is worth doing on Linux alone. Slice 1 also removed headers that five files included and never used (`harness_web`, `sandbox`, `cli`,
`cli_project`, and inotify from `cli_tools` and `client`).

## 2. The rule

**Only the platform layer includes operating-system headers** (`<unistd.h>`, `<sys/*.h>`, `<signal.h>`, `<poll.h>`, `<spawn.h>`, `<fcntl.h>`,
`<dirent.h>`, `<arpa/*.h>`, `<netinet/*.h>`, and later `<windows.h>`, `<winsock2.h>`) **or reads `/proc`.** Everything else uses its interfaces. The
`layering` test checks it (`cmake/CheckLayers.cmake`). The files that broke the rule were listed in `KNOWN_SYSTEM_USERS`, each with the slice that moved
it; slice 5 emptied the list. It stays as the mechanism: a file that starts using the system directly fails the test.

## 3. Shape

* **Its own library.** Sub0Platform is the layer below the kernel. Every Squad layer may use it, and it uses nothing else of ours, which its own
  `layering` test checks (`docs/COMPONENTS.md`). Interfaces are `sub0platform/<module>.hpp`. Each module has up to three sources (the layer files
  are flat, now in Sub0Platform's `cpp/src`):
  `src/platform_<m>.cpp` for what every system shares (the pure parsers, whole-file reads and writes), and one backend, `src/platform_<m>_posix.cpp` or
  `src/platform_<m>_win32.cpp`, which CMake picks by `SUB0SQUAD_OS`. The layering check counts a backend as its stem. `src/platform_win32_util.hpp`
  (UTF-8 and UTF-16, the system's error text, a closing `HANDLE`) is private to the Windows backends. Strings cross the interface as UTF-8; paths are
  `std::filesystem::path`, which is wide on Windows, so code above the layer passes the path or `.string()`, never `.c_str()` to a `char` API.
* **Types, not descriptors.** Handles are move-only and close themselves (`Child`, and later `TcpStream`, `LocalStream`, `Listener`, `FileLock`). A
  `ProcessId` (64-bit) replaces `pid_t`, and argv is a `std::vector<std::string>`, never a shell string.
* **Errors:** `std::expected<T, PlatformError>`. `PlatformError` holds the operation, the native code and a message that says what failed on what path
  ("cannot start 'g++' in /x/y: No such file or directory"). No errno is read outside the layer.
* **Parsers separate from readers.** `/proc` text is parsed by pure functions that take the text (`parseMeminfo(std::string_view)`), so they are tested with
  fixtures on any platform; the reader is a few lines.
* **Small surface, named for what the caller wants**, not for the system call: `runCaptured`, not `forkExecPipe`.

## 4. The modules

| Module | Interface | Replaces | POSIX | Windows |
|---|---|---|---|---|
| `platform_process` (**built**) | `ProcessSpec{argv, cwd, outputFile, appendOutput, outputPerms, nullInput, newSession, environment (replaces), pipeOutput, eventPipeVariable, memoryVictimScore}`; `spawn -> Child`; `Child::wait(timeout)`, `terminate()`, `kill()` (the whole group for a new session, even after its leader ended), `takeStdout/takeStderr/takeEvents -> Pipe{readSome(timeout)}`, `reapOrphans()`; `writeToInherited(handleText, data)` for the event pipe; `runCaptured(spec, {timeout, maxBytes, includeStderr}) -> Captured{status, output (the end of it), timedOut, truncated}` | the launchers above, the shim's fork; later `std::system` in the tests | `posix_spawnp` with `closefrom`, an empty signal mask and default SIGPIPE; the event pipe as descriptor 3; `oom_score_adj` written from the parent just after the start | CreateProcessW with an explicit handle list and a UTF-16 environment block; the event pipe's handle value in the variable; a Job object per group to kill the tree; no out-of-memory preference |
| `platform_proc` (**built**) | `selfExecutable()`, `currentProcess()`, `processStart(ProcessId)` and `isAlive(id, start)` (identity against PID reuse), `descendants(ProcessId)`, `commandLine(ProcessId)`, `terminateProcess(ProcessId)`, `killProcess(ProcessId)`, `becomeSubreaper()`, `ignoreBrokenPipes()`, `ignoreStopRequests()`, `setProcessName`, `crash()`, `setEnvironment`/`unsetEnvironment`; pure `parseProcStat`, `parseCmdline` | `process_tree.cpp`, the four readlinks, `build_id.cpp`, `first_use.cpp`, `getpid` in `harness_cpp.cpp`, the shim's `prctl` and `mock_worker`'s signals; the tests' lookups by `s0q-` name | `/proc`, `prctl`, signals | Toolhelp32 snapshot, process creation time, `QueryFullProcessImageNameW`; `terminateProcess` sets the target's stop event when it has one (a process that watches for stop requests), else `TerminateProcess` |
| `platform_host` (**built**) | `memory() -> {totalMiB, availableMiB, swap, commitHeadroomMiB}`, `cpuTimes()`, `loadAverage() -> optional`, `freeDiskMiB(path) -> optional`, `gpu() -> optional`; pure `parseMeminfo`, `parseCpuTimes` | `host_guard.cpp`, `portal_sampler.cpp` | `/proc/meminfo`, `/proc/stat`, `/proc/loadavg`, statvfs, `/sys/class/drm` | `GlobalMemoryStatusEx` (including the commit limit), `GetSystemTimes`, no load average (nullopt), `GetDiskFreeSpaceExW` |
| `platform_file` (**built**, except the temporary directory) | `readFile`/`readFileText`, `writeFile`, `writeFileAtomic`, `modifiedSeconds`, `AppendFile{open, append, sync, truncate}`, `appendToFile`, `writeAll`/`readSome` on a descriptor; later `FileLock` (with the daemon socket, slice 4) and a temporary directory; the owner-only directory is `std::filesystem::permissions` | `journal.cpp`, `ledger.cpp`, `progress.cpp`, `frame.cpp`, `event_channel.cpp`, `paths.cpp`, `fsync` in `shim.cpp`, `mkdtemp` in `check.hpp` | O_APPEND, fsync, rename, flock, chmod, mkdtemp | FlushFileBuffers, `MoveFileExW(REPLACE_EXISTING, WRITE_THROUGH)` with a retry while a reader holds the file, LockFileEx, an ACL |
| `platform_net` (**built**) | `SocketStream{writeAll, readSome(timeout) -> nullopt on time out, 0 at the end, native, release}`, `SocketListener{accept(timeout), port, native}`; `connectTcp(host, port)`, `listenLoopback(port)` (127.0.0.1 only; 0 lets the system choose), `waitForConnection(listeners, timeout)`, `loopbackPortInUse(port)` | `engine.cpp`, `portal.cpp`, `first_use.cpp`, `tests/fake_engine.hpp`, `tests/test_stream.cpp` | BSD sockets, non-blocking and close-on-exec, poll, MSG_NOSIGNAL | Winsock 2, non-blocking and not inherited, WSAPoll, SO_EXCLUSIVEADDRUSE (a held port is refused as on POSIX) |
| `platform_ipc` (**built**) | `listenLocal(path)`, `connectLocal(path) -> SocketStream or {error, noServer}`, `localSocketFile(path)`, `removeLocal(path)`, `clearStaleLocalSockets()`, `peerIsSameUser(stream)`, `peerUser(stream)` | `unix_socket.cpp` (gone), the socket code in `client.cpp` and `daemon.cpp` | AF_UNIX with the `/proc/self/fd` bind, umask 0177, `SO_PEERCRED` | AF_UNIX (Windows 10 1803 and later), the path in UTF-8; one over 107 bytes moves to a hash of it under `%USERPROFILE%\.sub0squad\sockets` (`localSocketFile`; not AppData, which packaged apps see virtualized), removed when the daemon stops and cleared at start when stale; no peer credentials, so the owner-only state directory is the check |
| `platform_wait` (**built**) | `Waiter{watchSocket(socket, read, write, tag), unwatchSocket, watchExit(id, tag), unwatchExit, watchStopRequests(tag), wake() (any thread), wait(timeout) -> [WaitEvent{kind, tag, readable, writable, hangup, status}]}`; a timer is the wait's timeout. The log follower polls four times a second instead of a change notification | `daemon_impl.hpp` (epoll, eventfd, signalfd, pidfd), `shim.cpp` (epoll, signalfd, timerfd, `waitpid(-1)`), inotify in `cli_jobs.cpp` | epoll, eventfd, a signalfd for SIGTERM and SIGINT (blocked in the calling thread and every thread it starts later), pidfd; an exited child is reaped and its status reported | one event for every socket (`WSAEventSelect`) with `WSAPoll` for the state each wait, `WaitForMultipleObjects` (at most 60 processes), a named stop event (`Local\sub0squad-stop-<pid>`) plus `SetConsoleCtrlHandler`; Job objects stand in for the subreaper |
| `platform_limits` | in `ProcessSpec`: a process count, a memory cap (the out-of-memory preference is `memoryVictimScore`, slice 5) | `ulimit` in scripts and tests | setrlimit, `oom_score_adj`, a cgroup when the service runs in one | Job object limits (active processes, job memory) |

`cpp_templates.cpp` has `popen` only inside the text of a generated project's helper, not in our code: it belongs to the C++ harness's toolchain profile
(section 6).

## 5. Order of work (each slice ships alone and changes no behaviour, except where stated)

0. **The check** (section 2), with every current offender listed. **Built.**
1. **`platform_process`**, with its own test (exit codes, signals, the captured tail, timeouts, a missing program, a bad working directory, output files,
   group kill, no leaked descriptors). **Built**, and the six launchers moved onto it. Behaviour that did change: every child now gets only the standard
   streams, a clean signal mask and default SIGPIPE; a program that cannot start is reported by name ("cannot start 'g++'") instead of a silent exit 127; the
   harness keeps the last 512 KiB of output instead of 256 to 512 KiB. Behaviour kept, though worth changing: a harness timeout kills only the direct child,
   so a timed-out build can leave compiler processes behind. Killing the group needs the children in a group of their own, which the shim's own group
   kill must still reach (still open after slice 5). Left: `std::system` in `test_scenarios.cpp`.
2. **`platform_proc` and `platform_host`** (**done** 2026-11-16): one self path, one memory reader, `ProcessId` out of the public headers. The
   daemon's pidfd watch became `Waiter::watchExit` in slice 5, with that loop.
3. **`platform_file`** (**done** 2026-11-16): durable appends, atomic replaces and whole-file reads in one tested place, binary mode throughout. The 21
   text-mode writes that would have put CRLF into goldens on Windows are moved onto it. The shim's `fsync` became `writeFileAtomic(..., durable)` in slice 5.
4. **`platform_net` and `platform_ipc`** (**done** 2026-11-16, POSIX and Windows together): engine client, portal, control client, port probe, the
   daemon's instance lock (`FileLock`), listener and peer check, and the tests' socket fixtures. The daemon still hands each accepted stream's native
   socket to its epoll loop (`SocketStream::release`) until slice 5, which keeps the streams. Behaviour that changed: the portal gives a client 2 s for its whole request
   (before: 2 s per read), and its token comes from `std::random_device` instead of `/dev/urandom`.
5. **`platform_wait`** (**done** 2026-11-16, POSIX and Windows together): the daemon's loop (its listener, clients, shims' exits, posts as wakes, and
   stop requests), the shim (spawned job with three piped streams, each copied by a thread of its own; its exit, a stop request or the wall time in one
   `Waiter`; the teardown through `Child` and `killProcess`; `exit.json` and `shim.json` written durably), the log follower, and `mock_worker` (its
   server and escapee are re-starts of itself, not forks). Behaviour that changed: a job whose program cannot start writes the reason to its
   `stderr.log` (still exit 127, or 126); the daemon takes its stop requests before its executor threads start, so a SIGTERM can no longer land on
   one of them and end the process; `logs --follow` looks every 250 ms; the job's oom score is set from the shim just after the start, not before the
   exec. The harness timeout's group kill (step 1) is still open.

Rough size, POSIX only (library code including tests): slice 1 was about 450 lines; 2 about 300; 3 about 250; 4 about 400; 5 about 600. The call sites
shrink by a similar amount.

## 6. Windows

A Windows build is the `_win32.cpp` siblings plus the following. Estimate (from the review): about 3,000 lines, 3 to 4 weeks of focused work, plus about
250 lines of memory-safety work that needs the owner because it means running the engine.

**Where it stands (2026-11-16, MinGW-w64 GCC 16.2 UCRT, CMake and Ninja; the commands are in [DEV_SETUP.md](DEV_SETUP.md)).** Configure works (the version
script has a `cmake/git-semver.cmd` twin). The platform layer has its four Windows backends and passes its tests natively:

* `platform_process_win32.cpp`: `CreateProcessW` with arguments quoted for `CommandLineToArgvW`, an explicit inherited-handle list (stdin, stdout,
  stderr only: the Windows form of `closefrom`), the null device for stdin. A new session is `CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW`, tries
  `CREATE_BREAKAWAY_FROM_JOB` first, starts suspended and is put in a job object of its own, which `terminate` and `kill` end as a whole. Neither is
  graceful (see below); they exit with 143 and 137 and `wait` reports signal 15 or 9, so callers read the same status on both systems. A crash status
  (0xC0000005 and kin) is reported as the nearest signal. `runCaptured` polls an anonymous pipe and stops reading when the child ends, since a grandchild
  may hold the pipe open.
* `platform_proc_win32.cpp`: `GetModuleFileNameW`; the creation time as the start stamp; a Toolhelp32 snapshot for the tree (an orphan keeps its dead
  parent's id, so a child created before the holder of that id is not counted); the command line read through `NtQueryInformationProcess` (looked up in
  ntdll at run time) and split by `CommandLineToArgvW`; the environment set in both the C runtime's copy and the system's.
* `platform_host_win32.cpp`: `GlobalMemoryStatusEx` (the commit headroom is what is left of the commit limit), `GetSystemTimes`, `std::filesystem::space`;
  no load average and no GPU figures yet.
* `platform_file_win32.cpp`: the C runtime's descriptors in binary, append and no-inherit mode; `_commit` (FlushFileBuffers) for sync, `_chsize_s` to
  truncate, `GetFileAttributesExW` for the modification time.

`platform_net_win32.cpp` and `platform_ipc_win32.cpp` (slice 4) are Winsock 2 and AF_UNIX, and `test_platform_net` passes natively.
`platform_wait_win32.cpp` (slice 5) and the pipes of `platform_process_win32.cpp` pass `test_platform_wait` natively, including a stop request to
another process through its named event. MinGW needs `libstdc++exp` for `std::print`'s console output; CMake links it. `tests/m0_graph_growth.cpp`
(a fork probe that answered on Linux) is built on POSIX only.

**The program runs natively (2026-11-16).** The daemon, the shim, jobs, workflows and the CLI run on Windows. These pass natively: the platform
tests, `test_e2e` (cancel leaves no process, lost supervisors, a daemon crash and adoption, the polled-shim fallback), `test_shim`, `test_idle`,
`test_archive`, `test_fork`, `test_scenarios`, `test_planner`, `test_wizard`, `test_project` (Python, C++ and web projects, cancel, crash and adoption), `test_readme`, `test_clarify` and the rest of the unit suite.
What it took, beyond the platform layer:

* **Line ends.** The program puts its standard streams in binary mode at start (`sub0platform::useBinaryStandardStreams`): in text mode every `\n` became `\r\n`,
  so a captured workflow id read back as `w-000001\r`. A checkout made before `.gitattributes` still had CRLF fixtures and goldens; re-checking those
  files out fixed a dozen golden tests. Test fixtures are written in binary mode.
* **Paths.** A workflow sandbox must be rooted (`has_root_directory`, so `C:\x` and `/x` both qualify), not "start with `/`"; the portal's sandbox check
  compares generic strings and refuses a root name (`C:x`, `\\server`); `mend-apply` refuses any rooted file. The compile-to-nowhere checks use
  `sub0platform::nullDevice()` (`NUL`).
* **The C++ harness on MinGW.** `_USE_MATH_DEFINES` everywhere (`M_PI` is hidden under `-std=c++20` otherwise); the app's test helper uses
  `_popen` with cmd.exe quoting and reads in text mode; the app is `calc_cli.exe`; the integration builds with AddressSanitizer and UBSan only where
  the compiler can link them (probed once: MinGW's GCC has neither, and the report then says "WITHOUT sanitizers"); a link error is quoted from
  "undefined reference" on, since MinGW's `ld.exe` path is longer than the cut.
* **The web harness on node 22 and later.** `node --test` takes the test files by name, not the `tests/` directory (on every system).
* **A `thread_local` simdjson parser** crashed threads at exit under MinGW (its destructor ran after the thread's storage was freed). Parsers now come
  from a small process-wide pool (`json::PooledParser`).
* **Job containment.** A job's job object does not allow breakaway: a process of the job that starts a new session of its own (the escapee in
  `test_e2e`) stays in the job, so cancel reaches it. A daemon's shims leave the caller's job when that job allows it; a crashed daemon's job has no
  kill-on-close, so its shims live on and the next daemon adopts them (`test_project`'s crash scenario passes natively).

The full suite passes natively (55 of 55, 2026-11-17, with node 24 and Python on `PATH`). Without node, the web-platform cases fail on any system
(`test_platforms`, `test_clarify`, `test_scenarios`, `test_project`) and `test_portal_page` is disabled. MSVC and clang-cl are untried.

* **First milestone: the domain layer and its tests under MSVC or clang-cl.** It is free of the operating system since slice 4, so this checks the C++23
  portability of most of the code cheaply.
* **Then the CLI and the mock first-use path** (`test_readme`): every module but the limits.
* **The C++ harness needs a toolchain profile** (compiler, warning and sanitizer flags, object and executable suffixes). Use LLVM `clang++` on Windows, whose
  diagnostics read close enough to GCC's for the mends and the diagnostics table to match. The Python harness is portable; the web harness needs node.

What Windows will not match exactly, and must be designed for:

* **Memory pressure.** The host-safety rules (`docs/STATUS.md` section 7) rely on a contained service whose out-of-memory kill takes the engine and not the
  session. Windows pages rather than kills. At the commit limit, allocation fails in every process, the owner's session included, so the guard must watch
  the commit headroom (page file included), not only free memory.
* **Detached children.** The daemon, the portal and the shim must outlive what started them, so "die with the parent" (KILL_ON_JOB_CLOSE) is wrong for them.
  Starting them outside the parent's job can fail when a terminal, an IDE or CTest runs us inside a job that does not allow breakaway. The launcher must
  detect that and say so.
* **Replacing a file while it is read.** Rename over a file fails while a reader has it open, and `std::ifstream` opens without delete sharing. The file
  module owns reads as well as writes (sharing flags), or retries.
* **Graceful stop.** There is no SIGTERM. Decided in slice 5: a process that watches for stop requests (the daemon, the shim) owns a named event that
  `terminateProcess` sets; anything else, and a job's group, ends at once (`TerminateProcess`, `TerminateJobObject`). So a cancel reaches the shim
  politely and the shim tears the job down as on POSIX, but a job's own SIGTERM handler has no Windows counterpart.
* **Peer credentials** on the local socket: the folder's ACL stands in for `SO_PEERCRED`.
* **The engine** (llama-server) already runs natively on Windows; nothing in the squad needs WSL for it.

### The primitives' cost (2026-11-17)

`benchmarks/bench_platform.cpp` pairs each primitive with a hand-written twin: the fewest direct system calls that do the same job on that system.
The ratio is the library call over the twin (1.00x is parity), from the baselines in `perf/baselines/` (`croglegion` is Windows 11 with MinGW-w64
GCC 16.2; `croglegion-wsl` is WSL2 Ubuntu 24.04 with GCC 15 on the same machine). Neither has hardware counters, so the ratios are wall time.

| Pair | Windows (twin) | Ratio | WSL (twin) | Ratio |
|---|---:|---:|---:|---:|
| waiter wake and wait (`Waiter`) | 260 ns | 1.15x | 410 ns | 1.04x |
| loopback 64 B write and read (`SocketStream`) | 7.5 µs | 1.00x | 70 µs | 0.93x |
| atomic replace 4 KiB (`writeFileAtomic`) | 300 µs | 1.08x | 78 µs | 0.98x |
| spawn and wait (`sub0platform::spawn`) | 14.9 ms | 1.01x | 0.61 ms | 1.01x |

What the pass changed, and what is left:

* **`writeFileAtomic` on Windows was 1.19x.** libstdc++'s `std::filesystem::rename` looks up the status of both paths before `MoveFileExW`; the replace is
  now one call per system (`detail::replaceFile`), and the temporary name is built without a narrow-string round trip. `writeFile` creates the parent
  folder only when the open fails (it was 1.22x on Linux from a stat of every ancestor).
* **The Windows `Waiter` was 1.32x.** It waited on the socket event even with no socket watched; now that handle is in the wait only while a socket is.
  The 1.15x left is the returned `std::vector`'s allocation (25 ns of the 40 on UCRT's heap, measured) plus moving the result; a borrowed span would
  remove it at the cost of a lifetime rule on every caller, for a wake that happens at human rates.
* **The loopback twin was wrong.** It gave up on `EAGAIN` (the library's sockets are non-blocking), so it skipped the wait the library is measured
  doing and showed the library 270x slower on Linux. It now waits in `poll`, as any reader must. TCP streams now set `TCP_NODELAY`: the library's TCP
  traffic is requests and replies, where a held small write waits for a delayed ACK. WSL's 70 µs round trip is the VM's wake-up from `poll`, on both sides.
* **Not platform primitives, for the record:** `Journal::append` is 1.9x a bare `write()` on Linux (1.07x on Windows): the journal formats the record and
  keeps it in memory, about 180 ns over a 210 ns write. `encodeFrame` is 2.4x its stack-only twin on Windows: one heap allocation for the returned string.

## 7. Recommendation

Slices 0 to 5 are done: they removed duplicated code and kept operating-system types out of the interfaces, and the platform layer has Windows
backends that pass its tests. The program runs natively on Windows (section 6). The platform primitives are benchmarked against hand-written twins on
both systems (`benchmarks/bench_platform.cpp`, part of `Sub0Squad_Bench`; [DEV_SETUP.md](DEV_SETUP.md), "Tests and benchmarks"; section 6 has the
figures). Next: MSVC or clang-cl.
