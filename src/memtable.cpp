#include "memtable.h"

// constructor
Memtable::Memtable() : size_bytes(0) {}

// get: look up a key without inserting (using .find())
std::optional<Entry> Memtable::get(const std::string& key) {

    auto it = map_.find(key);

    if (it != map_.end()){
        return it->second;
    }

    return std::nullopt;

}


// put: insert-or-overwrite, update size_bytes accurately
void Memtable::put(std::string key, std::string value) {
    auto it = map_.find(key);

    // if key exists, update size bytes via delta of values
    if (it != map_.end()){
        size_bytes += (value.size() - it->second.value.size());
    }
    else {
        size_bytes += (key.size() + value.size()); // not there, just add this kv
    }  

    map_[key] = Entry{Op::Put, value};

}

// erase: same logic as put, but adding a Op::Delete
void Memtable::erase(const std::string& key) {
    auto it = map_.find(key);

    if (it != map_.end()){
        size_bytes -= it->second.value.size();
    }
    else {
        size_bytes += key.size();
    }

    map_[key] = Entry{Op::Delete, ""};


}

// size_bytes accessor
std::size_t Memtable::get_size_bytes() const {
    return size_bytes;
}
