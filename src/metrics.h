#ifndef METRICS_H
#define METRICS_H

#include <atomic>
#include <cstdint>

// lightweight metrics tools for the benchmarks. these count total bytes
// actually written to disk so that we can compute the write amplification.
namespace metrics {

inline std::atomic<std::uint64_t> bytes_written{0};

inline void add_bytes_written(std::uint64_t n) {
    bytes_written.fetch_add(n, std::memory_order_relaxed);
}
inline std::uint64_t disk_bytes_written() {
    return bytes_written.load(std::memory_order_relaxed);
}
inline void reset_bytes_written() {
    bytes_written.store(0, std::memory_order_relaxed);
}

}  // namespace metrics

#endif  // METRICS_H
