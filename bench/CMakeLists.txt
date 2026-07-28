include(FetchContent)

# Google Benchmark. Turn off its own tests so the fetch stays lean.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(benchmark)

# Micro-benchmarks: write/read/scan throughput.
add_executable(bench_main bench_main.cpp)
target_link_libraries(bench_main PRIVATE lsm benchmark::benchmark)
target_include_directories(bench_main PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Plain reporter that prints the README numbers (latency percentiles + amp).
add_executable(lsm_stats lsm_stats.cpp)
target_link_libraries(lsm_stats PRIVATE lsm)
target_include_directories(lsm_stats PRIVATE ${CMAKE_SOURCE_DIR}/src)
