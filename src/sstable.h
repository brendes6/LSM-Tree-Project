#ifndef SSTABLE_H
#define SSTABLE_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "memtable.h"
#include "bloom.h"

// SSTable: Immutable, sorted on-disk table with layout:

// 1. Entries: [key_len u32][key][value_len u32][value][op u8]
// 2. Index: sparse index store - [key_len u32][key][offset u64] stored for every 16 entries
// 3. Footer: metadata stored - [index_offset u64][entry_count u64][magic num u32]

namespace sstable {
constexpr std::uint32_t kMagic      = 0x4C534D31;  // magic number - encoded 'LSM1'
constexpr std::size_t   kFooterSize = 28;
constexpr std::size_t   kIndexEvery = 16;
}  // namespace sstable

// streams a frozen memtable's sorted entries out as one SSTable file
class SSTableWriter {
  public:
    static void write(const std::string& path,
                      const std::map<std::string, Entry>& entries);
    // Overload for an already-sorted, unique-key sequence (compaction output).
    static void write(const std::string& path,
                      const std::vector<std::pair<std::string, Entry>>& entries);
};

// opens an SSTable: reads the footer, loads the sparse index into memory,
// and leaves the (large) data block on disk to be read on demand
class SSTableReader {
  public:
    explicit SSTableReader(const std::string& path);
    ~SSTableReader();
    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    // Look up a key in this SSTable - either nullopt, or entry 
    // possibly corresponding to a put or delete op
    std::optional<Entry> get(const std::string& key);
    const std::string& path() const;

    // forward iterator over this table's entries in key order. Used by the
    // compaction k-way merge (and later by scan).
    class Iterator {
      public:
        explicit Iterator(const SSTableReader& reader);
        bool valid() const { return valid_; }
        const std::string& key() const { return key_; }
        const Entry& entry() const { return entry_; }
        void next();                       // advance to the next entry
      private:
        std::string  data_;                // the data block, loaded once
        std::size_t  pos_ = 0;             // cursor into data_
        std::string  key_;                 // current entry's key
        Entry        entry_;               // current entry's op + value
        bool         valid_ = false;
        void parse_current();              // parse entry at pos_, advance pos_
    };
    Iterator iterator() const { return Iterator(*this); }

  private:
    int fd_;
    // Sparse index loaded into memory: sorted (key, offset-into-data-block)
    std::vector<std::pair<std::string, std::uint64_t>> index_;
    std::unique_ptr<Bloom> bloom_;
    std::uint64_t data_end_;  // byte offset where data block ends
    std::string path_;
};

#endif  // SSTABLE_H
