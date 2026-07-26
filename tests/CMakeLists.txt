include(FetchContent)

# Pull in GoogleTest at configure time so the project builds on a clean machine
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

# One test executable per component keeps failures easy to localize.
add_executable(memtable_test memtable_test.cpp)
target_link_libraries(memtable_test PRIVATE lsm GTest::gtest_main)
# Tests reach into the engine's PRIVATE headers (src/), so add that path here.
target_include_directories(memtable_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(memtable_test)

add_executable(wal_test wal_test.cpp)
target_link_libraries(wal_test PRIVATE lsm GTest::gtest_main)
target_include_directories(wal_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(wal_test)

add_executable(sstable_test sstable_test.cpp)
target_link_libraries(sstable_test PRIVATE lsm GTest::gtest_main)
target_include_directories(sstable_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(sstable_test)
