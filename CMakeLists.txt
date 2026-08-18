cmake_minimum_required(VERSION 3.14)
project(Corium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Header-only library target
add_library(corium INTERFACE)
target_include_directories(corium INTERFACE include)

# Showcase Sample Targets
add_executable(sample_01_smart_grid_substation samples/01_smart_grid_substation/main.cpp)
target_link_libraries(sample_01_smart_grid_substation PRIVATE corium)

add_executable(sample_02_aerospace_flight_controller samples/02_aerospace_flight_controller/main.cpp)
target_link_libraries(sample_02_aerospace_flight_controller PRIVATE corium)

add_executable(sample_03_hft_market_data_engine samples/03_hft_market_data_engine/main.cpp)
target_link_libraries(sample_03_hft_market_data_engine PRIVATE corium)

add_executable(sample_04_automotive_braking_ecu samples/04_automotive_braking_ecu/main.cpp)
target_link_libraries(sample_04_automotive_braking_ecu PRIVATE corium)

add_executable(sample_05_drone_ground_control_ipc samples/05_drone_ground_control_ipc/main.cpp)
target_link_libraries(sample_05_drone_ground_control_ipc PRIVATE corium)

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
        tests/test_logging.cpp
        tests/test_no_queue_service.cpp
        tests/test_clock_policies.cpp
        tests/test_isr_sink.cpp
        tests/test_batch_pump.cpp
        tests/test_fsm.cpp
        tests/test_async_task.cpp
        tests/test_wire_protocol.cpp
        tests/test_profiler.cpp
        tests/test_safety.cpp
        tests/test_ipc.cpp
        tests/test_stress_mpsc.cpp
        tests/fuzz_wire_deserialize.cpp
        tests/test_async_advanced.cpp
        tests/test_json_log_sink.cpp
    )
    target_link_libraries(corium_tests PRIVATE corium gtest)

    if(MSVC)
        target_link_options(corium_tests PRIVATE /STACK:8388608)
    endif()

    add_executable(corium_zero_heap_test tests/test_zero_heap_guarantee.cpp)
    target_link_libraries(corium_zero_heap_test PRIVATE corium)

    option(CORIUM_ENABLE_COVERAGE "Enable gcov code coverage" OFF)
    if(CORIUM_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(corium_tests PRIVATE --coverage -fprofile-update=atomic)
        target_link_options(corium_tests PRIVATE --coverage)
    endif()

    include(GoogleTest)
    gtest_discover_tests(corium_tests)
    add_test(NAME ZeroHeapGuaranteeTest COMMAND corium_zero_heap_test)
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

    add_executable(
        corium_benchmarks
        benchmarks/bench_main.cpp
        benchmarks/bench_ring_buffer.cpp
        benchmarks/bench_event_dispatch.cpp
        benchmarks/bench_batch_pump.cpp
    )
    target_link_libraries(corium_benchmarks PRIVATE corium benchmark::benchmark)
endif()

# Doxygen Documentation Target
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen HTML documentation with Mermaid.js support..."
        VERBATIM
    )
endif()

