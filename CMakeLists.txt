cmake_minimum_required(VERSION 3.21)
# WORKING TITLE: the final sub0* name is pending (docs/DECISIONS.md). Keep the name in this file, cmake/, src/ and README.md only.
project(Sub0Squad VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

find_package(Threads REQUIRED)
add_executable(sub0squad src/sub0squad.cpp)
target_compile_options(sub0squad PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sub0squad PRIVATE Sub0Pipeline::Sub0Pipeline Sub0Pipeline::Priority Sub0Pub::Sub0Pub Threads::Threads)

enable_testing()
add_test(NAME dependency_selftest COMMAND sub0squad --selftest)
