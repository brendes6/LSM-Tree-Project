#include "lsm/db.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string fresh_dir(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / ("lsm_scan_" + name);
    std::filesystem::remove_all(p);
    return p.string();
}

// zero-padded so lexicographic order matches numeric order
std::string key(int i) {
    char b[8];
    std::snprintf(b, sizeof b, "k%03d", i);
    return b;
}

}  // namespace

using lsm::DB;
using lsm::Options;


// tiny memtable forces flush, so scan must merge memtable + several sstables,
// using tombstones and honoring the range
TEST(Scan, MergesLevelsNewestWinsDropsTombstones) {
    Options opts;
    opts.dir = fresh_dir("merge");
    opts.memtable_bytes = 64;  // force multiple SSTables

    std::map<std::string, std::string> oracle;
    DB db = DB::open(opts);
    for (int i = 0; i < 200; ++i) {
        std::string k = key(i % 40);  // reuse keys -> overwrites across levels
        std::string v = "v" + std::to_string(i);
        db.put(k, v);
        oracle[k] = v;
    }
    db.erase(key(10));
    oracle.erase(key(10));
    db.erase(key(25));
    oracle.erase(key(25));

    const std::string start = key(5), end = key(30);
    auto result = db.scan(start, end);

    std::vector<std::pair<std::string, std::string>> expected;
    for (auto it = oracle.lower_bound(start); it != oracle.end() && it->first < end;
         ++it)
        expected.push_back(*it);

    EXPECT_EQ(result, expected);  // sorted, newest values, deletions excluded
    db.close();
    std::filesystem::remove_all(opts.dir);
}

// half-open interval: start inclusive, end exclusive; empty range yields empty
TEST(Scan, BoundariesAreHalfOpen) {
    Options opts;
    opts.dir = fresh_dir("bounds");
    DB db = DB::open(opts);
    db.put("b", "1");
    db.put("d", "2");
    db.put("f", "3");

    using V = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(db.scan("b", "e"), (V{{"b", "1"}, {"d", "2"}}));  // f excluded (>= e)
    EXPECT_EQ(db.scan("b", "d"), (V{{"b", "1"}}));              // d excluded (end)
    EXPECT_TRUE(db.scan("x", "z").empty());                    // nothing in range

    db.close();
    std::filesystem::remove_all(opts.dir);
}
