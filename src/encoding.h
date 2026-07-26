#ifndef ENCODING_H
#define ENCODING_H

#include <cstdint>
#include <string>

// Little-endian fixed-width integer codec, shared by the WAL and SSTable
// on-disk formats. Explicit byte order means the files are portable across
// machines

inline void put_u32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>( v        & 0xFF));
    out.push_back(static_cast<char>((v >> 8)  & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

inline std::uint32_t read_u32(const unsigned char* p) {
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void put_u64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

inline std::uint64_t read_u64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

#endif  // ENCODING_H
