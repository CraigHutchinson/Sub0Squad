cmake_minimum_required(VERSION 3.21)
# WORKING TITLE: the final sub0* name is pending (docs/DECISIONS.md). Keep the name in this file, cmake/, src/ and README.md only.
project(Sub0Squad VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

find_package(Threads REQUIRED)

set(SUB0SQUAD_STRICT -Wall -Wextra -Werror)

# Everything except main() lives in one static library so tests can link it.
add_library(sub0squad_core STATIC
  src/paths.cpp
  src/job_spec.cpp
  src/journal.cpp
  src/job_store.cpp
  src/process_tree.cpp
  src/shim.cpp
  src/supervisor.cpp
  src/mock_worker.cpp
  src/frame.cpp
  src/unix_socket.cpp
  src/progress.cpp
  src/status.cpp
  src/daemon.cpp
  src/daemon_jobs.cpp
  src/daemon_workflow.cpp
  src/daemon_governance.cpp
  src/daemon_jobs.cpp
  src/daemon_workflow.cpp
  src/daemon_governance.cpp
  src/plan.cpp
  src/host_guard.cpp
  src/architect.cpp
  src/optimizer.cpp
  src/scenario.cpp
  src/triage.cpp
  src/llm_agent.cpp
  src/agent_card.cpp
  src/canvas_agent.cpp
  src/fork.cpp
  src/blocks.cpp
  src/throughput.cpp
  src/turn_control.cpp
  src/web_templates.cpp
  src/harness_common.cpp
  src/harness_web.cpp
  src/calculator_spec.cpp
  src/cpp_templates.cpp
  src/harness_cpp.cpp
  src/py_templates.cpp
  src/harness_py.cpp
  src/harness_registry.cpp
  src/engine.cpp
  src/ledger.cpp
  src/workflow_status.cpp
  src/briefing.cpp
  src/qa.cpp
  src/division.cpp
  src/replay.cpp
  src/edits.cpp
  src/outline.cpp
  src/canvas.cpp
  src/recall.cpp
  src/trace.cpp
  src/overseer.cpp
  src/plan_lint.cpp
  src/portal.cpp
  src/portal_page.cpp
  src/event_channel.cpp
  src/sandbox.cpp
  src/workflow.cpp
  src/policy.cpp
  src/predicate.cpp
  src/mends.cpp
  src/attempt_failure.cpp
  src/contract.cpp
  src/scenario_compare.cpp
  src/build_id.cpp
  src/cmd_archive.cpp
  src/process.cpp
  src/cmd_process.cpp
  src/client.cpp)
target_include_directories(sub0squad_core PUBLIC include PRIVATE src)
target_compile_options(sub0squad_core PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(sub0squad_core PUBLIC Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Sub0Pub::Sub0Pub Threads::Threads PRIVATE simdjson::simdjson)

add_executable(sub0squad src/sub0squad.cpp src/first_use.cpp)
target_include_directories(sub0squad PRIVATE src)
target_link_libraries(sub0squad PRIVATE version::version)
target_compile_options(sub0squad PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(sub0squad PRIVATE sub0squad_core simdjson::simdjson)

enable_testing()
add_test(NAME dependency_selftest COMMAND sub0squad --selftest)
add_test(NAME layering COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckLayers.cmake)
add_test(NAME test_reporting COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckTests.cmake)

function(sub0squad_add_test name)
  # tests that exercise the multi-call binary receive its path as argv[1]
  add_executable(${name} tests/${name}.cpp)
  target_compile_options(${name} PRIVATE ${SUB0SQUAD_STRICT} -g)   # line numbers when a test crashes
  target_include_directories(${name} PRIVATE tests)
  target_link_libraries(${name} PRIVATE sub0squad_core simdjson::simdjson ${ARGN})
  add_test(NAME ${name} COMMAND ${name} $<TARGET_FILE:sub0squad>)
  add_dependencies(${name} sub0squad)
endfunction()

sub0squad_add_test(test_job_spec)
sub0squad_add_test(test_journal)
sub0squad_add_test(test_job_store)
sub0squad_add_test(test_predicate)
sub0squad_add_test(test_mends)
target_compile_definitions(test_mends PRIVATE SUB0SQUAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
sub0squad_add_test(test_process)
target_compile_definitions(test_process PRIVATE SUB0SQUAD_REGISTRY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/registry")
# The domain layer's tests, one program per unit; the three engine programs share tests/workflow_sim.hpp and the golden action logs in tests/data/actions.
sub0squad_add_test(test_plan)
sub0squad_add_test(test_policy)
sub0squad_add_test(test_edits)
sub0squad_add_test(test_diagnostics)
foreach(t test_workflow test_workflow_stages test_workflow_mends)
  sub0squad_add_test(${t})
  target_compile_definitions(${t} PRIVATE SUB0SQUAD_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endforeach()
sub0squad_add_test(test_host_guard)
sub0squad_add_test(test_platforms)
target_compile_definitions(test_platforms PRIVATE SUB0SQUAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(test_platforms PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_stream)
set_tests_properties(test_stream PROPERTIES TIMEOUT 60)
sub0squad_add_test(test_agent)
sub0squad_add_test(test_canvas_agent)
set_tests_properties(test_canvas_agent PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_fork)
set_tests_properties(test_fork PROPERTIES TIMEOUT 300)
add_executable(test_blocks tests/test_blocks.cpp)
target_compile_options(test_blocks PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_blocks PRIVATE tests)
target_link_libraries(test_blocks PRIVATE sub0squad_core simdjson::simdjson)
add_test(NAME test_blocks COMMAND test_blocks $<TARGET_FILE:sub0squad> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/replies)
add_dependencies(test_blocks sub0squad)
set_tests_properties(test_blocks PROPERTIES TIMEOUT 300)
set_tests_properties(test_agent PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_scenarios)
set_tests_properties(test_scenarios PROPERTIES TIMEOUT 600)
sub0squad_add_test(test_project)
add_executable(test_readme tests/test_readme.cpp)
target_compile_options(test_readme PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_readme PRIVATE tests)
add_test(NAME test_readme COMMAND test_readme $<TARGET_FILE:sub0squad> ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
add_dependencies(test_readme sub0squad)
set_tests_properties(test_readme PROPERTIES TIMEOUT 900)
set_tests_properties(test_project PROPERTIES TIMEOUT 600)
sub0squad_add_test(test_archive)
sub0squad_add_test(test_executors)
sub0squad_add_test(test_heal)
set_tests_properties(test_heal PROPERTIES TIMEOUT 1200)
sub0squad_add_test(test_nearpass)
set_tests_properties(test_nearpass PROPERTIES TIMEOUT 1800)
set_tests_properties(test_executors PROPERTIES TIMEOUT 900)
sub0squad_add_test(test_e2e)
set_tests_properties(test_e2e PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_idle)
set_tests_properties(test_idle PROPERTIES TIMEOUT 120)
sub0squad_add_test(test_shim)
add_executable(test_adapter tests/test_adapter.cpp)
target_compile_options(test_adapter PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_adapter PRIVATE tests)
target_link_libraries(test_adapter PRIVATE sub0squad_core simdjson::simdjson)
add_test(NAME test_adapter COMMAND test_adapter ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/optimization-run-1-stalled.json)
set_tests_properties(test_shim PROPERTIES TIMEOUT 120)

add_executable(m0_graph_growth tests/m0_graph_growth.cpp)
target_compile_options(m0_graph_growth PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(m0_graph_growth PRIVATE Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Threads::Threads)
add_test(NAME m0_graph_growth COMMAND m0_graph_growth)

# sub0perf benchmark (Sub0Math tooling): Sub0Squad_Bench, plus _json, _runs, _check and _baseline targets.
sub0math_add_benchmark(Sub0Squad_Bench benchmarks/bench_squad.cpp)
target_include_directories(Sub0Squad_Bench PRIVATE include src)
target_link_libraries(Sub0Squad_Bench PRIVATE sub0squad_core simdjson::simdjson)
