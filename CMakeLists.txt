cmake_minimum_required(VERSION 3.21)
# WORKING TITLE: the final sub0* name is pending (docs/DECISIONS.md). Keep the name in this file, cmake/, src/ and README.md only.
project(Sub0Work VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

find_package(Threads REQUIRED)
add_executable(sub0workd src/sub0workd.cpp)
target_compile_options(sub0workd PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sub0workd PRIVATE Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Sub0Pub::Sub0Pub Threads::Threads)

enable_testing()
add_test(NAME dependency_selftest COMMAND sub0workd --selftest)
