cmake_minimum_required(VERSION 3.20)
project(md_replay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello)
target_sources(hello PRIVATE src/hello.cpp)
