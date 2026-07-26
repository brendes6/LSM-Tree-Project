#include "memtable.h"

#include <gtest/gtest.h>

// memtable behaviors such as newest-write-wins, tombstones, allowing
// empty values, byte counter

TEST(Memtable, GetOnMissingKeyReturnsNullopt) {
    Memtable m;
    EXPECT_FALSE(m.get("nope").has_value());   // absent key
}

TEST(Memtable, PutThenGetReturnsValue) {
    Memtable m;
    m.put("k", "v");
    auto e = m.get("k");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->op, Op::Put);
    EXPECT_EQ(e->value, "v");
}

TEST(Memtable, NewestWriteWins) {
    Memtable m;
    m.put("k", "old");
    m.put("k", "new");
    auto e = m.get("k");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->value, "new");
}

TEST(Memtable, EraseWritesTombstoneNotHole) {
    Memtable m;
    m.put("k", "v");
    m.erase("k");
    auto e = m.get("k");
    ASSERT_TRUE(e.has_value());       // assert still in the map
    EXPECT_EQ(e->op, Op::Delete);     // but as a tombstone, not a value
}


// Check that erasing a non-existent key still returns tombstone
// prevents ignored tombstones resurrecting old vals in sstable
TEST(Memtable, EraseAbsentKeyStillTombstones) {
    Memtable m;
    m.erase("ghost");                 // never put; not in the memtable
    auto e = m.get("ghost");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->op, Op::Delete);
}

TEST(Memtable, EmptyValueIsARealValue) {
    Memtable m;
    m.put("k", "");
    auto e = m.get("k");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->op, Op::Put);        // empty string != deleted
    EXPECT_EQ(e->value, "");
}

// Verify size tracking works with overwrites
TEST(Memtable, SizeTracksBytesAcrossOverwrite) {
    Memtable m;
    EXPECT_EQ(m.get_size_bytes(), 0u);
    m.put("ab", "cde");               // new: +2 +3
    EXPECT_EQ(m.get_size_bytes(), 5u);
    m.put("ab", "z");                 // overwrite: value 3 -> 1, delta -2
    EXPECT_EQ(m.get_size_bytes(), 3u);
}
