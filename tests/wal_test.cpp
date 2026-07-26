#include "wal.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Rec {
    Op op;
    std::string key;
    std::string val;
};

// Make a temporary path
std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("lsm_wal_" + name + ".log"))
        .string();
}

// Open a fresh Wal on the path and collect every record replay hands back.
std::vector<Rec> replay_all(const std::string& path) {
    Wal w(path, true);
    std::vector<Rec> out;
    w.replay([&](Op op, const std::string& k, const std::string& v) {
        out.push_back({op, k, v});
    });
    return out;
}

}  // namespace


// Test that appending, reopening, and replaying maintains
// what we wrote in order (including empty val and tombstone)
TEST(Wal, RoundTripsRecords) {
    const std::string path = temp_path("roundtrip");
    std::filesystem::remove(path);
    {
        Wal w(path, true);
        w.append(Op::Put, "a", "1");
        w.append(Op::Put, "b", "");
        w.append(Op::Delete, "a", "");
    }

    auto recs = replay_all(path);
    ASSERT_EQ(recs.size(), 3u);
    EXPECT_EQ(recs[0].op, Op::Put);
    EXPECT_EQ(recs[0].key, "a");
    EXPECT_EQ(recs[0].val, "1");
    EXPECT_EQ(recs[1].op, Op::Put);
    EXPECT_EQ(recs[1].key, "b");
    EXPECT_EQ(recs[1].val, "");
    EXPECT_EQ(recs[2].op, Op::Delete);
    EXPECT_EQ(recs[2].key, "a");
    std::filesystem::remove(path);
}

// A crash mid-write leaves a truncated final record. Replay must recover the
// intact prefix and drop the torn tail, testing the bounds checks.
TEST(Wal, TornTailRecoversPrefix) {
    const std::string path = temp_path("torntail");
    std::filesystem::remove(path);
    {
        Wal w(path, true);
        w.append(Op::Put, "k1", "v1");
        w.append(Op::Put, "k2", "v2");
    }
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 3);  // lop the tail of record 2

    auto recs = replay_all(path);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].key, "k1");
    std::filesystem::remove(path);
}

// Flip a byte inside a record: lengths still parse, but the CRC no longer
// matches. Replay must reject it and stop cleanly, testing the CRC check.
TEST(Wal, CorruptedRecordIsRejected) {
    const std::string path = temp_path("corrupt");
    std::filesystem::remove(path);
    {
        Wal w(path, true);
        w.append(Op::Put, "hello", "world");
    }
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        char c = 0;
        f.seekg(6);
        f.get(c);
        f.seekp(6);
        f.put(static_cast<char>(c ^ 0xFF));
    }

    auto recs = replay_all(path);
    EXPECT_EQ(recs.size(), 0u);  // corruption caught; nothing trusted
    std::filesystem::remove(path);
}
