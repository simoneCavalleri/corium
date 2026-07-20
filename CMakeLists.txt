cmake_minimum_required(VERSION 3.14)
project(Corium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library target
add_library(corium INTERFACE)
target_include_directories(corium INTERFACE include)

# Sample targets
add_executable(sample_01_basic samples/01_basic_app/main.cpp)
target_link_libraries(sample_01_basic PRIVATE corium)

add_executable(sample_02_services samples/02_background_services/main.cpp)
target_link_libraries(sample_02_services PRIVATE corium)

add_executable(sample_03_events samples/03_custom_events/main.cpp)
target_link_libraries(sample_03_events PRIVATE corium)

add_executable(sample_04_policies samples/04_policy_runtimes/main.cpp)
target_link_libraries(sample_04_policies PRIVATE corium)

# Optional Benchmark Suite Target
option(CORIUM_BUILD_BENCHMARKS "Build Corium benchmark suite" ON)

if(CORIUM_BUILD_BENCHMARKS)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable Google Benchmark self-tests" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable Google Benchmark install" FORCE)

    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(corium_benchmarks benchmarks/main.cpp)
    target_link_libraries(corium_benchmarks PRIVATE corium benchmark::benchmark)
endif()
