cmake_minimum_required(VERSION 3.22)
project(threadschedd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(threadschedd
    src/main.cpp
    src/config.cpp
    src/proc_reader.cpp
    src/cpu_binder.cpp
    src/core_topology.cpp
    src/event_watcher.cpp
    src/sampler.cpp
)

target_include_directories(threadschedd PRIVATE
    include
    third_party
)
target_compile_options(threadschedd PRIVATE -Wall -Wextra -Wpedantic)

if (ANDROID)
    target_link_libraries(threadschedd PRIVATE log)
endif()
