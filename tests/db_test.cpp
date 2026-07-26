#include "lsm/db.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace {

std::string fresh_dir(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / ("lsm_db_" + name);
    std::filesystem::remove_all(p);
    return p.string();
}

}  // namespace

using lsm::DB;
using lsm::Options;

TEST(DB, PutGetEraseOverwrite) {
    Options opts;
    opts.dir = fresh_dir("basic");
    DB db = DB::open(opts);

    db.put("a", "1");
    db.put("b", "2");
    EXPECT_EQ(db.get("a"), std::optional<std::string>("1"));
    db.put("a", "11");  // overwrite -> newest wins
    EXPECT_EQ(db.get("a"), std::optional<std::string>("11"));
    db.erase("b");
    EXPECT_FALSE(db.get("b").has_value());     // tombstone reads absent
    EXPECT_FALSE(db.get("nope").has_value());  // never existed
    db.close();

    std::filesystem::remove_all(opts.dir);
}

// tiny memtable -> many flushes -> data spread across many SSTables. tests
// sstable reading/loading
TEST(DB, SurvivesCleanRestart) {
    Options opts;
    opts.dir = fresh_dir("restart");
    opts.memtable_bytes = 128;  // force frequent flushing

    std::map<std::string, std::string> oracle;

    {
        DB db = DB::open(opts);
        db.put("sentinel", "keepme");  // lands in the first SSTable, never touched
        oracle["sentinel"] = "keepme";

        for (int i = 0; i < 300; ++i) {
            std::string key = "k" + std::to_string(i % 40);  // reuse -> overwrites
            std::string val = "v" + std::to_string(i);
            db.put(key, val);
            oracle[key] = val;
        }
        for (int i = 0; i < 8; ++i) {
            std::string key = "k" + std::to_string(i);
            db.erase(key);
            oracle.erase(key);
        }
        db.close();
    }

    DB db2 = DB::open(opts);
    EXPECT_EQ(db2.get("sentinel"),
              std::optional<std::string>("keepme"));  // must reach oldest table
    for (int i = 0; i < 40; ++i) {
        std::string key = "k" + std::to_string(i);
        auto got = db2.get(key);
        auto it = oracle.find(key);
        if (it == oracle.end()) {
            EXPECT_FALSE(got.has_value()) << key << " should be absent";
        } else {
            ASSERT_TRUE(got.has_value()) << key << " should be present";
            EXPECT_EQ(*got, it->second) << "wrong value for " << key;
        }
    }
    db2.close();

    std::filesystem::remove_all(opts.dir);
}

// No close() -> simulates a crash. since data lives only in the WAL, reopen must
// replay it, including the newest overwrite and the tombstone.
TEST(DB, RecoversFromWalWithoutClose) {
    Options opts;
    opts.dir = fresh_dir("crash");
    opts.memtable_bytes = 1u << 20;  // large -> nothing flushes; all in the WAL

    {
        DB db = DB::open(opts);
        db.put("x", "1");
        db.put("y", "2");
        db.put("x", "1v2");
        db.erase("y");
        // deliberately NO close() -> crash
    }

    DB db2 = DB::open(opts);
    EXPECT_EQ(db2.get("x"), std::optional<std::string>("1v2"));  // newest put
    EXPECT_FALSE(db2.get("y").has_value());                      // tombstone

    std::filesystem::remove_all(opts.dir);
}
