// captures important stats from the benchmarks:
// point-read latency p50/p95/p99, write amplification, and space amplification.
#include "lsm/db.h"
#include "metrics.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using lsm::DB;
using lsm::Options;
using Clock = std::chrono::steady_clock;

namespace {

std::string make_key(std::uint64_t i) {
    char b[24];
    std::snprintf(b, sizeof b, "key%016llu", static_cast<unsigned long long>(i));
    return b;
}

std::uint64_t dir_size(const std::string& dir) {
    std::uint64_t total = 0;
    for (auto& e : std::filesystem::recursive_directory_iterator(dir))
        if (e.is_regular_file()) total += e.file_size();
    return total;
}

}  // namespace

int main() {
    const std::uint64_t N = 200000;    // currently set to 200k, bumping to 1m eventual goal
    const std::size_t   VSZ = 100;
    const std::string   value(VSZ, 'x');
    const std::size_t   key_sz = make_key(0).size();
    const std::string   dir =
        (std::filesystem::temp_directory_path() / "lsm_stats").string();

    std::filesystem::remove_all(dir);
    Options opts;
    opts.dir = dir;
    opts.sync_on_write = false;   // measure the structural cost, not fsync

    metrics::reset_bytes_written();
    DB db = DB::open(opts);

    // test sequential write throughput
    auto t0 = Clock::now();
    for (std::uint64_t i = 0; i < N; ++i) db.put(make_key(i), value);
    auto t1 = Clock::now();
    const double wsec = std::chrono::duration<double>(t1 - t0).count();
    const double wops = N / wsec;

    const std::uint64_t client_bytes = N * (key_sz + VSZ);
    const std::uint64_t disk_bytes = metrics::disk_bytes_written();
    const double wa = static_cast<double>(disk_bytes) / client_bytes;

    db.close();  // flush everything so on-disk size is stable
    const std::uint64_t logical = N * (key_sz + VSZ);
    const std::uint64_t on_disk = dir_size(dir);
    const double space_amp = static_cast<double>(on_disk) / logical;

    // capture point-read percentiles
    DB db2 = DB::open(opts);
    const std::uint64_t R = 50000;
    std::vector<double> lat_us;
    lat_us.reserve(R);
    std::mt19937_64 rng(42);
    for (std::uint64_t i = 0; i < R; ++i) {
        const std::uint64_t k = rng() % N;
        const auto a = Clock::now();
        auto v = db2.get(make_key(k));
        const auto b = Clock::now();
        (void)v;
        lat_us.push_back(std::chrono::duration<double, std::micro>(b - a).count());
    }
    std::sort(lat_us.begin(), lat_us.end());
    auto pct = [&](double p) { return lat_us[static_cast<std::size_t>(p * (R - 1))]; };
    db2.close();

    // output all stats
    std::printf("LSM-Tree storage engine benchmark stats: \n\n");
    std::printf("workload: %llu writes, %zuB values, sync_on_write=off\n\n",
                static_cast<unsigned long long>(N), VSZ);
    std::printf("sequential write : %10.0f ops/sec   (%.1f MB/sec)\n",
                wops, wops * (key_sz + VSZ) / 1e6);
    std::printf("point read  p50  : %10.2f us\n", pct(0.50));
    std::printf("point read  p95  : %10.2f us\n", pct(0.95));
    std::printf("point read  p99  : %10.2f us\n", pct(0.99));
    std::printf("write amplification : %.2fx   (%llu disk / %llu client bytes)\n",
                wa, static_cast<unsigned long long>(disk_bytes),
                static_cast<unsigned long long>(client_bytes));
    std::printf("space amplification : %.2fx   (%llu on-disk / %llu logical bytes)\n",
                space_amp, static_cast<unsigned long long>(on_disk),
                static_cast<unsigned long long>(logical));

    std::filesystem::remove_all(dir);
    return 0;
}
