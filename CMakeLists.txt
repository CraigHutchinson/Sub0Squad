cmake_minimum_required(VERSION 3.24)
# WORKING TITLE: the final sub0* name is pending (docs/DECISIONS.md). Keep the name in this file, cmake/, src/ and README.md only.
project(Sub0Squad VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

find_package(Threads REQUIRED)

set(SUB0SQUAD_STRICT -Wall -Wextra -Werror)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # clang is the strict reference compiler. GCC 15 adds two flow-based diagnostics that are not errors here, so GCC can still build (WSL has only GCC):
  # -Warray-bounds fires inside simdjson's json_escape.h after inlining (a false positive in third-party code that SYSTEM includes do not suppress), and
  # -Wformat-truncation fires on snprintf into small line-number buffers whose values cannot reach the width it assumes. Both stay visible as warnings.
  list(APPEND SUB0SQUAD_STRICT -Wno-error=array-bounds -Wno-error=format-truncation)
endif()

# One static library per component layer (docs/COMPONENTS.md), made from the same lists the include check reads (cmake/Layers.cmake) and linked in
# layer order: a layer links only the layers below it, so an upward CALL is an unresolved symbol exactly as an upward include fails the layering test.
include(cmake/Layers.cmake)
if(WIN32)
  set(SUB0SQUAD_OS win32)
else()
  set(SUB0SQUAD_OS posix)
endif()
# Sub0Platform (cmake/dependencies.cmake) is the layer below the lowest Squad layer: every Squad layer links it through the chain below.
set(SUB0SQUAD_LOWER Sub0Platform::sub0platform)
set(layerIndex 0)
foreach(layerName ${SUB0SQUAD_LAYER_NAMES})
  set(layerSources "")
  foreach(stem ${LAYER${layerIndex}})
    # sub0squad.cpp is main(): the composition root is the executable, not part of a library.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${stem}.cpp AND NOT stem STREQUAL "sub0squad")
      list(APPEND layerSources src/${stem}.cpp)
    endif()
    # A platform stem has a backend per operating system (docs/PLATFORM.md): src/<stem>_posix.cpp or src/<stem>_win32.cpp.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${stem}_${SUB0SQUAD_OS}.cpp)
      list(APPEND layerSources src/${stem}_${SUB0SQUAD_OS}.cpp)
    endif()
  endforeach()
  set(lib sub0squad_${layerName})
  add_library(${lib} STATIC ${layerSources})
  target_include_directories(${lib} PUBLIC include PRIVATE src)
  target_compile_options(${lib} PRIVATE ${SUB0SQUAD_STRICT})
  target_link_libraries(${lib} PUBLIC ${SUB0SQUAD_LOWER} Threads::Threads PRIVATE simdjson::simdjson)
  # The link-direction proof: this layer with EVERY object forced in must link against only the layers below it. The `link_<layer>` tests run them.
  add_executable(link_${layerName} tests/link_main.cpp)
  target_link_libraries(link_${layerName} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lib}>" simdjson::simdjson)
  list(APPEND SUB0SQUAD_LINK_PROOFS link_${layerName})
  set(SUB0SQUAD_LOWER ${lib})
  math(EXPR layerIndex "${layerIndex} + 1")
endforeach()
# The canvas tooling is the Sub0Canvas library (cmake/dependencies.cmake); the domain layer's forwarding headers (outline, edits, canvas, doc_layers) and its turn_control name it.
target_link_libraries(sub0squad_domain PUBLIC sub0canvas::sub0canvas)
# The daemon's job graph and typed events are the Sub0 libraries; only the orchestration layer uses them.
target_link_libraries(sub0squad_orchestration PUBLIC Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Sub0Pub::Sub0Pub)
# The portal's page is two files of its own (web/portal.html, web/portal.js), compiled in as strings; editing either rebuilds the generated source.
set(SUB0SQUAD_PORTAL_WEB ${CMAKE_CURRENT_BINARY_DIR}/portal_web.cpp)
add_custom_command(OUTPUT ${SUB0SQUAD_PORTAL_WEB}
  COMMAND ${CMAKE_COMMAND} -DOUT=${SUB0SQUAD_PORTAL_WEB} -DNAMESPACE=sub0squad::portalweb
          "-DFILES=kPageHtml=${CMAKE_CURRENT_SOURCE_DIR}/web/portal.html|kPageScript=${CMAKE_CURRENT_SOURCE_DIR}/web/portal.js"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedText.cmake
  DEPENDS web/portal.html web/portal.js cmake/EmbedText.cmake
  COMMENT "Embedding the portal page"
  VERBATIM)
target_sources(sub0squad_presentation PRIVATE ${SUB0SQUAD_PORTAL_WEB})

add_executable(sub0squad src/sub0squad.cpp)
target_include_directories(sub0squad PRIVATE src)
target_link_libraries(sub0squad PRIVATE version::version)
target_compile_options(sub0squad PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(sub0squad PRIVATE sub0squad_presentation simdjson::simdjson)

enable_testing()
add_test(NAME dependency_selftest COMMAND sub0squad --selftest)
add_test(NAME layering COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckLayers.cmake)
foreach(proof ${SUB0SQUAD_LINK_PROOFS})
  add_test(NAME ${proof} COMMAND ${proof})
endforeach()
add_test(NAME test_reporting COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckTests.cmake)
# The portal page's script, rendered against a recorded state with no browser. It needs node; without it the test is registered as DISABLED so the
# report says it did not run (it is never silently absent). test_portal checks the embedded page itself and always runs.
find_program(SUB0SQUAD_NODE NAMES node nodejs)
add_test(NAME test_portal_page COMMAND ${SUB0SQUAD_NODE} ${CMAKE_SOURCE_DIR}/tests/portal_page_test.js ${CMAKE_SOURCE_DIR}/web/portal.js ${CMAKE_SOURCE_DIR}/tests/data/portal/state.json)
if(NOT SUB0SQUAD_NODE)
  set_tests_properties(test_portal_page PROPERTIES DISABLED TRUE)
  message(STATUS "node was not found: test_portal_page is disabled (the portal page script is not rendered by any test on this machine)")
endif()

function(sub0squad_add_test name layer)
  # A test links the HIGHEST layer it needs (and so every layer below it), never the whole program: a test of the kernel cannot reach the daemon.
  # Tests that exercise the multi-call binary receive its path as argv[1].
  add_executable(${name} tests/${name}.cpp)
  target_compile_options(${name} PRIVATE ${SUB0SQUAD_STRICT} -g)   # line numbers when a test crashes
  target_include_directories(${name} PRIVATE tests)
  target_link_libraries(${name} PRIVATE sub0squad_${layer} simdjson::simdjson ${ARGN})
  add_test(NAME ${name} COMMAND ${name} $<TARGET_FILE:sub0squad>)
  add_dependencies(${name} sub0squad)
endfunction()

sub0squad_add_test(test_portal presentation)
sub0squad_add_test(test_wizard presentation)
target_include_directories(test_wizard PRIVATE src)
set_tests_properties(test_wizard PROPERTIES TIMEOUT 600)
target_include_directories(test_portal PRIVATE src)
sub0squad_add_test(test_job_spec kernel)
sub0squad_add_test(test_journal kernel)
sub0squad_add_test(test_job_store kernel)
sub0squad_add_test(test_predicate domain)
sub0squad_add_test(test_mends agents)
target_compile_definitions(test_mends PRIVATE SUB0SQUAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
sub0squad_add_test(test_process domain)
sub0squad_add_test(test_clarify domain)
sub0squad_add_test(test_stance domain)
set_tests_properties(test_clarify PROPERTIES TIMEOUT 600)
target_compile_definitions(test_process PRIVATE SUB0SQUAD_REGISTRY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/registry")
# The domain layer's tests, one program per unit; the three engine programs share tests/workflow_sim.hpp and the golden action logs in tests/data/actions.
sub0squad_add_test(test_plan harnesses)
sub0squad_add_test(test_policy harnesses)
sub0squad_add_test(test_diagnostics harnesses)
foreach(t test_workflow test_workflow_stages test_workflow_mends)
  sub0squad_add_test(${t} harnesses)
  target_compile_definitions(${t} PRIVATE SUB0SQUAD_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endforeach()
sub0squad_add_test(test_host_guard kernel)
sub0squad_add_test(test_platforms harnesses)
target_compile_definitions(test_platforms PRIVATE SUB0SQUAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(test_platforms PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_stream domain)
set_tests_properties(test_stream PROPERTIES TIMEOUT 60)
sub0squad_add_test(test_agent agents)
sub0squad_add_test(test_planner agents)
target_compile_definitions(test_planner PRIVATE SUB0SQUAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(test_planner PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_canvas_agent agents)
set_tests_properties(test_canvas_agent PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_fork agents)
set_tests_properties(test_fork PROPERTIES TIMEOUT 300)
add_executable(test_blocks tests/test_blocks.cpp)
target_compile_options(test_blocks PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_blocks PRIVATE tests)
target_link_libraries(test_blocks PRIVATE sub0squad_agents simdjson::simdjson)
add_test(NAME test_blocks COMMAND test_blocks $<TARGET_FILE:sub0squad> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/replies)
add_dependencies(test_blocks sub0squad)
set_tests_properties(test_blocks PROPERTIES TIMEOUT 300)
set_tests_properties(test_agent PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_scenarios agents)
set_tests_properties(test_scenarios PROPERTIES TIMEOUT 600)
sub0squad_add_test(test_project kernel)
add_executable(test_readme tests/test_readme.cpp)
target_compile_options(test_readme PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_readme PRIVATE tests)
target_link_libraries(test_readme PRIVATE Sub0Platform::sub0platform)   # its runner
add_test(NAME test_readme COMMAND test_readme $<TARGET_FILE:sub0squad> ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
add_dependencies(test_readme sub0squad)
set_tests_properties(test_readme PROPERTIES TIMEOUT 900)
set_tests_properties(test_project PROPERTIES TIMEOUT 600)
sub0squad_add_test(test_archive kernel)
sub0squad_add_test(test_executors agents)
sub0squad_add_test(test_heal agents)
set_tests_properties(test_heal PROPERTIES TIMEOUT 1200)
sub0squad_add_test(test_nearpass agents)
sub0squad_add_test(test_thrash agents)
set_tests_properties(test_thrash PROPERTIES TIMEOUT 1800)
set_tests_properties(test_nearpass PROPERTIES TIMEOUT 1800)
set_tests_properties(test_executors PROPERTIES TIMEOUT 900)
sub0squad_add_test(test_e2e kernel)
set_tests_properties(test_e2e PROPERTIES TIMEOUT 300)
sub0squad_add_test(test_idle kernel)
set_tests_properties(test_idle PROPERTIES TIMEOUT 120)
sub0squad_add_test(test_shim runtime)
add_executable(test_adapter tests/test_adapter.cpp)
target_compile_options(test_adapter PRIVATE ${SUB0SQUAD_STRICT})
target_include_directories(test_adapter PRIVATE tests)
target_link_libraries(test_adapter PRIVATE sub0squad_kernel simdjson::simdjson)
add_test(NAME test_adapter COMMAND test_adapter ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/optimization-run-1-stalled.json)
set_tests_properties(test_shim PROPERTIES TIMEOUT 120)

# The M0 probe forks so a crash or a hang is an answer; it answered on Linux and stays a POSIX-only record (docs/PLATFORM.md).
if(NOT WIN32)
  add_executable(m0_graph_growth tests/m0_graph_growth.cpp)
  target_compile_options(m0_graph_growth PRIVATE ${SUB0SQUAD_STRICT})
  target_link_libraries(m0_graph_growth PRIVATE Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Threads::Threads)
  add_test(NAME m0_graph_growth COMMAND m0_graph_growth)
endif()

# Every test's tier (smoke or full) and area labels, checked at configure; after the last add_test.
include(cmake/TestTiers.cmake)

# sub0perf benchmark (Sub0Perf tooling): Sub0Squad_Bench, plus _json, _runs, _check and _baseline targets.
# One executable, so one baseline per host (perf/baselines/<host>.json): the kernel's pairs and the platform primitives' (bench_platform.cpp).
sub0perf_add_benchmark(Sub0Squad_Bench benchmarks/bench_squad.cpp benchmarks/bench_platform.cpp)
target_include_directories(Sub0Squad_Bench PRIVATE include src)
target_link_libraries(Sub0Squad_Bench PRIVATE sub0squad_kernel Sub0Platform::sub0platform simdjson::simdjson)
if(WIN32)
  target_link_libraries(Sub0Squad_Bench PRIVATE ws2_32)   # the hand-written socket twin calls Winsock directly
endif()
