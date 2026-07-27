#include "lsm/db.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <algorithm>


namespace lsm {


// return a DB by value, move-only
DB DB::open(const Options& opts) {
    // create the db object, and fill in the opts_, wal, memtable

    DB db;
    db.opts_ = opts;

    std::filesystem::create_directories(db.opts_.dir);
    std::string wal_filename = "wal.log";
    std::filesystem::path wal_path = std::filesystem::path(opts.dir) / wal_filename;

    db.wal_ = std::make_unique<Wal>(wal_path.string(), opts.sync_on_write);
    db.memtable_ =  Memtable();

    // replay the wal to capture any data from a crash 
    db.wal_->replay([&db](Op op, const std::string& k, const std::string& v) {
        if (op == Op::Put) db.memtable_.put(k, v);
        else               db.memtable_.erase(k);   // Op::Delete
    });

    // get list of .sst files in directory that 
    std::string target_extension = ".sst";
    std::vector<std::filesystem::path> matched_files;
    for (const auto& entry : std::filesystem::directory_iterator(db.opts_.dir)) {
        if (std::filesystem::is_regular_file(entry) && entry.path().extension() == target_extension) {
            matched_files.push_back(entry.path());
        }
    }

    // iterate files, parsing into SStableReader's and updating
    // vector of sstables, next sstable id
    std::sort(matched_files.begin(), matched_files.end());
    uint64_t next_id = 0;
    for (const auto& pth : matched_files) {
        db.sstables_.push_back(std::make_unique<SSTableReader>(pth.string()));
        try {
            if (std::stoi(pth.filename()) > (int)next_id){
                next_id = std::stoi(pth.filename());
            }
        }
        catch (const std::invalid_argument& e) {
            std::cout << "not a num" << std::endl;
        }
    }

    db.next_sst_id_ = next_id + 1;
    return db;
}

void DB::put(const std::string& key, const std::string& value) {
    // append data to wal first, then memtable, check for flush

    wal_->append(Op::Put, key, value);
    memtable_.put(key, value);

    if (memtable_.get_size_bytes() > opts_.memtable_bytes){
        flush();
    }
}

std::optional<std::string> DB::get(const std::string& key) {
    // check memtable for value, acknowledging tombstones
    std::optional<Entry> result = memtable_.get(key);

    if (result){
        if (result->op == Op::Put){
            return result->value;
        }
        else{
            return std::nullopt;
        }
    }

    // search in all sstables, potentially checking tombstones
    for (int i= (int)sstables_.size() - 1; i>=0; i--){
        result = sstables_[i]->get(key);
        if (result){
            if (result->op == Op::Put){
                return result->value;
            }
            else{
                return std::nullopt;
            }
        }
    }  

    // key not found
    
    return std::nullopt;
}

void DB::erase(const std::string& key) {
    // append wal with tombstone, erase from memtable

    wal_->append(Op::Delete, key, "");
    memtable_.erase(key);

    // check for flush
    if (memtable_.get_size_bytes() > opts_.memtable_bytes){
        flush();
    }
}

std::vector<std::pair<std::string, std::string>>
DB::scan(const std::string& start, const std::string& end) {

    // populate map of most recent entries
    std::map<std::string, Entry> ret_map;
    
    // search sstables oldest to newest for val
    for (const auto& table : sstables_){

        auto it = table->iterator();

        // iterate to valid keys
        while (it.valid() && it.key() < start){
            it.next();
        }
        // while keys less than end, add to map
        while (it.valid() && it.key() < end){
            ret_map[it.key()] = it.entry();
            it.next();
        }
    }

    // iterate memtable, adding keys in range to new map
    for (const auto& [key, entry] : memtable_.entries()){
        if (key < start){
            continue;
        }
        else if (key >= end){
            break;
        }

        ret_map[key] = entry;
    }

    // return vector storing key, value pairs in order
    std::vector<std::pair<std::string, std::string>> ret_vec;

    // add only Put operations
    for (const auto& [k, ent] : ret_map){
        if (ent.op == Op::Put){
            ret_vec.push_back({k, ent.value});
        }
    }

    return ret_vec;
}

void DB::close() {
    // memtable flush
    flush();
}

void DB::flush(){
    // if memtable is empty, no flushing needed, return
    if (memtable_.get_size_bytes() == 0){
        return;
    }

    // build string to new sst path
    std::string sst_str = std::to_string(next_sst_id_);
    size_t total_width = 8;

    if (sst_str.length() < total_width) {
        sst_str.insert(0, total_width - sst_str.length(), '0');
    }
    std::string new_sst_path = opts_.dir + "/" + sst_str + ".sst";

    // write memtable entries to new sstable path
    SSTableWriter::write(new_sst_path, memtable_.entries());

    // create sstable reader, add to vector
    sstables_.push_back(std::make_unique<SSTableReader>(new_sst_path));

    // create new memtable to empty it, and clear wal (calls ftruncate(fd_, 0))
    memtable_ = Memtable();
    wal_->clear();

    next_sst_id_++;

    // start sstable compaction if needed
    if (sstables_.size() >= opts_.sstable_trigger) {

        // store all sstables and paths
        std::vector<SSTableReader*> inputs;
        std::vector<std::string>    old_paths;
        for (auto& r : sstables_) { inputs.push_back(r.get()); old_paths.push_back(r->path()); }

        // get new sstable filename
        std::string new_sst_str = std::to_string(next_sst_id_);
        if (new_sst_str.length() < total_width) {
            new_sst_str.insert(0, total_width - new_sst_str.length(), '0');
        }
        std::string out = opts_.dir + "/" + new_sst_str + ".sst";

        // compact to new file, write+fsync new file
        Compaction::compact(inputs, out);
        next_sst_id_++;

        // drop old sstable readers, add new one
        sstables_.clear();
        sstables_.push_back(std::make_unique<SSTableReader>(out));

        // remove old sstable files
        for (auto& p : old_paths) std::filesystem::remove(p);
    }
}


}  // namespace lsm