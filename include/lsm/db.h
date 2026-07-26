#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "wal.h"
#include "sstable.h"

namespace lsm {

struct Options {
    std::string   dir;                       // where data + WAL live
    std::size_t   memtable_bytes = 4u << 20; // flush threshold (4 MiB)
    std::size_t   sstable_trigger = 4;       // num sstables to trigger compaction
    bool          sync_on_write  = true;     // fsync WAL on each Put?
};

class DB {
  public:
    // opens (or creates) a db in Options::dir, replaying WAL to 
    // rebuild memtable in case of crash
    static DB open(const Options& opts);

    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    void erase(const std::string& key);   // tombstone

    // ordered range scan over [start, end). Returns key/value pairs.
    std::vector<std::pair<std::string, std::string>>
    scan(const std::string& start, const std::string& end);

    void close();                          // flush + fsync + shut down
  
  private:
    void flush();
    Options                                 opts_;
    std::unique_ptr<Wal>                    wal_;       // owns the log fd
    Memtable                                memtable_;  // active, in-RAM
    std::vector<std::unique_ptr<SSTableReader>> sstables_;  // creation order
    std::uint64_t                           next_sst_id_ = 0;  // for filenames
};

} // namespace lsm