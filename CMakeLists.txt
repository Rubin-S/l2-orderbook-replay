cmake_minimum_required(VERSION 3.20)
project(md_replay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello)
target_sources(hello PRIVATE src/hello.cpp)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(mdr_tests tests/test_main.cpp)
target_include_directories(mdr_tests PRIVATE include)

enable_testing()
add_test(NAME unit COMMAND mdr_tests)

add_executable(mdr_convert tools/convert.cpp)
target_include_directories(mdr_convert PRIVATE include)

add_executable(mdr_replay src/replay_main.cpp)
target_include_directories(mdr_replay PRIVATE include)