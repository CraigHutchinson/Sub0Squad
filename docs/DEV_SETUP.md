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

`SUB0_GITHUB_DIR` defaults to `/run/media/craig-hutchinson/54D4C5011B8AF586/GitHub`. A configure with the overrides active logs
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
