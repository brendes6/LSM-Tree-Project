#ifndef COMPACTION_H
#define COMPACTION_H

#include <string>
#include <vector>

#include "sstable.h"


// size-tiered compaction: k-way merge multiple SSTable's into one new sstable,
// keeping only the newest value per key and dropping tombstones
class Compaction {
  public:
    // `inputs` are ordered oldest to newest, so key ties resolve to the newest.
    static void compact(const std::vector<SSTableReader*>& inputs,
                        const std::string& out_path);
};

#endif  // COMPACTION_H
