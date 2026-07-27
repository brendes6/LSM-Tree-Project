#include "bloom.h"

#include <cmath>
#include <utility>

#include "encoding.h"  // put_u32 / read_u32 / put_u64 / read_u64

namespace {

// FNV-1a 64-bit hash function. a decent hash function good enough for our bloom filter
std::uint64_t fnv1a(const std::string& key) {
    std::uint64_t h = 1469598103934665603ULL;   // FNV offset basis
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;                   // FNV prime
    }
    return h;
}

}  // namespace


// constructor to determine m and k from:
// n - expected key count
// p - target false-positive rate
Bloom::Bloom(std::size_t n, double p) {
    if (n == 0) n = 1;           // avoid divide-by-zero
    double bits = -(double)n * std::log(p) / (std::log(2) * std::log(2));
    m_ = (std::uint64_t)std::ceil(bits);
    if (m_ < 8) m_ = 8;          // never a zero-size array
    double kf = (double)m_ / n * std::log(2);
    k_ = (std::uint32_t)std::llround(kf);
    if (k_ < 1) k_ = 1;          // at least one hash
    bits_.assign((m_ + 7) / 8, 0);
}

Bloom::Bloom(std::uint32_t k, std::uint64_t num_bits, std::vector<std::uint8_t> bits)
    : k_(k), m_(num_bits), bits_(std::move(bits)) {}

// insert key into bit table using hashing
void Bloom::insert(const std::string& key) {
    // get hash, split into high and low half
    std::uint64_t h  = fnv1a(key);
    std::uint32_t h1 = (std::uint32_t)(h >> 32);   // high half
    std::uint32_t h2 = (std::uint32_t)h;           // low half
    h2 |= 1;                                        // keep it odd so hashes don't collapse

    // for each bit we need, derive from original hash
    for (std::uint32_t i = 0; i < k_; ++i) {
        std::uint64_t bit = ((std::uint64_t)h1 + (std::uint64_t)i * h2) % m_;
        bits_[bit / 8] |= (std::uint8_t)(1u << (bit % 8));
    }
    
}

// might_contain: do the same thing as insert, but the moment
// we find a bit not set, return false
bool Bloom::might_contain(const std::string& key) const {
    std::uint64_t h  = fnv1a(key);
    std::uint32_t h1 = (std::uint32_t)(h >> 32);   // high half
    std::uint32_t h2 = (std::uint32_t)h;           // low half
    h2 |= 1;                                        // keep it odd so hashes don't collapse

    // for each bit we need, derive from original hash
    for (std::uint32_t i = 0; i < k_; ++i) {
        std::uint64_t bit = ((std::uint64_t)h1 + (std::uint64_t)i * h2) % m_;
        if (!((bits_[bit / 8] >> (bit % 8)) & 1)) return false;   // any bit off means definitely not
    }
    
    return true;
}

// serialize: output the bloom filter in format:
// [k u32][m u64][string of bits]
std::string Bloom::serialize() const {

    std::string out;
    put_u32(out, k_);
    put_u64(out, m_);
    out.append(reinterpret_cast<const char*>(bits_.data()), bits_.size());
    return out;
}

// deserialize: given bloom filter, serialize into k, m, 
// and bits of data and return new object
Bloom Bloom::deserialize(const unsigned char* data, std::size_t len) {

    std::uint32_t k = read_u32(data);
    std::uint64_t m = read_u64(data + 4);
    std::vector<std::uint8_t> bits(data + 12, data + len);
    return Bloom(k, m, std::move(bits));
}
