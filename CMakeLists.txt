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

add_executable(sample_06_priority samples/06_priority_events/main.cpp)
target_link_libraries(sample_06_priority PRIVATE corium)

add_executable(sample_07_overflow samples/07_overflow_policies/main.cpp)
target_link_libraries(sample_07_overflow PRIVATE corium)

add_executable(sample_08_timer samples/08_timer_scheduler/main.cpp)
target_link_libraries(sample_08_timer PRIVATE corium)

add_executable(sample_09_desktop samples/09_desktop_gui/main.cpp)
target_link_libraries(sample_09_desktop PRIVATE corium)

# Optional Real GLFW Desktop GUI Sample
option(CORIUM_BUILD_GUI_GLFW "Build real GLFW desktop window sample" ON)

if(CORIUM_BUILD_GUI_GLFW)
    include(FetchContent)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(glfw)

    add_executable(sample_10_glfw samples/10_glfw_window/main.cpp)
    target_link_libraries(sample_10_glfw PRIVATE corium glfw)
    target_compile_definitions(sample_10_glfw PRIVATE CORIUM_HAS_GLFW=1)
endif()

# Unit Test Suite Target
option(CORIUM_BUILD_TESTS "Build Corium unit test suite" ON)

if(CORIUM_BUILD_TESTS)
    enable_testing()
    include(FetchContent)

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(
        corium_tests
        tests/main.cpp
        tests/test_mpsc_ring_buffer.cpp
        tests/test_fast_delegate.cpp
        tests/test_reactor.cpp
        tests/test_runtime.cpp
        tests/test_background_service.cpp
        tests/test_priority_queue.cpp
        tests/test_overflow_policy.cpp
        tests/test_timer_scheduler.cpp
        tests/test_ui_window.cpp
    )
    target_link_libraries(corium_tests PRIVATE corium gtest)

    include(GoogleTest)
    gtest_discover_tests(corium_tests)
endif()

# Optional Benchmark Suite Target
option(CORIUM_BUILD_BENCHMARKS "Build Corium benchmark suite" OFF)

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
