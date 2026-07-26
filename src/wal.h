#ifndef WAL_H
#define WAL_H

#include <functional>
#include <string>

#include "memtable.h"


// Write-ahead log. Every write is appended (and optionally fsync-ed) on here
// before it is written to the memtable and acknowledged back to the DB, so a 
// crash can replay the log to re-fill the memtable. Records are formatted:

// [op: u8][key_len:u32][key bytes][val_len:u32][val bytes][crc32: u32]

class Wal {
  public:
    // Opens (creates if missing) the log file at path in append mode
    Wal(const std::string& path, bool sync_on_write);
    ~Wal();

    // since it owns a file, dont allow copying
    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;

    // Serialize one record, write it, and (if sync_on_write) fsync it
    void append(Op op, const std::string& key, const std::string& value);

    // read valid records front-to-back, passing to 'apply' so the DB can
    // rebuild the memtable, stopping at a corrupt record
    void replay(
        const std::function<void(Op op,
                                 const std::string& key,
                                 const std::string& value)>& apply);

  private:
    int  fd_;
    // toggle to fsync data immediately on write
    bool sync_on_write_;
};

#endif  // WAL_H
