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
  src/journal.cpp)
target_include_directories(sub0squad_core PUBLIC include PRIVATE src)
target_compile_options(sub0squad_core PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(sub0squad_core PUBLIC Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Sub0Pub::Sub0Pub Threads::Threads PRIVATE simdjson::simdjson)

add_executable(sub0squad src/sub0squad.cpp)
target_compile_options(sub0squad PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(sub0squad PRIVATE sub0squad_core simdjson::simdjson)

enable_testing()
add_test(NAME dependency_selftest COMMAND sub0squad --selftest)

function(sub0squad_add_test name)
  add_executable(${name} tests/${name}.cpp)
  target_compile_options(${name} PRIVATE ${SUB0SQUAD_STRICT})
  target_include_directories(${name} PRIVATE tests)
  target_link_libraries(${name} PRIVATE sub0squad_core simdjson::simdjson ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

sub0squad_add_test(test_job_spec)
sub0squad_add_test(test_journal)

add_executable(m0_graph_growth tests/m0_graph_growth.cpp)
target_compile_options(m0_graph_growth PRIVATE ${SUB0SQUAD_STRICT})
target_link_libraries(m0_graph_growth PRIVATE Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Threads::Threads)
add_test(NAME m0_graph_growth COMMAND m0_graph_growth)
