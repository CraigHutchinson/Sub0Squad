# Platform: one library for everything the operating system does

Status: 2026-11-16. Owner direction: good platform abstractions, first of all to keep a good shape to the code; a native Windows build is the second
reason, not the first. **Built:** the `platform` layer (layer 0, `cmake/Layers.cmake`, with its `link_platform` proof), the system-header check (slice 0)
and `platform_process` with its test (slice 1). The launchers of `harness_common`, `host_guard`, `wizard`, `replay`, `first_use` and `supervisor` run on it;
the shim's own fork waits for slice 5. Slice 2: `platform_proc` and `platform_host`, with tests; `process_tree` is gone, the four `/proc/self/exe` reads
and the two `/proc/meminfo` parsers are one each, and `ProcessId` replaced `pid_t` in the public headers. Slice 3: `platform_file`, with a test; the
journal, ledger, progress, frames and the event channel use it, and the eleven private copies of `slurp`/`readFile`/`writeFile`/`writeFileAtomic` in
`src/` are gone (two shared helpers, `harnessutil` and `agent`, forward to it). The tests' one `std::system` now runs through the launcher.
**Not yet:** 21 direct `std::ofstream` writes in text mode (move each onto `writeFile` when its file is next touched), the daemon's pidfd watch
(`watchExit`, moved to slice 5 with the loop it feeds), and slices 4 and 5. Evidence: a survey of `src/` and `tests/` on that
date, checked by an independent review of this page against the code.

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
`layering` test checks it (`cmake/CheckLayers.cmake`). The files that still break the rule are listed in `KNOWN_SYSTEM_USERS`, each with the slice that moves
it. The list may only shrink: a file that stops using the system must be taken off the list, and a file that starts fails the test.

## 3. Shape

* **Its own layer.** `platform` is layer 0, below the kernel. Every layer may use it and it uses nothing else of ours, which its `link_platform` proof checks
  at link time (`docs/COMPONENTS.md`). Interfaces are in `include/sub0squad/platform_*.hpp` and the POSIX implementation in `src/platform_*.cpp` (the layer
  files are flat). A Windows implementation becomes a `src/platform_*_win32.cpp` sibling, and CMake picks one of the two for each stem.
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
| `platform_process` (**built**) | `ProcessSpec{argv, cwd, outputFile, appendOutput, outputPerms, nullInput, newSession}`; `spawn -> Child`; `Child::wait(timeout)`, `terminate()`, `kill()` (the whole group for a new session); `runCaptured(spec, {timeout, maxBytes, includeStderr}) -> Captured{status, output (the end of it), timedOut, truncated}` | the launchers above; later `std::system` in the tests | `posix_spawnp` with `closefrom`, an empty signal mask and default SIGPIPE; still to come: the shim's pre-exec steps (oom score, parent-death signal) | CreateProcessW; a Job object per group to kill the tree |
| `platform_proc` (**built**, except `watchExit`) | `selfExecutable()`, `currentProcess()`, `processStart(ProcessId)` and `isAlive(id, start)` (identity against PID reuse), `descendants(ProcessId)`, `commandLine(ProcessId)`, `terminateProcess(ProcessId)`, `setEnvironment`/`unsetEnvironment`; pure `parseProcStat`, `parseCmdline`; later `watchExit(ProcessId)` | `process_tree.cpp`, the four readlinks, `build_id.cpp`, `first_use.cpp`, `pidfd_open` in `daemon_jobs.cpp`, `getpid` in `harness_cpp.cpp`; the tests' lookups by `s0q-` name | `/proc`, pidfd | Toolhelp32 snapshot, process creation time, `QueryFullProcessImageNameW`, a process handle to wait on |
| `platform_host` (**built**) | `memory() -> {totalMiB, availableMiB, swap, commitHeadroomMiB}`, `cpuTimes()`, `loadAverage() -> optional`, `freeDiskMiB(path) -> optional`, `gpu() -> optional`; pure `parseMeminfo`, `parseCpuTimes` | `host_guard.cpp`, `portal_sampler.cpp` | `/proc/meminfo`, `/proc/stat`, `/proc/loadavg`, statvfs, `/sys/class/drm` | `GlobalMemoryStatusEx` (including the commit limit), `GetSystemTimes`, no load average (nullopt), `GetDiskFreeSpaceExW` |
| `platform_file` (**built**, except the lock and the temporary directory) | `readFile`/`readFileText`, `writeFile`, `writeFileAtomic`, `modifiedSeconds`, `AppendFile{open, append, sync, truncate}`, `appendToFile`, `writeAll`/`readSome` on a descriptor; later `FileLock` (with the daemon socket, slice 4) and a temporary directory; the owner-only directory is `std::filesystem::permissions` | `journal.cpp`, `ledger.cpp`, `progress.cpp`, `frame.cpp`, `event_channel.cpp`, `paths.cpp`, `fsync` in `shim.cpp`, `mkdtemp` in `check.hpp` | O_APPEND, fsync, rename, flock, chmod, mkdtemp | FlushFileBuffers, `MoveFileExW(REPLACE_EXISTING, WRITE_THROUGH)` with a retry while a reader holds the file, LockFileEx, an ACL |
| `platform_net` | `TcpStream::connect(host, port, timeout)`, `TcpListener` (loopback), read and write with a deadline | `engine.cpp`, `portal_impl.hpp`, `first_use.cpp`, `tests/fake_engine.hpp` | BSD sockets, poll | Winsock (almost the same API), WSAPoll |
| `platform_ipc` | `LocalListener::bind(path)`, `LocalStream::connect(path)`, `peerIsSameUser()` | `unix_socket.cpp`, the socket code in `client.cpp` | AF_UNIX with the `/proc/self/fd` bind, `flock`, `SO_PEERCRED` | AF_UNIX (Windows 10 1803 and later) or a named pipe; the peer check by the folder's ACL |
| `platform_wait` | `Waiter`: wait on streams, child exits, a timer, a wake-up and a shutdown request together; `FileWatch` for "this log grew" | `daemon_impl.hpp` (epoll, eventfd, signalfd, timerfd), `shim.cpp` (epoll, the subreaper), inotify in `cli_jobs.cpp` | epoll, eventfd, signalfd, timerfd, inotify, `PR_SET_CHILD_SUBREAPER` | an I/O completion port or `WaitForMultipleObjects`, waitable timers, `ReadDirectoryChangesW`, `SetConsoleCtrlHandler`; Job objects stand in for the subreaper |
| `platform_limits` | in `ProcessSpec`: a process count, a memory cap, an out-of-memory preference | `ulimit` in scripts and tests, `oom_score_adj` in `shim.cpp` | setrlimit, `oom_score_adj`, a cgroup when the service runs in one | Job object limits (active processes, job memory) |

`cpp_templates.cpp` has `popen` only inside the text of a generated project's helper, not in our code: it belongs to the C++ harness's toolchain profile
(section 6).

## 5. Order of work (each slice ships alone and changes no behaviour, except where stated)

0. **The check** (section 2), with every current offender listed. **Built.**
1. **`platform_process`**, with its own test (exit codes, signals, the captured tail, timeouts, a missing program, a bad working directory, output files,
   group kill, no leaked descriptors). **Built**, and the six launchers moved onto it. Behaviour that did change: every child now gets only the standard
   streams, a clean signal mask and default SIGPIPE; a program that cannot start is reported by name ("cannot start 'g++'") instead of a silent exit 127; the
   harness keeps the last 512 KiB of output instead of 256 to 512 KiB. Behaviour kept, though worth changing: a harness timeout kills only the direct child,
   so a timed-out build can leave compiler processes behind. Killing the group needs the children in a group of their own, which the shim's own group
   kill must still reach. Decide that together with the shim in slice 5. Left: the shim's fork (slice 5); `std::system` in `test_scenarios.cpp`.
2. **`platform_proc` and `platform_host`** (**done** 2026-11-16): one self path, one memory reader, `ProcessId` out of the public headers. The
   daemon's pidfd watch (`watchExit`) moved to slice 5: it is a descriptor in the daemon's epoll set and belongs with that loop.
3. **`platform_file`** (**done** 2026-11-16): durable appends, atomic replaces and whole-file reads in one tested place, binary mode throughout. Left:
   21 direct `std::ofstream` writes in text mode, which on Windows would write CRLF into goldens; `fsync` in `shim.cpp` (slice 5).
4. **`platform_net` and `platform_ipc`**: engine client, portal, daemon socket, fake engine.
5. **`platform_wait`**: the daemon and shim loops, the subreaper, the log follower. This needs the most design, so do it once 1 to 4 have settled.

Rough size, POSIX only (library code including tests): slice 1 was about 450 lines; 2 about 300; 3 about 250; 4 about 400; 5 about 600. The call sites
shrink by a similar amount.

## 6. Windows, afterwards

With slices 0 to 5 done, a Windows build is the `_win32.cpp` siblings plus the following. Estimate (from the review): about 3,000 lines, 3 to 4 weeks of
focused work, plus about 250 lines of memory-safety work that needs the owner because it means running the engine.

* **First milestone: the domain layer and its tests under MSVC or clang-cl.** It is NOT free of the operating system today: `engine.cpp` uses sockets,
  `ledger.cpp` raw descriptors, `test_stream` sockets. It becomes free of it after slices 3 and 4, and then checks the C++23 portability of most of the
  code cheaply.
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
* **Graceful stop.** There is no SIGTERM: a polite stop is a message (over the child's pipe or a named event), then `TerminateJobObject`.
* **Peer credentials** on the local socket: the folder's ACL stands in for `SO_PEERCRED`.
* **The engine** (llama-server) already runs natively on Windows; nothing in the squad needs WSL for it.

## 7. Recommendation

Slices 0 to 3 are done: they removed duplicated code and kept operating-system types out of the interfaces, at low risk. Move the remaining text-mode
writes onto `writeFile` as their files are touched. Schedule slices 4 and 5 when the experiments leave room, since they touch the daemon's core loop. Decide on the Windows
implementation after that; its first step is the domain layer under MSVC. Until then the WSL path in [DEV_SETUP.md](DEV_SETUP.md) is the way to build on a
Windows host.
