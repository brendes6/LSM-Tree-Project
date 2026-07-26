#ifndef SSTABLE_H
#define SSTABLE_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "memtable.h"

// SSTable: Immutable, sorted on-disk table with layout:

// 1. Entries: [key_len u32][key][value_len u32][value][op u8]
// 2. Index: sparse index store - [key_len u32][key][offset u64] stored for every 16 entries
// 3. Footer: metadata stored - [index_offset u64][entry_count u64][magic num u32]

namespace sstable {
constexpr std::uint32_t kMagic      = 0x4C534D31;  // magic number - encoded 'LSM1'
constexpr std::size_t   kFooterSize = 20;
constexpr std::size_t   kIndexEvery = 16;
}  // namespace sstable

// streams a frozen memtable's sorted entries out as one SSTable file
class SSTableWriter {
  public:
    static void write(const std::string& path,
                      const std::map<std::string, Entry>& entries);
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

  private:
    int fd_;
    // Sparse index loaded into memory: sorted (key, offset-into-data-block)
    std::vector<std::pair<std::string, std::uint64_t>> index_;
    std::uint64_t data_end_;  // byte offset where data block ends
};

#endif  // SSTABLE_H
