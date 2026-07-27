#include "sstable.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>


#include <stdexcept>

#include "encoding.h"


// write(): given the file path and memtable, write all entries in specific 
// byte format to be read later
void SSTableWriter::write(const std::string& path, const std::map<std::string, Entry>& entries) {

    // data: stores key length, key, val len, val, and op
    std::string data;
    // idx: stores key len, key, and key offset into data
    std::string idx;
    // footer: stores index offset into sstable, num entries and magic num
    std::string footer;

    // initialize bloom filter w tunable fp rate
    Bloom bloom(entries.size(), 0.01);

    std::size_t num_entries = 0;
    std::size_t offset = 0;

    for (const auto& [key, entry] : entries){

        // add key to bloom filter
        bloom.insert(key);

        // place key/val sizes+data on data
        put_u32(data, key.size());
        data.append(key);
        put_u32(data, entry.value.size());
        data.append(entry.value);
        data.push_back(static_cast<char>(entry.op));
        
        // every 16 entries, append to index
        if ((num_entries % sstable::kIndexEvery) == 0){
            put_u32(idx, key.size());
            idx.append(key);
            put_u64(idx, offset);
        }

        // update data block's offset and num entries
        offset += (key.size() + entry.value.size() + 4 + 4 + 1);
        num_entries++;
    }

    // offset of index start
    put_u64(footer, data.size());
    // num entries
    put_u64(footer, entries.size());
    // bloom offset
    put_u64(footer, data.size() + idx.size());
    // magic num
    put_u32(footer, sstable::kMagic);


    int fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0){
        throw std::runtime_error("SSTable: failed to open file");
    }

    // get string from bloom filter
    std::string bloom_block = bloom.serialize();

    // write all combined data, fsync and close
    std::string all_combined = data + idx + bloom_block + footer;
    ::write(fd_, all_combined.data(), all_combined.size());
    ::fsync(fd_);
    ::close(fd_);

}

// reader constructor: create file descriptor and populate index vector
SSTableReader::SSTableReader(const std::string& path)
    : fd_(-1), data_end_(0) {

    fd_ = ::open(path.c_str(), O_RDONLY);
    
    // get file size
    struct stat st;
    if (::fstat(fd_, &st) != 0){
        throw std::runtime_error("SSTable: fstat failed");
    }

    off_t file_size = st.st_size;

    // read in footer, extract index offset and magic num
    std::string buf(sstable::kFooterSize, '\0');
    ::pread(fd_, buf.data(), buf.size(), file_size-sstable::kFooterSize);

    uint64_t index_offset = read_u64(reinterpret_cast<const unsigned char*>(buf.data() + 0));
    uint64_t _ = read_u64(reinterpret_cast<const unsigned char*>(buf.data() + 8));
    uint64_t bloom_offset = read_u64(reinterpret_cast<const unsigned char*>(buf.data() + 16));
    uint32_t magic = read_u32(reinterpret_cast<const unsigned char*>(buf.data() + 24));

    //verify magic num
    if (magic != sstable::kMagic){
        throw std::runtime_error("SSTable: magic numbers don't match");
    }

    data_end_ = index_offset;

    // read in index table
    std::string idx_buf(bloom_offset - index_offset, '\0');
    ::pread(fd_, idx_buf.data(), idx_buf.size(), index_offset);

    // read in bloom table
    std::string bloom_buf(file_size - bloom_offset - sstable::kFooterSize, '\0');
    ::pread(fd_, bloom_buf.data(), bloom_buf.size(), bloom_offset);

    bloom_ = std::make_unique<Bloom>(
        Bloom::deserialize(reinterpret_cast<const unsigned char*>(bloom_buf.data()), bloom_buf.size())
    );

    //parse string into (key, index) pairs and add to vector
    std::size_t i = 0;
    while (i < idx_buf.size()){
        uint32_t key_l = read_u32(reinterpret_cast<const unsigned char*>(idx_buf.data() + i));

        if (i + 4 + key_l >= idx_buf.size()){
            break;
        }

        std::string key = idx_buf.substr(i+4, key_l);
        uint64_t off = read_u64(reinterpret_cast<const unsigned char*>(idx_buf.data() + i + 4 + key_l ));

        index_.emplace_back(key, off);

        i += key_l + 12;
    }


}

// destructor
SSTableReader::~SSTableReader() {
    if (fd_ >= 0) ::close(fd_);
}

// reader::get(): given a key, find the greatest index with key <= key passed in
// to determine where to start search in data. Iterate data, checking keys until
// we find it or reach a key with val > our search, if so return nullopt
std::optional<Entry> SSTableReader::get(const std::string& key) {
    // first check bloom filter - if bloom filter says no, return nullopt
    if (bloom_ && !bloom_->might_contain(key)) return std::nullopt;

    // binary search to find greatest index with key <= ours
    int block = -1, l = 0, r = (int)index_.size() - 1;
    while (l <= r) {

        int m = (l + r) / 2;

        if (index_[m].first <= key){
            block = m;
            l = m + 1;
        }
        else {
            r = m - 1;
        }
    }
    if (block == -1) return std::nullopt;


    // create data string storing data from start index to end of sstable entries
    uint64_t start = index_[block].second;
    std::string data(data_end_ - start, '\0');
    ::pread(fd_, data.data(), data.size(), start);

    // search this data string
    std::size_t pos = 0;
    std::size_t data_size = data.size();

    while (pos < data_size){

        // extract key length
        std::uint32_t key_len =
            read_u32(reinterpret_cast<const unsigned char*>(data.data() + pos));
        pos += 4;

        if ((pos + key_len + 4) > data_size){
            break;
        }

        // extract key, val length
        std::string parsed_key = data.substr(pos, key_len);
        pos += key_len;
        std::uint32_t val_len =
            read_u32(reinterpret_cast<const unsigned char*>(data.data() + pos));
        pos += 4;

        if ((pos + val_len + 1) > data_size){
            break;
        }

        // extract val length, operator
        std::string parsed_val = data.substr(pos, val_len);
        pos += val_len;
        Op op = static_cast<Op>(static_cast<unsigned char>(data[pos]));
        pos += 1;

        // if keys match return. if > then key doesn't exist
        if (parsed_key == key){
            return Entry{op, parsed_val};
        }
        if (parsed_key > key){
            return std::nullopt;
        }
    }


    return std::nullopt;
}
