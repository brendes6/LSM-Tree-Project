#include "compaction.h"
#include "lsm/db.h"
#include "sstable.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string fresh_dir(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / ("lsm_cmp_" + name);
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p.string();
}

std::size_t count_sst(const std::string& dir) {
    std::size_t n = 0;
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".sst") ++n;
    return n;
}

}  // namespace


// test direct k-way merge: ensure newest value wins across tables, tombstone
// in the newest table removes the key entirely
TEST(Compaction, KWayMergeResolvesNewestAndDropsTombstones) {
    const std::string dir = fresh_dir("unit");

    // oldest -> newest
    std::map<std::string, Entry> t0 = {{"a", Entry{Op::Put, "1"}},
                                       {"b", Entry{Op::Put, "1"}},
                                       {"c", Entry{Op::Put, "1"}}};
    std::map<std::string, Entry> t1 = {{"b", Entry{Op::Put, "2"}},
                                       {"d", Entry{Op::Put, "2"}}};
    std::map<std::string, Entry> t2 = {{"a", Entry{Op::Put, "3"}},
                                       {"c", Entry{Op::Delete, ""}}};

    SSTableWriter::write(dir + "/0.sst", t0);
    SSTableWriter::write(dir + "/1.sst", t1);
    SSTableWriter::write(dir + "/2.sst", t2);

    std::vector<std::unique_ptr<SSTableReader>> readers;
    readers.push_back(std::make_unique<SSTableReader>(dir + "/0.sst"));
    readers.push_back(std::make_unique<SSTableReader>(dir + "/1.sst"));
    readers.push_back(std::make_unique<SSTableReader>(dir + "/2.sst"));

    std::vector<SSTableReader*> inputs;
    for (auto& r : readers) inputs.push_back(r.get());

    const std::string out = dir + "/merged.sst";
    Compaction::compact(inputs, out);

    SSTableReader m(out);
    auto a = m.get("a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->op, Op::Put);
    EXPECT_EQ(a->value, "3");  // newest table wins
    auto b = m.get("b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->value, "2");  // newer than t0's "1"
    auto d = m.get("d");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->value, "2");
    EXPECT_FALSE(m.get("c").has_value());  // tombstone dropped means key gone

    std::filesystem::remove_all(dir);
}


// end to end test: have a tiny memtable and low trigger force real flushes, assert
// sstable count actually collapsed and keys are still in the sstables after a restart
// with newest wins and tombstones
TEST(Compaction, DbCollapsesFilesAndStaysCorrect) {
    lsm::Options opts;
    opts.dir = fresh_dir("db");
    opts.memtable_bytes = 64;   // tiny -> frequent flushes
    opts.sstable_trigger = 4;   // compact after 4 tables

    std::map<std::string, std::string> oracle;
    {
        lsm::DB db = lsm::DB::open(opts);
        for (int i = 0; i < 500; ++i) {
            std::string k = "k" + std::to_string(i % 30);  // reuse -> overwrites
            std::string v = "v" + std::to_string(i);
            db.put(k, v);
            oracle[k] = v;
        }
        for (int i = 0; i < 10; ++i) {
            std::string k = "k" + std::to_string(i);
            db.erase(k);
            oracle.erase(k);
        }
        db.close();
    }

    EXPECT_LE(count_sst(opts.dir), opts.sstable_trigger)
        << "compaction did not collapse the SSTables";

    lsm::DB db2 = lsm::DB::open(opts);
    for (int i = 0; i < 30; ++i) {
        std::string k = "k" + std::to_string(i);
        auto got = db2.get(k);
        auto it = oracle.find(k);
        if (it == oracle.end()) {
            EXPECT_FALSE(got.has_value()) << k << " should be absent";
        } else {
            ASSERT_TRUE(got.has_value()) << k << " should be present";
            EXPECT_EQ(*got, it->second) << "wrong value for " << k;
        }
    }
    db2.close();
    std::filesystem::remove_all(opts.dir);
}
