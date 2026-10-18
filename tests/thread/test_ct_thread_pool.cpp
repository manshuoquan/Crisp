// MT1 thread pool tests: submit / priority / cancellation / shutdown /
// zero-allocation / task trace / thread affinity.
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#include "thread/ct_thread_pool.h"
#include "test_harness.h"

using crisp::thread::CtThreadPool;
using crisp::thread::CtTaskHandle;
using crisp::thread::CtTaskPhase;
using crisp::thread::CtTaskRecord;

namespace {

/// Real global operator new/delete counters (audit K2 fix: the previous
/// version declared plain functions and never installed them, so the zero-
/// allocation assertion was vacuous).
std::atomic<std::size_t> g_allocCount{0};
std::atomic<std::size_t> g_deallocCount{0};

} // namespace

// Install GLOBAL counting operators (also the nothrow variants, so no
// allocation path escapes the counter).
void* operator new(std::size_t n) {
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n);
}
void operator delete(void* p) noexcept {
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p) noexcept {
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}

TEST_CASE(pool_runs_submitted_task_and_returns_value) {
    CtThreadPool pool;
    pool.start(2);

    auto handle = pool.submit<int>([] { return 42; });
    EXPECT_TRUE(handle.valid());
    EXPECT_TRUE(handle.waitForDone(5000));
    EXPECT_TRUE(std::holds_alternative<int>(handle.result()));
    EXPECT_EQ(std::get<int>(handle.result()), 42);

    pool.shutdown();
}

TEST_CASE(pool_priority_runs_high_first) {
    CtThreadPool pool;
    pool.start(1); // single worker

    // Pin the worker with a barrier so it CANNOT race the submits below
    // (audit K3 fix #2: barrier + release, fully deterministic).
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    while (barrier.phase() != CtTaskPhase::kRunning) { std::this_thread::yield(); }

    std::atomic<int> order{0};
    std::atomic<int> first{0}, second{0};
    auto low = pool.submit<int>([&] {
        first.store(order.fetch_add(1));
        return 1;
    }, /*priority=*/0);
    auto high = pool.submit<int>([&] {
        second.store(order.fetch_add(1));
        return 2;
    }, /*priority=*/100);
    EXPECT_TRUE(low.valid());
    EXPECT_TRUE(high.valid());

    release.store(true);
    EXPECT_TRUE(barrier.waitForDone(5000));
    EXPECT_TRUE(low.waitForDone(5000));
    EXPECT_TRUE(high.waitForDone(5000));
    // High priority submitted second must run first (order 0).
    EXPECT_EQ(first.load(), 1); // low ran second
    EXPECT_EQ(second.load(), 0); // high ran first

    pool.shutdown();
}

TEST_CASE(pool_handle_destruction_cancels_queued_task) {
    CtThreadPool pool;
    pool.start(1);

    // Make the single worker BUSY so the submitted task stays queued
    // deterministically (audit K3 fix): a barrier task that the worker
    // blocks on until we drop the second handle.
    std::atomic<bool> releaseBarrier{false};
    auto barrier = pool.submit<int>([&] {
        while (!releaseBarrier.load()) { std::this_thread::yield(); }
        return 0;
    });

    std::atomic<bool> ran{false};
    {
        auto handle = pool.submit<int>([&] {
            ran.store(true);
            return 7;
        });
        EXPECT_TRUE(handle.valid());
        // Drop the handle while the task is still queued (worker is busy
        // with the barrier): unlink + cancel happens on destruction.
    }
    // Release the barrier, then verify the cancelled task never ran.
    releaseBarrier.store(true);
    EXPECT_TRUE(barrier.waitForDone(5000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(ran.load());

    pool.shutdown();
}

TEST_CASE(pool_cancel_running_task_cooperatively) {
    CtThreadPool pool;
    pool.start(1);

    std::atomic<bool> sawCancel{false};
    std::atomic<bool> running{false};
    auto handle = pool.submit<int>(
        [&](crisp::thread::CtCancelToken& tok) {
            running.store(true);
            while (!tok.isCancelled()) {
                std::this_thread::yield();
            }
            sawCancel.store(true);
            return 0;
        },
        /*priority=*/0, std::in_place);

    // Wait until the task is actually running (audit K3 fix: phase check
    // instead of a fixed sleep).
    while (handle.phase() != CtTaskPhase::kRunning) {
        if (!running.load()) std::this_thread::yield();
        else break;
    }
    handle.cancel();
    EXPECT_TRUE(handle.waitForDone(5000));
    EXPECT_TRUE(sawCancel.load());
    EXPECT_EQ(handle.phase(), CtTaskPhase::kCancelled);

    pool.shutdown();
}

TEST_CASE(pool_shutdown_is_idempotent) {
    CtThreadPool pool;
    pool.start(2);
    pool.shutdown();
    pool.shutdown(); // second call is a no-op
    EXPECT_FALSE(pool.running());
}

TEST_CASE(pool_destructor_joins_workers) {
    std::atomic<bool> done{false};
    {
        CtThreadPool pool;
        pool.start(2);
        auto handle = pool.submit<int>([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            done.store(true);
            return 1;
        });
        EXPECT_TRUE(handle.valid());
        pool.shutdown(); // drains queued tasks, joins workers
    }
    EXPECT_TRUE(done.load());
}

TEST_CASE(pool_submit_zero_heap_for_node) {
    CtThreadPool pool;
    pool.start(1);

    // Reset counters before submit (audit K2 fix: global operator new is
    // installed above, so this assertion is real).
    g_allocCount.store(0);
    g_deallocCount.store(0);
    {
        // Trivial capture lambda: with libstdc++ SBO the std::function holds
        // the lambda inline, so the only unavoidable allocation on submit is
        // the task object itself (shared_ptr control block + CtTask are
        // allocated together in make_shared — a single new call).
        auto handle = pool.submit<int>([] { return 1; });
        EXPECT_TRUE(handle.valid());
        EXPECT_TRUE(handle.waitForDone(5000));
    }
    const std::size_t allocs = g_allocCount.load();
    // make_shared allocates the task object ONCE; the intrusive node is
    // embedded (zero per-node allocation). Upper bound 2 covers the
    // std::function SBO uncertainty (one extra block if it ever heap-frees).
    EXPECT_LE(allocs, 2u);
    pool.shutdown();
}

TEST_CASE(pool_task_trace_records_phases) {
    CtThreadPool pool;
    pool.start(1);
    auto handle = pool.submit<int>([] { return 3; });
    EXPECT_TRUE(handle.waitForDone(5000));

    std::vector<CtTaskRecord> recs(64);
    const std::size_t n = pool.copyTaskTrace(recs.data(), recs.size());
    EXPECT_GE(n, 2u); // kSubmitted + terminal

    bool sawSubmitted = false, sawRunning = false, sawTerminal = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (recs[i].phase == CtTaskPhase::kSubmitted)
            sawSubmitted = true;
        if (recs[i].phase == CtTaskPhase::kRunning)
            sawRunning = true;
        if (recs[i].phase == CtTaskPhase::kCompleted ||
            recs[i].phase == CtTaskPhase::kCancelled)
            sawTerminal = true;
        if (recs[i].phase == CtTaskPhase::kCompleted) {
            EXPECT_NE(recs[i].workerThread, 0u); // ran on a real worker
        }
    }
    EXPECT_TRUE(sawSubmitted);
    EXPECT_TRUE(sawRunning); // code-review WARN-2: in-flight phase recorded
    EXPECT_TRUE(sawTerminal);
    pool.shutdown();
}

TEST_CASE(pool_task_runs_on_worker_thread) {
    CtThreadPool pool;
    pool.start(2);

    const auto submitter = std::this_thread::get_id();
    std::atomic<bool> onWorker{false};
    auto handle = pool.submit<int>([&] {
        onWorker.store(std::this_thread::get_id() != submitter);
        return 1;
    });
    EXPECT_TRUE(handle.waitForDone(5000));
    EXPECT_TRUE(onWorker.load());
    pool.shutdown();
}

TEST_CASE(pool_rejects_submit_when_not_running) {
    CtThreadPool pool; // never started
    auto handle = pool.submit<int>([] { return 1; });
    EXPECT_FALSE(handle.valid());
}

TEST_CASE(pool_task_exception_is_captured) {
    CtThreadPool pool;
    pool.start(1);
    auto handle = pool.submit<int>([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_TRUE(handle.waitForDone(5000));
    EXPECT_TRUE(std::holds_alternative<std::exception_ptr>(handle.result()));
    bool rethrown = false;
    try {
        std::rethrow_exception(std::get<std::exception_ptr>(handle.result()));
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    EXPECT_TRUE(rethrown);
    pool.shutdown();
}

TEST_CASE(pool_handle_move_assignment_cleans_old_task) {
    // WARN-1 coverage: move-assigning over a handle must run the cancel/
    // join protocol on the OLD task (via tmp destructor in copy-and-swap),
    // not silently abandon it.
    CtThreadPool pool;
    pool.start(1);

    // Pin the worker: old task stays queued (kSubmitted).
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    while (barrier.phase() != CtTaskPhase::kRunning) { std::this_thread::yield(); }

    std::atomic<bool> oldRan{false};
    auto oldHandle = pool.submit<int>([&] { oldRan.store(true); return 1; });
    EXPECT_TRUE(oldHandle.valid());

    auto newHandle = pool.submit<int>([] { return 2; });
    EXPECT_TRUE(newHandle.valid());

    // Move-assign: oldHandle's task must be cancelled+unlinked (tmp dtor),
    // newHandle's task taken over by oldHandle.
    oldHandle = std::move(newHandle);
    EXPECT_TRUE(oldHandle.valid());
    EXPECT_FALSE(newHandle.valid());

    release.store(true);
    EXPECT_TRUE(barrier.waitForDone(5000));
    EXPECT_TRUE(oldHandle.waitForDone(5000));
    EXPECT_EQ(std::get<int>(oldHandle.result()), 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(oldRan.load()); // old task was cancelled, never ran

    pool.shutdown();
}

TEST_CASE(pool_handle_self_move_is_safe) {
    // WARN-1 coverage: self move-assignment must be a no-op (guard).
    CtThreadPool pool;
    pool.start(1);
    auto handle = pool.submit<int>([] { return 5; });
    EXPECT_TRUE(handle.valid());
    CtTaskHandle<int>& ref = handle;
    handle = std::move(ref); // self-assignment
    EXPECT_TRUE(handle.valid());
    EXPECT_TRUE(handle.waitForDone(5000));
    EXPECT_EQ(std::get<int>(handle.result()), 5);
    pool.shutdown();
}

TEST_CASE(ct_thread_move_then_join) {
    // WARN-1 coverage: CtThread move semantics + join contract.
    std::atomic<bool> ran{false};
    std::atomic<crisp::event::CtThreadId> tid{0};
    crisp::thread::CtThread t([&] {
        ran.store(true);
        tid.store(crisp::event::currentThreadId());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    EXPECT_TRUE(t.joinable());

    crisp::thread::CtThread t2(std::move(t));
    EXPECT_FALSE(t.joinable()); // moved-from
    EXPECT_TRUE(t2.joinable());

    t2.join();
    EXPECT_TRUE(ran.load());
    EXPECT_NE(tid.load(), 0u);
    EXPECT_EQ(t2.threadId(), tid.load()); // id survives join
}

TEST_CASE(pool_start_twice_is_noop) {
    // Code-review MUST regression: a second start() while running must be a
    // no-op (the old code spawned workers then deadlocked joining them).
    CtThreadPool pool;
    pool.start(1);
    pool.start(2); // must return immediately, no deadlock
    pool.start(1); // again
    EXPECT_TRUE(pool.running());
    EXPECT_EQ(pool.workerCount(), 1u); // first start's workers only
    auto h = pool.submit<int>([] { return 9; });
    EXPECT_TRUE(h.waitForDone(5000));
    EXPECT_EQ(std::get<int>(h.result()), 9);
    pool.shutdown();
}

TEST_CASE(pool_concurrent_start_is_safe) {
    // Code-review MUST-1 regression: two threads calling start() at once
    // must not both commit (the loser must see running==true and return).
    // Repeat several rounds to widen the race window.
    for (int round = 0; round < 8; ++round) {
        CtThreadPool pool;
        std::thread a([&] { pool.start(1); });
        std::thread b([&] { pool.start(2); });
        a.join();
        b.join();
        EXPECT_TRUE(pool.running());
        // Exactly ONE start() must have won: the loser returned as no-op.
        EXPECT_GE(pool.workerCount(), 1u);
        EXPECT_LE(pool.workerCount(), 2u);
        auto h = pool.submit<int>([round] { return round; });
        EXPECT_TRUE(h.waitForDone(5000));
        EXPECT_EQ(std::get<int>(h.result()), round);
        pool.shutdown();
    }
}

TEST_CASE(pool_overflow_rejects_submit) {
    // Keep the single worker BUSY forever (barrier task), so submits
    // accumulate past kMaxQueued and overflow rejection kicks in.
    CtThreadPool pool;
    pool.start(1);
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    // Barrier must be running (worker busy) before we flood the queue.
    while (barrier.phase() != CtTaskPhase::kRunning) { std::this_thread::yield(); }

    // Flood: submit well past kMaxQueued (8192). KEEP the handles (dropping
    // them would cancel+unlink the tasks and the queue never fills). Overflow
    // must reject.
    std::vector<CtTaskHandle<int>> holders;
    holders.reserve(20000);
    bool sawReject = false;
    for (int i = 0; i < 20000; ++i) {
        auto h = pool.submit<int>([i] { return i; });
        if (!h.valid()) {
            sawReject = true;
            break;
        }
        holders.push_back(std::move(h));
    }
    EXPECT_TRUE(sawReject); // cap enforced

    release.store(true);
    barrier.waitForDone(5000);
    pool.shutdown();
}