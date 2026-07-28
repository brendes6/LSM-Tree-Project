# LSM-Tree Storage Engine (C++)

A small, persistent, crash-save key-value storage engine built from scratch in modern C++, using the Log-Structured Merge-tree design that sits underneath many popular databases like LevelDB, Cassandra, and RocksDB.

I build this project after reading about LSM-Tree databases in DDIA and wanted to understand how the storage layer of databases work beyond the abstractions under which I've used them in other projects. I wanted to build out the write-ahead log, in-memory memtables, on-disk SSTables, bloom filters, and compaction by working to implement the pieces myself. My goal wasn't to beat RocksDB but to learn more about how these database internals work and build my C++ skills with a tested, from-scratch implementation of this idea.


## Public API

The engine exposes these six key operations:

```cpp
namespace lsm {
class DB {
  public:
    static DB open(const Options& opts);          // create/open; replays the WAL

    void put(const std::string& key, const std::string& value);

    std::optional<std::string> get(const std::string& key);

    void erase(const std::string& key);           // add a tombstone

    std::vector<std::pair<std::string,std::string>>
        scan(const std::string& start, const std::string& end);  // [start, end)

    void close();                                 // flush + fsync + shut down
};
}
```

Keys and values are stored as byte strings. Empty values are allowed, and a key that is
missing vs a deleted key are different things.

## Architecture

```
WRITE PATH                          READ PATH
----------                          ---------
put(k, v)                           get(k)
  -> append to WAL (fsync)            -> check active memtable
  -> insert into memtable             -> for each SSTable, newest -> oldest:
  -> if memtable full:                     -> ask its bloom filter first
       freeze it, flush to SSTable         -> if "maybe": binary-search the
       start a fresh memtable                 sparse index, read that block
  -> when enough SSTables pile up:     -> first hit wins; a tombstone hit
       compact them (k-way merge)         means "deleted" -> not found
```

Two invariants hold the whole thing together:

The storage engine has two invariants that allow the implementation to work:

1. **Newer data shadows older data.**: A key can exist in the memtable and
   several SSTables at once, so reads need to stop at the first (newest) copy, and compaction
   keeps only the newest key entry.
2. **On-disk files are immutable.**: SSTables are never edited in place. Updates
   and deletes cause new writes elsewhere, and compaction produces new files and
   atomically retires the old ones. This is what makes crash recovery and
   concurrent reads tractable.

## How it works

**Write path:**

Every mutation is first appended to the *write-ahead log* and (optionally)
fsync'd before it is acknowledged, which guarentees durability. This mutation then
goes into the *memtable*, which is an in-memory sorted key-value store (thus I use
a `std::map`). When the memtable crosses a size threshold it is flushed to an immutable
*SSTable* file on disk and the WAL is cleared.


**WAL record format**:

Stored in little-endian and explicit to allow cross-machine correctness

```
[op: u8][key_len: u32][key][value_len: u32][value][crc32: u32]
```

On restart, the WAL is replayed front-to-back to rebuild the memtable. The
trailing CRC lets recovery detect a crash-torn tail record and stop cleanly
instead of reading garbage.

**SSTable format**:

SSTables are immutable sorted files on disk storing:

```
[Data]   entry* : key_len | key | value_len | value | op   (ascending by key)
[Index]  sparse: every 16th entry -> key | offset          (binary-searchable)
[Bloom]  serialized bloom filter (k, m, bits)
[Footer] fixed 28 bytes: index_offset | entry_count | bloom_offset | MAGIC
```

A reader opens the file, seeks to the fixed-size footer, and loads the small
index and bloom blocks into memory. The large data block stays on disk and is
read on demand.

**Read path.**:

We check the memtable first using `get`, and if the key is not present then each SSTable newest → oldest. For
each SSTable it asks the *bloom filter* first. The filter can either say that the key is "definitely not" in the SSTable, or "maybe". On a definitely not, the SSTable is skipped. On a "maybe," it
binary-searches the sparse index to a block, reads just that block, and scans it.
The first entry wins, and a tombstone hit is a definitive "not found."

**Bloom filter.**:

A bit array with *k* hash functions derived from two base
hashes (double hashing over a 64-bit FNV-1a). Sized for a target 1% false-
positive rate. It can answer "definitely not" or "maybe" about a key's presence in a sstable, but never a false
negative. A correctly built filter cannot produce one, so zero false negatives
is an invariant the tests guard, not a rate to tune.

**Compaction.**:

Size-tiered: when enough SSTables accumulate, they're merged via
a *k-way merge* (a min-heap over the tables' iterators). For each key, only the
newest version is kept, and tombstones are dropped (safe here because a full merge
leaves no older file that could resurrect the key). The new file is written and
fsync'd *before* the old ones are unlinked, so a crash at any moment leaves a
recoverable database.

## Benchmarks

Measured with the included harness (`bench/lsm_stats`) on Apple Silicon (macOS),
single-threaded, 100-byte values, `sync_on_write` off, warm OS page cache. These
are numbers from my machine, so run it yourself with `./build/bench/lsm_stats`.

I ran it at two scales on purpose, because the interesting part of an LSM tree is
how it behaves as the data grows.

I ran it at both a 200k scale and 1m scale to observe how the LSM
tree behaved as the size of data grows.

| Metric | 200K keys | 1M keys |
|---|---|---|
| Sequential write throughput | ~113K ops/sec (~13.5 MB/sec) | ~50K ops/sec (~6 MB/sec) |
| Point read p50 / p95 / p99 | 3.7 / 5.0 / 8.0 us | 3.8 / 4.8 / 6.0 us |
| Write amplification (disk / client bytes) | 2.86x | 7.79x |
| Space amplification (disk / logical bytes) | 1.10x | 1.10x |
| Bloom filter false-positive rate (target 1%) | 0.921% | 0.921% |

These benchmarks revealed that as we scale the data:

* **Write amplification climbs from 2.86x to 7.79x** as the dataset grows. This is due to the compaction method I use of merging *every* SSTable when a merge is triggered, which makes it rewrite an ever-larger
  table each round, where a real tiered or leveled scheme would bound it. This is the
  clearest limitation of the project, and it is exactly the write-amplification
  price an LSM tree pays for cheap writes.
* **Point reads stay flat** (~3.8 us) as the data grows, because each SSTable's
  bloom filter and sparse index keep a lookup at roughly constant work no matter
  how large the dataset gets.
* **Space amplification stays at 1.10x**, since compaction keeps reclaiming
  superseded data.

## Compared to real databases

**What I did the same:** WAL for durability, sorted memtable → immutable SSTables,
per-SSTable bloom filters, a sparse block index, and compaction to control read/
space amplification.

**What I simplified, and why:**
- **Size-tiered (major) compaction** instead of leveled. It's simpler to get
  correct, which was the point. RocksDB uses *leveled* compaction, which trades
  higher write amplification for lower space and read amplification, a better
  fit at production scale. My simple "merge everything on trigger" strategy has
  higher write amplification than a real tiered scheme would.
- **`std::map` memtable** instead of a skip list. RocksDB uses a skip list for
  better concurrency and cache behavior; a `std::map` is a red-black tree that's
  perfectly correct and much easier to reason about and implement for a small project.
- **Single-threaded and cooperative.** Compaction runs inline, not on a
  background thread.

## Known limitations / what I'd change for production

- **Leveled compaction** to cut write amplification and bound the number of
  files a read touches.
- **Background compaction** on its own thread, plus **concurrent reads** via
  `std::shared_mutex` (the immutable-file + atomic-swap design already makes
  this approachable).
- **WAL segment rotation** instead of a single growing/truncating log.
- **Directory `fsync`** after creating/renaming SSTables, so the directory entry
  itself is durable across a crash.

## Build & test

Requires CMake ≥ 3.16 and a C++17 compiler. GoogleTest and Google Benchmark are
fetched automatically.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure   # run the tests
./build/bench/lsm_stats                       # print the benchmark numbers
./build/bench/bench_main                      # Google Benchmark micro-benchmarks
```

Tests cover: WAL round-trip / torn-tail recovery / corruption detection,
memtable newest-wins and tombstones, SSTable round-trip, bloom filter zero-
false-negatives and measured FPR, compaction newest-wins and tombstone handling,
and end-to-end DB correctness against a `std::map` oracle across restarts.
