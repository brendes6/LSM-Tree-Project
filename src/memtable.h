#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <map>
#include <string>
#include <optional>
#include <cstdint>


enum class Op { Put, Delete };

struct Entry {
    Op op;
    std::string value;
};


class Memtable {
    public:
        // constructor - doesn't need anything passed in
        Memtable();

        // get method
        std::optional<Entry> get(const std::string& key);

        // put method
        void put(std::string key, std::string value);

        // erase method
        void erase(const std::string& key);

        std::size_t get_size_bytes() const;

        // sorted (key, Entry) view, used when flushing to an SSTable
        const std::map<std::string, Entry>& entries() const { return map_; }

    private:
        // The hashmap itself
        std::map<std::string, Entry> map_;
        std::size_t size_bytes;

};



#endif