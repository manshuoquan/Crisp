// MT3 tests: CtParallel map/filter/reduce.
//
// Acceptance (docs/threading/03 §4 MT3): results identical to a sequential
// pass; chunk correctness; deterministic order; exception propagation.
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <forward_list>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "thread/ct_parallel.h"
#include "thread/ct_thread_pool.h"
#include "test_harness.h"

using crisp::thread::CtParallel;
using crisp::thread::CtThreadPool;

TEST_CASE(parallel_map_matches_sequential) {
    CtThreadPool pool;
    pool.start(2);

    std::vector<int> in;
    for (int i = 0; i < 1000; ++i)
        in.push_back(i);

    auto out = CtParallel::map(pool, in.begin(), in.end(),
                               [](int x) { return x * x; });

    EXPECT_EQ(out.size(), 1000u);
    bool ok = true;
    for (std::size_t i = 0; i < out.size(); ++i)
        ok = ok && out[i] == static_cast<int>(i * i);
    EXPECT_TRUE(ok);

    pool.shutdown();
}

TEST_CASE(parallel_map_small_input_serial) {
    // <= kDefaultChunk: runs serially (no pool task overhead).
    CtThreadPool pool;
    pool.start(1);

    std::vector<int> in{1, 2, 3};
    auto out = CtParallel::map(pool, in.begin(), in.end(),
                               [](int x) { return x + 1; });
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], 2);
    EXPECT_EQ(out[1], 3);
    EXPECT_EQ(out[2], 4);

    pool.shutdown();
}

TEST_CASE(parallel_map_empty_input) {
    CtThreadPool pool;
    pool.start(1);
    std::vector<int> in;
    auto out = CtParallel::map(pool, in.begin(), in.end(),
                               [](int x) { return x; });
    EXPECT_TRUE(out.empty());
    pool.shutdown();
}

TEST_CASE(parallel_map_forward_iterator) {
    // Forward (single-pass) iterators are supported (materialized once).
    CtThreadPool pool;
    pool.start(2);

    std::forward_list<int> fl;
    for (int i = 0; i < 600; ++i)
        fl.push_front(i); // 0..599 in reverse

    auto out = CtParallel::map(pool, fl.begin(), fl.end(),
                               [](int x) { return x * 2; });
    // forward_list materialization preserves iteration order: reverse order.
    EXPECT_EQ(out.size(), 600u);
    EXPECT_EQ(out[0], 599 * 2); // first iterated element is 599

    pool.shutdown();
}

TEST_CASE(parallel_map_propagates_exception) {
    CtThreadPool pool;
    pool.start(2);

    // Deterministic trigger (audit WARN fix): throw when mapping the element
    // with value 350 — every element is unique so exactly one chunk hits it.
    std::vector<int> in;
    for (int i = 0; i < 700; ++i)
        in.push_back(i);

    bool caught = false;
    try {
        CtParallel::map(pool, in.begin(), in.end(), [](int x) -> int {
            if (x == 350)
                throw std::runtime_error("boom");
            return x;
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    EXPECT_TRUE(caught);

    pool.shutdown();
}

TEST_CASE(parallel_filter_matches_sequential) {
    CtThreadPool pool;
    pool.start(2);

    std::vector<int> in;
    for (int i = 0; i < 1000; ++i)
        in.push_back(i);

    auto out = CtParallel::filter(pool, in.begin(), in.end(),
                                  [](int x) { return x % 2 == 0; });

    // Evens in order: 0, 2, 4, ...
    EXPECT_EQ(out.size(), 500u);
    bool ok = true;
    for (std::size_t i = 0; i < out.size(); ++i)
        ok = ok && out[i] == static_cast<int>(i * 2);
    EXPECT_TRUE(ok);

    pool.shutdown();
}

TEST_CASE(parallel_reduce_matches_sequential) {
    CtThreadPool pool;
    pool.start(2);

    std::vector<int> in;
    for (int i = 0; i < 1000; ++i)
        in.push_back(i);

    const int sum = CtParallel::reduce(
        pool, in.begin(), in.end(), 0,
        [](int a, int b) { return a + b; });

    const int expected = std::accumulate(in.begin(), in.end(), 0);
    EXPECT_EQ(sum, expected);

    pool.shutdown();
}

TEST_CASE(parallel_reduce_empty_and_small) {
    CtThreadPool pool;
    pool.start(1);

    std::vector<int> empty;
    EXPECT_EQ(CtParallel::reduce(pool, empty.begin(), empty.end(), 42,
                                 [](int a, int b) { return a + b; }),
              42);

    std::vector<int> small{10, 20, 30};
    EXPECT_EQ(CtParallel::reduce(pool, small.begin(), small.end(), 0,
                                 [](int a, int b) { return a + b; }),
              60);

    pool.shutdown();
}

TEST_CASE(parallel_reduce_non_commutative_preserves_order) {
    // String concatenation is non-commutative: chunked reduce must still
    // produce the same string as a left fold (chunk order preserved).
    CtThreadPool pool;
    pool.start(2);

    std::vector<std::string> parts;
    for (int i = 0; i < 600; ++i)
        parts.push_back(std::to_string(i) + ",");

    const std::string joined = CtParallel::reduce(
        pool, parts.begin(), parts.end(), std::string{},
        [](std::string a, const std::string& b) {
            a += b;
            return a;
        });

    std::string expected;
    for (const auto& p : parts)
        expected += p;
    EXPECT_EQ(joined, expected);

    pool.shutdown();
}

TEST_CASE(parallel_reduce_subtraction_is_deterministic) {
    // Non-associative op (subtraction): the chunked result is DETERMINISTIC
    // (chunk-index fold order) but NOT the exact sequential left fold — the
    // documented contract. This test pins the deterministic value.
    CtThreadPool pool;
    pool.start(2);

    std::vector<int> in;
    for (int i = 1; i <= 600; ++i)
        in.push_back(i); // 1..600

    const int chunked = CtParallel::reduce(
        pool, in.begin(), in.end(), 0,
        [](int a, int b) { return a - b; });

    // Expected: fold chunks of 256 elements, partials combined in order.
    // chunk0 = 1-2-...-256; chunk1 = 257-...-512; chunk2 = 513-...-600.
    // result = 0 - chunk0 - chunk1 - chunk2.
    auto chunkFold = [](int begin, int end) {
        int acc = begin;
        for (int i = begin + 1; i <= end; ++i)
            acc -= i;
        return acc;
    };
    const int expected = 0 - chunkFold(1, 256) - chunkFold(257, 512) - chunkFold(513, 600);
    EXPECT_EQ(chunked, expected);

    pool.shutdown();
}

TEST_CASE(parallel_reduce_non_default_constructible_T) {
    // Audit SUGGEST-1 regression: reduce must work with a NON-default-
    // constructible accumulator (the parked slot uses optional<T>).
    struct Acc {
        int sum = 0;
        Acc(int v) : sum(v) {}               // implicit: T acc = in[begin]
        Acc(const Acc&) = default;
        Acc(Acc&&) = default;
        Acc& operator=(Acc&&) = default;
    };
    CtThreadPool pool;
    pool.start(2);
    std::vector<int> in;
    for (int i = 1; i <= 600; ++i)
        in.push_back(i);
    const Acc result = CtParallel::reduce(
        pool, in.begin(), in.end(), Acc(0),
        [](Acc a, Acc b) {
            a.sum += b.sum;
            return a;
        });
    EXPECT_EQ(result.sum, 600 * 601 / 2);
    pool.shutdown();
}

TEST_CASE(parallel_pool_rejection_falls_back_serial) {
    // Audit MUST coverage: when the pool rejects ALL chunks (not started),
    // the serial fallback must still produce the correct in-order output.
    CtThreadPool pool; // never started: submit always rejects

    std::vector<int> in;
    for (int i = 0; i < 700; ++i) // > kDefaultChunk => would be chunked
        in.push_back(i);

    auto out = CtParallel::map(pool, in.begin(), in.end(),
                               [](int x) { return x + 1; });
    EXPECT_EQ(out.size(), 700u);
    bool ok = true;
    for (std::size_t i = 0; i < out.size(); ++i)
        ok = ok && out[i] == static_cast<int>(i) + 1;
    EXPECT_TRUE(ok);

    auto kept = CtParallel::filter(pool, in.begin(), in.end(),
                                   [](int x) { return x % 3 == 0; });
    EXPECT_EQ(kept.size(), 234u); // 0..699, multiples of 3

    const int sum = CtParallel::reduce(pool, in.begin(), in.end(), 0,
                                       [](int a, int b) { return a + b; });
    EXPECT_EQ(sum, 700 * 699 / 2);
}

TEST_CASE(parallel_mixed_pool_rejection_preserves_order) {
    // Audit MUST regression: when the pool rejects SOME chunks (queue full
    // past kMaxQueued) the output must still be in chunk index order.
    // Pre-fill the queue to force rejection: start(1), occupy the worker
    // with a slow task, then submit 8192+ filler tasks that stay queued,
    // then run a small map — its chunks must be REJECTED and serially
    // parked in chunk order.
    CtThreadPool pool;
    pool.start(1);

    // Occupy the worker for 200ms (enough to fill the queue below).
    auto barrier = pool.submit<int>([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    });
    while (barrier.phase() != crisp::thread::CtTaskPhase::kRunning &&
           !barrier.isDone()) { std::this_thread::yield(); }

    // Fill the queue to capacity (8192). Keep handles alive so tasks stay
    // queued (dropping them would cancel+unlink).
    std::vector<crisp::thread::CtTaskHandle<int>> fillers;
    fillers.reserve(8200);
    for (int i = 0; i < 8200; ++i) {
        auto h = pool.submit<int>([i] { return i; });
        if (!h.valid())
            break; // queue full
        fillers.push_back(std::move(h));
    }
    // At least ~8192 fillers queued => a following map's chunks are rejected.

    // Small map (chunked input): ALL its chunks are rejected -> parked
    // serially in chunk order; output must be exact.
    std::vector<int> in;
    for (int i = 0; i < 700; ++i)
        in.push_back(i);
    auto out = CtParallel::map(pool, in.begin(), in.end(),
                               [](int x) { return x * 2; });
    EXPECT_EQ(out.size(), 700u);
    bool ok = true;
    for (int i = 0; i < 700; ++i)
        ok = ok && out[static_cast<std::size_t>(i)] == i * 2;
    EXPECT_TRUE(ok);

    barrier.waitForDone(5000);
    pool.shutdown();
}
