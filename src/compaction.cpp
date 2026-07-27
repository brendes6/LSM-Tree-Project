#include "compaction.h"

#include <cstddef>
#include <queue>
#include <utility>
#include <vector>

namespace {

// one pending entry in the merge, tagged with which input it came from
struct HeapItem {
    std::string key;
    Entry       entry;
    std::size_t input;   // source table index, larger index means newer table
};


// since std::priority_queue is a max-heap, ordering by key with '>' makes it a min
// heap so the smallest key can be at the top
struct ByKeyMin {
    bool operator()(const HeapItem& a, const HeapItem& b) const {
        return a.key > b.key;
    }
};

}  // namespace

void Compaction::compact(const std::vector<SSTableReader*>& inputs,
                         const std::string& out_path) {

    // assign one iterator to each SSTableReader input
    std::vector<SSTableReader::Iterator> iters;
    iters.reserve(inputs.size());
    for (auto* in : inputs) iters.push_back(in->iterator());

    // seed the heap with the first entry of each non-empty input
    std::priority_queue<HeapItem, std::vector<HeapItem>, ByKeyMin> heap;
    for (std::size_t i = 0; i < iters.size(); ++i)
        if (iters[i].valid())
            heap.push({iters[i].key(), iters[i].entry(), i});

    std::vector<std::pair<std::string, Entry>> merged;

    // pop items off the heap to be examined
    while (!heap.empty()){

        // find items with the same key
        std::string key = heap.top().key;
        std::vector<HeapItem> same_key;

        while (!heap.empty() && heap.top().key == key){
            same_key.push_back(heap.top());
            heap.pop();
        }

        // of these items, find the item with most recent sstable
        HeapItem to_add = same_key.front();

        for (const auto& item : same_key){
            iters[item.input].next();
            if (iters[item.input].valid()){
                heap.push({iters[item.input].key(), iters[item.input].entry(), item.input});
            }

            if (item.input > to_add.input){
                to_add = item;
            }
        }

        // if entry is a put operation, add to new sstable
        if (to_add.entry.op == Op::Put){
            merged.emplace_back(to_add.key, to_add.entry);
        }

    }

    SSTableWriter::write(out_path, merged);
}
