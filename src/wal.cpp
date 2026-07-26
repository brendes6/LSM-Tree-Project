#include "wal.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <stdexcept>

#include "encoding.h"

namespace {

// CRC functions: we keep the crc at the end of the file to perform
// a checksum over the data in the file. These functions create crc numbers
// such that if data in the WAL is modified via corruption, the crc's will
// not match and we will know the WAL is invalid.
std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}
std::uint32_t crc32(const void* data, std::size_t len) {
    static const std::array<std::uint32_t, 256> table = make_crc_table();
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(const std::string& s) {
    return crc32(s.data(), s.size());
}

}  // namespace

// RAII constructors and destructors to initialize file descriptor and boolean fsync toggle
Wal::Wal(const std::string& path, bool sync_on_write)
    : fd_(-1), sync_on_write_(sync_on_write) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) throw std::runtime_error("Wal: could not open " + path);
}

Wal::~Wal() {
    if (fd_ >= 0) ::close(fd_);
}


// append(): write the operation, key, and value to WAL in structured byte
// order. Uses put_uXX helpers to write info as stuctured bytes, writing data
// and possibly fsync-ing
void Wal::append(Op op, const std::string& key, const std::string& value) {

    std::string rec;

    // append bytes
    rec.push_back(static_cast<char>(op));
    put_u32(rec, key.size());
    rec.append(key);
    put_u32(rec, value.size());
    rec.append(value);

    // add crc
    std::uint32_t crc = crc32(rec);
    put_u32(rec, crc);

    // write and fsync if toggled
    ::write(fd_, rec.data(), rec.size());
    if (sync_on_write_){
        ::fsync(fd_);
    }
}


// replay(): load in the WAL and iterate it, validating entries and passing
// them back into the apply function to add back to memtable
void Wal::replay(const std::function<void(Op, const std::string&,
                                              const std::string&)>& apply) {

    if (::lseek(fd_, 0, SEEK_SET) == -1){
        return;
    }

    // read all WAL data to string buffer
    std::string buf;
    char tmp[4096];
    ssize_t n;
    while ((n = ::read(fd_, tmp, sizeof tmp)) > 0){
        buf.append(tmp, static_cast<std::size_t>(n));
    }

    // iterate values in string
    std::size_t pos = 0;
    std::size_t buf_size = buf.size();

    while (pos < buf_size){

        size_t record_start = pos;

        // check space for op + key_len
        if ((pos+5) > buf_size){
            break;
        }

        // extract op and key_len
        Op op = static_cast<Op>(static_cast<unsigned char>(buf[pos]));
        pos += 1;
        std::uint32_t key_len =
            read_u32(reinterpret_cast<const unsigned char*>(buf.data() + pos));
        pos += 4;

        if ((pos + key_len + 4) > buf_size){
            break;
        }

        // extract key, val_len
        std::string key = buf.substr(pos, key_len);
        pos += key_len;
        std::uint32_t val_len =
            read_u32(reinterpret_cast<const unsigned char*>(buf.data() + pos));
        pos += 4;

        if ((pos + val_len + 4) > buf_size){
            break;
        }

        // extract val and CRC
        std::string val = buf.substr(pos, val_len);
        pos += val_len;
        std::uint32_t ending_crc =
            read_u32(reinterpret_cast<const unsigned char*>(buf.data() + pos));
        std::uint32_t crc = crc32(buf.substr(record_start, pos-record_start));

        // if CRCs dont match, data is corrupted - break
        if (ending_crc != crc){
            break;
        }

        // send back to memtable
        apply(op, key, val);

        pos += 4;
    }
}
