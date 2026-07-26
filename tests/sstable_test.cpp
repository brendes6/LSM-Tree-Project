#include "memtable.h"
#include "sstable.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("lsm_sst_" + name + ".sst"))
        .string();
}

// Zero-padded so lexicographic order matches numeric order.
std::string k(int i) {
    char b[16];
    std::snprintf(b, sizeof b, "key%05d", i);
    return b;
}

}  // namespace


// Flush a created memtable using the writer, reopen with the reader, and assert
// every key still exists with correct ops and values
TEST(SSTable, RoundTripManyKeys) {
    const std::string path = temp_path("roundtrip");
    std::filesystem::remove(path);

    Memtable m;
    const int N = 50;
    for (int i = 0; i < N; ++i) m.put(k(i), "val" + std::to_string(i));
    m.erase(k(7));      // tombstone in the middle
    m.erase(k(N - 1));  // tombstone on the last key

    SSTableWriter::write(path, m.entries());
    SSTableReader r(path);

    for (int i = 0; i < N; ++i) {
        auto e = r.get(k(i));
        ASSERT_TRUE(e.has_value()) << "missing key " << k(i);
        if (i == 7 || i == N - 1) {
            EXPECT_EQ(e->op, Op::Delete) << "expected tombstone at " << k(i);
        } else {
            EXPECT_EQ(e->op, Op::Put);
            EXPECT_EQ(e->value, "val" + std::to_string(i));
        }
    }

    // Keys outside the range and a bogus one all miss cleanly.
    EXPECT_FALSE(r.get("aaa_before_all").has_value());
    EXPECT_FALSE(r.get("zzz_after_all").has_value());
    EXPECT_FALSE(r.get("key99999").has_value());

    std::filesystem::remove(path);
}
