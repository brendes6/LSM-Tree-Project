#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lsm {

struct Options {
    std::string   dir;                       // where data + WAL live
    std::size_t   memtable_bytes = 4u << 20; // flush threshold (4 MiB)
    std::size_t   sstable_trigger = 4;       // num sstables to trigger compaction
    bool          sync_on_write  = true;     // fsync WAL on each Put?
};

class DB {
  public:
    // Opens (or creates) a db in Options::dir, replaying WAL to 
    // rebuild memtable in case of crash
    static DB open(const Options& opts);

    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    void erase(const std::string& key);   // tombstone

    // Ordered range scan over [start, end). Returns key/value pairs.
    std::vector<std::pair<std::string, std::string>>
    scan(const std::string& start, const std::string& end);

    void close();                          // flush + fsync + shut down
};

} // namespace lsm