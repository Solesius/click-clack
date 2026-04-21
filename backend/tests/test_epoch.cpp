// ──────────────────────────────────────────────────────────────
// test_epoch.cpp — Atomic epoch counter
// epoch allocator tests — monotonicity under concurrency
// ──────────────────────────────────────────────────────────────

#include <click_clack/core/epoch.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>

namespace cc::test {

TEST(Epoch, StartsAtZero) {
    EpochCounter ep;
    EXPECT_EQ(ep.current(), 0u);
}

TEST(Epoch, Monotonic) {
    EpochCounter ep;
    auto a = ep.next();
    auto b = ep.next();
    auto c = ep.next();
    EXPECT_EQ(a, 1u);
    EXPECT_EQ(b, 2u);
    EXPECT_EQ(c, 3u);
}

TEST(Epoch, Recover) {
    EpochCounter ep;
    ep.recover(99);
    EXPECT_EQ(ep.current(), 99u);
    EXPECT_EQ(ep.next(), 100u);
}

TEST(Epoch, ConcurrentMonotonic) {
    EpochCounter ep;
    constexpr int N = 8;
    constexpr int PER_THREAD = 1000;
    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> results(N);

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&ep, &out = results[i]]() {
            out.reserve(PER_THREAD);
            for (int j = 0; j < PER_THREAD; ++j) {
                out.push_back(ep.next());
            }
        });
    }
    for (auto& t : threads) t.join();

    // All values unique and in [1, N * PER_THREAD]
    std::set<uint64_t> all;
    for (auto& v : results) all.insert(v.begin(), v.end());
    EXPECT_EQ(all.size(), N * PER_THREAD);
    EXPECT_EQ(*all.begin(), 1u);
    EXPECT_EQ(*all.rbegin(), static_cast<uint64_t>(N * PER_THREAD));
}

} // namespace cc::test
