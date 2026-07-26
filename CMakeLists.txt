cmake_minimum_required(VERSION 3.16)
project(lsmtree CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json so editors know how files are combined
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror)

# The engine, built as a static library that tests and benchmarks link against.
add_library(lsm STATIC
    src/wal.cpp
    src/memtable.cpp
    src/sstable.cpp
    src/bloom.cpp
    src/compaction.cpp
    src/db.cpp)

# Public headers live in include/ private headers live in src folder
target_include_directories(lsm PUBLIC include PRIVATE src)

enable_testing()
add_subdirectory(tests)
