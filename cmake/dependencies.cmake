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

# Sub0Math: hosts the family's performance tooling (sub0perf: calibrated interleaved benchmark rounds, per-host baselines, the
# measuring lock, pre-push gate; docs/PERFORMANCE.md there). Used here only for its tooling (cmake/Sub0MathTooling.cmake), not its maths.
# Pinned to the head of its default branch as of 2026-11-09.
CPMAddPackage(NAME Sub0Math
  GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Math.git
  GIT_TAG 1d20313cbd2ff2753620246b81934d0489a0636f
  DOWNLOAD_ONLY YES)
include("${Sub0Math_SOURCE_DIR}/cmake/Sub0MathTooling.cmake")
