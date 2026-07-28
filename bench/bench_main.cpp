#include "lsm/db.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

using lsm::DB;
using lsm::Options;

namespace {

std::string bench_dir(const char* tag) {
    auto p = std::filesystem::temp_directory_path() / (std::string("lsm_bm_") + tag);
    std::filesystem::remove_all(p);
    return p.string();
}

std::string make_key(std::uint64_t i) {
    char b[24];
    std::snprintf(b, sizeof b, "key%016llu", static_cast<unsigned long long>(i));
    return b;
}

constexpr std::size_t kValueSize = 100;

}  // namespace


// benchmark sequential key writes 
// fsync is off to isolate LSM structure cost
static void BM_SequentialWrite(benchmark::State& state) {
    Options opts;
    opts.dir = bench_dir("seq");
    opts.sync_on_write = state.range(0) != 0;
    DB db = DB::open(opts);

    const std::string value(kValueSize, 'x');
    std::uint64_t i = 0;
    for (auto _ : state) db.put(make_key(i++), value);
    db.close();

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (make_key(0).size() + kValueSize));
    std::filesystem::remove_all(opts.dir);
}
BENCHMARK(BM_SequentialWrite)->Arg(0)->Arg(1);

// benchmark random key writes (no fsync)
static void BM_RandomWrite(benchmark::State& state) {
    Options opts;
    opts.dir = bench_dir("rand");
    opts.sync_on_write = false;
    DB db = DB::open(opts);

    std::mt19937_64 rng(1234);
    const std::string value(kValueSize, 'x');
    for (auto _ : state) db.put(make_key(rng()), value);
    db.close();

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (make_key(0).size() + kValueSize));
    std::filesystem::remove_all(opts.dir);
}
BENCHMARK(BM_RandomWrite);

// benchmark point reads after lots are loaded in
static void BM_PointRead(benchmark::State& state) {
    Options opts;
    opts.dir = bench_dir("read");
    opts.sync_on_write = false;
    DB db = DB::open(opts);

    const std::uint64_t N = 200000;
    const std::string value(kValueSize, 'x');
    for (std::uint64_t i = 0; i < N; ++i) db.put(make_key(i), value);

    std::mt19937_64 rng(9);
    for (auto _ : state) {
        auto v = db.get(make_key(rng() % N));
        benchmark::DoNotOptimize(v);
    }
    db.close();
    state.SetItemsProcessed(state.iterations());
    std::filesystem::remove_all(opts.dir);
}
BENCHMARK(BM_PointRead);

// benchmark range scan across multiple sizes
static void BM_Scan(benchmark::State& state) {
    Options opts;
    opts.dir = bench_dir("scan");
    opts.sync_on_write = false;
    DB db = DB::open(opts);

    const std::uint64_t N = 200000;
    const std::string value(kValueSize, 'x');
    for (std::uint64_t i = 0; i < N; ++i) db.put(make_key(i), value);

    const std::uint64_t span = static_cast<std::uint64_t>(state.range(0));
    std::mt19937_64 rng(3);
    for (auto _ : state) {
        std::uint64_t s = rng() % (N - span);
        auto rows = db.scan(make_key(s), make_key(s + span));
        benchmark::DoNotOptimize(rows);
    }
    db.close();
    state.SetItemsProcessed(state.iterations() * span);
    std::filesystem::remove_all(opts.dir);
}
BENCHMARK(BM_Scan)->Arg(10)->Arg(100)->Arg(1000);

BENCHMARK_MAIN();
