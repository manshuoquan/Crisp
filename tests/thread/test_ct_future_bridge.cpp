// MT2 tests: CtFuture + thread->event bridge.
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "thread/ct_future.h"
#include "thread/ct_task_event.h"
#include "thread/ct_task_event_bridge.h"
#include "thread/ct_thread_pool.h"
#include "thread/ct_thread_trace.h" // CtTaskRecord
#include "event/ct_dispatcher.h"
#include "event/ct_event_loop.h"
#include "event/ct_event_set.h"
#include "event/ct_handler_token.h"
#include "event/ct_receiver.h"
#include "test_harness.h"

using crisp::event::CtDispatcher;
using crisp::event::CtEvent;
using crisp::event::CtEventLoop;
using crisp::event::CtEventSet;
using crisp::event::CtReceiver;
using crisp::thread::CtFuture;
using crisp::thread::CtTaskDone;
using crisp::thread::CtTaskEventBridge;
using crisp::thread::CtThreadPool;
using crisp::thread::CtTaskPhase;
using crisp::thread::CtTaskRecord;

// Event set must contain CtTaskDone<int> for the bridge to post it.
CRISP_EVENT_TYPE(crisp::thread::CtTaskDone<int>, "CtTaskDone<int>");
using BridgeEventSet = CtEventSet<CtTaskDone<int>>;
using BridgeDispatcher = CtDispatcher<BridgeEventSet>;

namespace {

/// A receiver that records delivered task-done events (thread-affined to the
/// loop thread by construction).
class DoneSink : public CtReceiver {
public:
    explicit DoneSink(std::atomic<int>* count, std::atomic<int>* value)
        : m_count(count), m_value(value) {}

    void onDone(const CtEvent<CtTaskDone<int>>& ev) {
        m_count->fetch_add(1);
        if (ev.payload().result)
            m_value->store(std::get<int>(*ev.payload().result));
    }

private:
    std::atomic<int>* m_count;
    std::atomic<int>* m_value;
};

} // namespace

TEST_CASE(future_wait_ready_and_result) {
    CtThreadPool pool;
    pool.start(1);
    auto handle = pool.submit<int>([] { return 21; });
    CtFuture<int> future(std::move(handle));
    EXPECT_TRUE(future.valid());
    EXPECT_TRUE(future.waitReady(5000));
    EXPECT_EQ(future.phase(), CtTaskPhase::kCompleted);
    EXPECT_EQ(std::get<int>(future.result()), 21);
    pool.shutdown();
}

TEST_CASE(future_cancel_queued_task) {
    CtThreadPool pool;
    pool.start(1);

    // Pin the worker with a barrier so the second task stays queued.
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    while (barrier.phase() != CtTaskPhase::kRunning) { std::this_thread::yield(); }

    auto handle = pool.submit<int>([] { return 5; });
    CtFuture<int> future(std::move(handle));
    EXPECT_TRUE(future.valid());
    future.cancel();
    EXPECT_TRUE(future.waitReady(5000));
    EXPECT_EQ(future.phase(), CtTaskPhase::kCancelled);

    release.store(true);
    barrier.waitForDone(5000);
    pool.shutdown();
}

TEST_CASE(future_notifier_fires_on_completion) {
    CtThreadPool pool;
    pool.start(1);
    std::atomic<bool> fired{false};
    std::atomic<int> seen{0};
    {
        auto handle = pool.submit<int>([] { return 42; });
        CtFuture<int> future(std::move(handle));
        future.setNotifier([&] {
            fired.store(true);
            seen.store(42);
        });
        EXPECT_TRUE(future.waitReady(5000));
        // Notifier fires on the worker at terminal; poll for it (audit K3
        // fix: no hardcoded sleep — a slow worker may not have fired yet).
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!fired.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        EXPECT_TRUE(fired.load());
        EXPECT_EQ(seen.load(), 42);
    }
    pool.shutdown();
}

TEST_CASE(bridge_posts_done_event_to_target_thread) {
    // Full MT2 loop: submit on the pool (worker thread) -> bridge -> postTo
    // the UI thread's CtEventLoop -> handler receives CtTaskDone<int>.
    auto dispatcher = std::make_shared<BridgeDispatcher>();

    // The "UI thread" is THIS thread: attach a loop + dispatcher so the
    // routing table knows where to deliver posted events.
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);
    const auto uiThreadId = crisp::event::currentThreadId();

    std::atomic<int> received{0};
    std::atomic<int> value{0};
    auto sink = std::make_shared<DoneSink>(&received, &value);
    auto token = dispatcher->addHandler<CtTaskDone<int>>(
        sink, [&sink](CtEvent<CtTaskDone<int>>& ev) { sink->onDone(ev); });

    CtThreadPool pool;
    pool.start(1);

    // Bridge submit: task runs on a worker, completion posted back to UI.
    CtTaskEventBridge<BridgeEventSet, int> bridge(dispatcher);
    auto future = bridge.submit(pool, uiThreadId, [] { return 77; });
    EXPECT_TRUE(future.valid());

    // Wait for the worker to finish (the bridge posts the done event as part
    // of completion), then drain the UI loop. Quit from a thread AFTER the
    // event is processed: poll received, then quit (audit K3 fix — no
    // hardcoded quit delay).
    EXPECT_TRUE(future.waitReady(5000));
    std::thread quitThread([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (received.load() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        loop.quit();
    });
    loop.exec();
    quitThread.join();

    EXPECT_EQ(received.load(), 1);
    EXPECT_EQ(value.load(), 77);

    pool.shutdown();
    token = crisp::event::CtHandlerToken{}; // unregister before sink dies
}

TEST_CASE(bridge_late_onThread_still_delivers) {
    // Audit MUST regression: wiring an EXISTING (already-completed) future
    // via onThread must still deliver the done event — the notifier must not
    // be silently dropped because fireCompletion already ran.
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);
    const auto uiThreadId = crisp::event::currentThreadId();

    std::atomic<int> received{0};
    std::atomic<int> value{0};
    auto sink = std::make_shared<DoneSink>(&received, &value);
    auto token = dispatcher->addHandler<CtTaskDone<int>>(
        sink, [&sink](CtEvent<CtTaskDone<int>>& ev) { sink->onDone(ev); });

    CtThreadPool pool;
    pool.start(1);

    CtTaskEventBridge<BridgeEventSet, int> bridge(dispatcher);

    // Submit and WAIT for completion BEFORE wiring the bridge (fast task:
    // the worker has already fired/not-fired by the time we call onThread).
    auto handle = pool.submit<int>([] { return 88; });
    CtFuture<int> future(std::move(handle));
    EXPECT_TRUE(future.waitReady(5000));

    bridge.onThread(std::move(future), uiThreadId); // late registration

    std::thread quitThread([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (received.load() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        loop.quit();
    });
    loop.exec();
    quitThread.join();

    EXPECT_EQ(received.load(), 1);
    EXPECT_EQ(value.load(), 88);

    pool.shutdown();
    token = crisp::event::CtHandlerToken{};
}

TEST_CASE(bridge_queued_cancel_still_delivers) {
    // Audit MUST regression: a wired future whose task is cancelled while
    // still QUEUED must still deliver CtTaskDone (phase kCancelled) — the
    // owner-cancel path fires the notifier (the worker skips the task).
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    CtEventLoop loop;
    loop.attachToCurrentThread();
    loop.setDispatcher(dispatcher);
    const auto uiThreadId = crisp::event::currentThreadId();

    std::atomic<int> received{0};
    std::atomic<int> cancelledSeen{0};
    auto sink = std::make_shared<DoneSink>(&received, &cancelledSeen);
    // DoneSink records value on result; for cancel we just count + phase.
    auto token = dispatcher->addHandler<CtTaskDone<int>>(
        sink, [&sink, &cancelledSeen](CtEvent<CtTaskDone<int>>& ev) {
            sink->onDone(ev);
            if (ev.payload().phase == crisp::thread::CtTaskPhase::kCancelled)
                cancelledSeen.fetch_add(1);
        });

    CtThreadPool pool;
    pool.start(1);

    // Pin the worker so the second task stays queued.
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    while (barrier.phase() != CtTaskPhase::kRunning) { std::this_thread::yield(); }

    CtTaskEventBridge<BridgeEventSet, int> bridge(dispatcher);
    auto future = bridge.submit(pool, uiThreadId, [] { return 5; });
    EXPECT_TRUE(future.valid());

    // Cancel while queued: the owner-cancel path fires the notifier.
    future.cancel();
    EXPECT_TRUE(future.waitReady(5000));

    std::thread quitThread([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (received.load() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        loop.quit();
    });
    loop.exec();
    quitThread.join();

    EXPECT_EQ(received.load(), 1);      // done event delivered
    EXPECT_EQ(cancelledSeen.load(), 1); // and phase == kCancelled

    release.store(true);
    barrier.waitForDone(5000);
    pool.shutdown();
    token = crisp::event::CtHandlerToken{};
}

TEST_CASE(bridge_delivery_failure_is_observable) {
    // Design §3.1 rule 2/3: a target thread with NO loop must not silently
    // drop the completion — the bridge records the failure in the task trace
    // (audit W1). Use a bogus thread id that has no registered queue.
    auto dispatcher = std::make_shared<BridgeDispatcher>();
    // NO loop attached to this thread => routing table empty for it.
    const auto bogusThread = crisp::event::currentThreadId();

    CtThreadPool pool;
    pool.start(1);

    CtTaskEventBridge<BridgeEventSet, int> bridge(dispatcher);
    bridge.setTaskTrace(&pool.taskTrace()); // expose trace (see below)

    auto future = bridge.submit(pool, bogusThread, [] { return 1; });
    EXPECT_TRUE(future.valid());
    EXPECT_TRUE(future.waitReady(5000));

    // Poll the task trace until the kDropped record appears (audit recheck
    // WARN: no fixed sleep — the worker may not have run the notifier yet).
    std::vector<CtTaskRecord> recs(64);
    bool sawDropped = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!sawDropped && std::chrono::steady_clock::now() < deadline) {
        const std::size_t n = pool.copyTaskTrace(recs.data(), recs.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (recs[i].phase == crisp::thread::CtTaskPhase::kDropped &&
                recs[i].workerThread == bogusThread) {
                sawDropped = true;
                break;
            }
        }
        if (!sawDropped)
            std::this_thread::yield();
    }
    EXPECT_TRUE(sawDropped);

    pool.shutdown();
}
