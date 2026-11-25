# Sub0 family libraries via CPM, each pinned to an exact revision for reproducible builds (CI, fresh machines).
# For local development CPM honours CPM_<NAME>_SOURCE: when set, the package is taken from that directory and the pin is
# ignored (the Sub0 dev machine sets these for every checkout under the GitHub folder; see docs/DEV_SETUP.md).
# CPM prints "Using local source" when an override is in effect.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/get_cpm.cmake)

# Sub0Pipeline: C++23 DAG job scheduler (deferred completion, cancellation, remote sub-graphs). Pinned to the head of its
# default branch as of 2026-11-09 (committed 2026-10-21). Remote sub-graphs stay OFF until milestone M1 needs them.
CPMAddPackage(NAME Sub0Pipeline
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Pipeline.git
  GIT_TAG 3d11ffebe7ce4dc99eb7d5fc96412dd1c7d321fd
  OPTIONS "SUB0PIPELINE_BUILD_TESTING OFF" "SUB0PIPELINE_BUILD_EXAMPLES OFF" "SUB0PIPELINE_BUILD_BENCHMARKS OFF"
          "SUB0PIPELINE_BUILD_REMOTE OFF")

# Sub0Pub: typed publish/subscribe (v2.0.0-alpha, CI-tested, not field-tested). Pinned to the head of its default branch
# as of 2026-11-09 (committed 2026-10-09).
CPMAddPackage(NAME Sub0Pub
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Pub.git
  GIT_TAG 504d772ecd8da6f22a22a26dd78ae3cb61c97868
  OPTIONS "SUB0PUB_BUILD_TESTING OFF" "SUB0PUB_BUILD_EXAMPLES OFF")

# Sub0Perf: the family's performance tooling (sub0perf: calibrated interleaved benchmark rounds, per-host baselines, the measuring
# lock, pre-push gate; docs/PERFORMANCE.md there). It moved out of Sub0Math (ADR 0004 in CraigHutchinson/.github).
# Pinned to the v0.4.0 tag commit (2026-11-25).
CPMAddPackage(NAME Sub0Perf
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Perf.git
  GIT_TAG af5953411488674d8f9b85e9ce8687510203c0ac
  DOWNLOAD_ONLY YES)
# Off by default in this project: no git hooks installed and no benchmark run after every build; run the targets deliberately
# (cmake --build <dir> --target Sub0Squad_Bench_check) and enable the hooks only when ready (docs/DEV_SETUP.md).
set(SUB0PERF_ON_BUILD "OFF" CACHE STRING "")
set(SUB0PERF_GIT_HOOKS OFF CACHE BOOL "")
include("${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake")

# Sub0Platform: the operating-system layer of the Sub0 C++ projects (cpp/README.md there). It is the layer below Sub0Squad's kernel
# (docs/COMPONENTS.md): only it includes operating-system headers. Pinned to the head of its default branch as of 2026-11-19; its own tests are off here.
CPMAddPackage(NAME Sub0Platform
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Platform.git
  GIT_TAG 5feccdf226046cc790214bc30793832a1888ce4b
  SOURCE_SUBDIR cpp
  OPTIONS "SUB0PLATFORM_BUILD_TESTS OFF" "SUB0PLATFORM_INSTALL OFF")

# simdjson 5.1.1 (Apache-2.0, released 2026-11-01): the one JSON library, as in Sub0Llm. Reads use the on-demand forward API;
# writes use simdjson::builder::string_builder (present since 5.x; Sub0Llm's notes still say "parse only", which is stale for 5.x).
# Keep simdjson types out of public headers (docs/DECISIONS.md).
CPMAddPackage(NAME simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG 40a686ab89dc5ee58ec10d0514dd5c99abbc472b
  OPTIONS "SIMDJSON_DEVELOPER_MODE OFF")

# Sub0Canvas: the canvas tooling (folded views, edits, the revisioned canvas, stable block ids and layers; its docs/INTEGRATION.md), split out of this
# repository on 2026-11-17 so it can serve other consumers. Sub0Squad is its first. Pinned to the v0.3.0 release (turn control and the reply applier live there, adopted 2026-11-23); changes to that code are made there first, then
# the pin is bumped here (test_blocks' recorded replies exercise it, so they run against every new pin). It reuses the simdjson target above.
CPMAddPackage(NAME Sub0Canvas
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Canvas.git
  GIT_TAG v0.3.0
  OPTIONS "SUB0CANVAS_BUILD_TESTING OFF")

# Version.cmake (BareCpper org, same author): git describe -> version fields and a generated header, refreshed every configure. Pinned to the v0.4 tag.
# Provides version::version and Version.hpp: sub0squad::version::version_sha / version_full (…-dirty when the tree has changes).
set(VERSION_OUT_DIR "${CMAKE_BINARY_DIR}/generated/sub0squad")
set(VERSION_FALLBACK "0.0.0")
set(VERSION_H_FILENAME "Version.hpp")
set(VERSION_NAMESPACE "sub0squad::version")
# Version.cmake reads `describe --tags` and writes an empty header when the nearest tag is not a semantic version (the milestone tag `parked-v1`
# broke every build: docs/INTERVENTIONS.md row 37). It is given a wrapper that describes only v<major>.<minor> tags, at configure time and in its
# build-time regeneration; with no such tag it falls back to 0.0.0-<commit count>-g<hash>.
# The package is fetched with plain git and only then added with the wrapper: FetchContent's own git calls must not go through it, because cmd.exe
# eats the `^` of `git rev-parse HEAD^0` and a fresh clone (an empty CPM cache, as in CI) fails on Windows.
CPMAddPackage(NAME Version.cmake
  GIT_REPOSITORY https://github.com/BareCpper/Version.cmake.git
  GIT_TAG c9e67af0aec3588912b760b45dca3ff217a62f59
  DOWNLOAD_ONLY YES)
if(CMAKE_HOST_WIN32)
  set(GIT_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/git-semver.cmd")   # CMake cannot run a shell script on Windows
else()
  set(GIT_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/git-semver.sh")
endif()
add_subdirectory(${Version.cmake_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_deps/version.cmake-build)
unset(GIT_EXECUTABLE)
if(NOT VERSION_SET)
  message(FATAL_ERROR "Version.cmake could not derive a version (see the 'Git Describe' lines above): the generated Version.hpp would not compile")
endif()
