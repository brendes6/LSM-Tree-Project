#include "bloom.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <iostream>
#include <string>


// test for ZERO false negatives. insert 10k keys, assert might
// containe never returns false.
TEST(Bloom, ZeroFalseNegatives) {
    const std::size_t n = 10000;
    Bloom b(n, 0.01);
    for (std::size_t i = 0; i < n; ++i) b.insert("key" + std::to_string(i));
    for (std::size_t i = 0; i < n; ++i)
        EXPECT_TRUE(b.might_contain("key" + std::to_string(i)))
            << "false negative on key" << i;
}

// tunable test: query a large DISJOINT set of absent keys and count how often
// the filter says "maybe" anyway. Prints the real rate for reference
TEST(Bloom, MeasuredFalsePositiveRateUnderTarget) {
    const std::size_t n = 10000;
    const double target = 0.01;
    Bloom b(n, target);
    for (std::size_t i = 0; i < n; ++i) b.insert("key" + std::to_string(i));

    const std::size_t trials = 100000;
    std::size_t fp = 0;
    for (std::size_t i = 0; i < trials; ++i)
        if (b.might_contain("absent" + std::to_string(i))) ++fp;  // never inserted

    const double rate = static_cast<double>(fp) / trials;
    std::cout << "[bloom] measured false-positive rate = " << (rate * 100.0)
              << "%  (target " << (target * 100.0) << "%)\n";
    EXPECT_LT(rate, 0.02);  // target 1%, generous ceiling for variance
}

// the filter must survive a trip through disk (SSTable bloom block).
TEST(Bloom, SerializeRoundTrips) {
    Bloom b(1000, 0.01);
    for (int i = 0; i < 1000; ++i) b.insert("k" + std::to_string(i));

    const std::string blob = b.serialize();
    Bloom b2 = Bloom::deserialize(
        reinterpret_cast<const unsigned char*>(blob.data()), blob.size());

    for (int i = 0; i < 1000; ++i)
        EXPECT_TRUE(b2.might_contain("k" + std::to_string(i)));  // members survive
    EXPECT_EQ(b.might_contain("definitely_absent_zzz"),
              b2.might_contain("definitely_absent_zzz"));         // same verdict
}
