#ifndef BLOOM_H
#define BLOOM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


// bloom filter: the bloom filter contains a bit array and hashing functions
// to give the DB either a guarenteed "no" if a key is in an sstable, or a "maybe"
// , never a false negative. the rate of false positives is minimized and slightly
// impacts performance but doesn't invalidate reads
class Bloom {
  public:
    // constructor for size n keys and target FPR p 
    Bloom(std::size_t n, double p);

    // rebuild a filter from its stored parameters + bits (for SSTableReader).
    Bloom(std::uint32_t k, std::uint64_t num_bits, std::vector<std::uint8_t> bits);

    void insert(const std::string& key);
    bool might_contain(const std::string& key) const;

    // SSTable Bloom block layout: [k u32][num_bits u64][bit bytes ...]
    std::string serialize() const;
    static Bloom deserialize(const unsigned char* data, std::size_t len);

  private:
    std::uint32_t             k_ = 1;   // number of hash functions
    std::uint64_t             m_ = 8;   // number of bits
    std::vector<std::uint8_t> bits_;    // ceil(m_/8) bytes, 8 bits each
};

#endif  // BLOOM_H
