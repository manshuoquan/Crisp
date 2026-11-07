// CRISP threading subsystem demo (MT1-MT3).
//
// Shows the type-safe task pipeline:
//   MT1: CtThreadPool.submit<T>(fn) -> CtTaskHandle<T> (cooperative cancel)
//   MT2: CtFuture<T> (waitReady / result / cancel) + CtTaskEventBridge
//        (task completion posted back to the UI thread's CtEventLoop as
//        CtEvent<CtTaskDone<T>>)
//   MT3: CtParallel::map / filter / reduce (self-scheduled chunks,
//        deterministic in-order results)
//
// New-API invariants:
//   - the pool MUST be started with start(N) before submit (0 = default cap);
//   - a rejected submit returns an INVALID handle (check future.valid());
//   - waitForDone()/waitReady() return true at a terminal state;
//   - the bridge's EventSet MUST contain CtTaskDone<T> (static_asserted);
//   - CtParallel calls are BLOCKING: never call them inside a worker task.
//
// Build & run:
//   cmake -S . -B build -G "MinGW Makefiles"
//   cmake --build build --parallel 2
//   ./build/examples/thread_demo/thread_demo.exe

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "thread/ct_future.h"
#include "thread/ct_parallel.h"
#include "thread/ct_task_event.h"
#include "thread/ct_task_event_bridge.h"
#include "thread/ct_thread_pool.h"
#include "thread/ct_thread_trace.h" // CtTaskPhase / CtTaskRecord
#include "event/ct_dispatcher.h"
#include "event/ct_event_loop.h"
#include "event/ct_event_set.h"
#include "event/ct_receiver.h"
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadId

using crisp::event::CtDispatcher;
using crisp::event::CtEvent;
using crisp::event::CtEventLoop;
using crisp::event::CtEventSet;
using crisp::event::CtReceiver;
using crisp::event::CtThreadId;
using crisp::event::createReceiver;
using crisp::event::currentThreadId;
using crisp::thread::CtFuture;
using crisp::thread::CtParallel;
using crisp::thread::CtResult;
using crisp::thread::CtTaskDone;
using crisp::thread::CtTaskEventBridge;
using crisp::thread::CtTaskPhase;
using crisp::thread::CtTaskRecord;
using crisp::thread::CtThreadPool;

// The bridge posts CtTaskDone<int> back to the UI thread: the EventSet
// used by the dispatcher must contain that payload (static_asserted in
// CtTaskEventBridge). Add the framework trait specialization first.
CRISP_EVENT_TYPE(crisp::thread::CtTaskDone<int>, "CtTaskDone<int>");

using ThreadDemoEventSet = CtEventSet<CtTaskDone<int>>;
using ThreadDemoDispatcher = CtDispatcher<ThreadDemoEventSet>;

// ---------------------------------------------------------------------------
// A receiver that counts task completions arriving on the UI thread.
// It is created on the UI thread, so its affinity matches the event loop.
// ---------------------------------------------------------------------------
class CtDoneSink : public CtReceiver {
public:
    explicit CtDoneSink(int* completed, int* total)
        : m_completed(completed), m_total(total) {}

    void onDone(const CtEvent<CtTaskDone<int>>& ev) {
        ++*m_total;
        const CtTaskDone<int>& done = ev.payload();
        if (!done.result) {
            std::printf("  [ui] task %llu cancelled (phase %d)\n",
                        static_cast<unsigned long long>(done.taskId),
                        static_cast<int>(done.phase));
            return;
        }
        const CtResult<int>& result = *done.result;
        if (const int* value = std::get_if<int>(&result)) {
            std::printf("  [ui] task %llu done with value %d\n",
                        static_cast<unsigned long long>(done.taskId), *value);
            *m_completed += *value;
        } else {
            // The task threw: the result holds an exception_ptr.
            std::printf("  [ui] task %llu FAILED with exception\n",
                        static_cast<unsigned long long>(done.taskId));
        }
        // NOTE: the bridge-delivered event is a const CtEvent<T>& - the
        // sink observes it; acceptance is not required here.
    }

private:
    int* m_completed; ///< accumulated sum of completed task values
    int* m_total;     ///< number of done events received on the UI thread
};

// ---------------------------------------------------------------------------
// Demo 1 (MT1/MT2): submit tasks, wait for them, cancel one, dump the trace.
// ---------------------------------------------------------------------------
void demoSubmitAndWait(CtThreadPool& pool) {
    std::printf("== demo: submit + wait (MT1/MT2) ==\n");

    auto handle = pool.submit<int>([] { return 21 * 2; });
    CtFuture<int> future(std::move(handle)); // adopts the task handle
    if (!future.valid()) {
        std::printf("  submit rejected (pool not running? queue full?)\n");
        return;
    }
    const bool done = future.waitReady(5000);
    int result = 0;
    if (done) {
        // get_if: the result is a variant<int, exception_ptr> - a throwing
        // task would land in the exception branch (robustness demo).
        if (const int* value = std::get_if<int>(&future.result()))
            result = *value;
    }
    std::printf("  future ready=%d phase=%d result=%d\n",
                done ? 1 : 0, static_cast<int>(future.phase()), result);

    // Cancel a task that is still queued: pin the worker with a barrier
    // task, then submit a second task and cancel it immediately.
    std::atomic<bool> release{false};
    auto barrier = pool.submit<int>([&release] {
        while (!release.load()) { std::this_thread::yield(); }
        return 0;
    });
    if (!barrier.valid()) {
        std::printf("  barrier submit rejected - skip cancel demo\n");
        return;
    }
    // Wait until the barrier is running (bounded wait: no infinite spin if
    // the pool rejected/cancelled the barrier first).
    for (int i = 0; i < 5000 && barrier.phase() != CtTaskPhase::kRunning; ++i)
        std::this_thread::yield();

    if (barrier.phase() != CtTaskPhase::kRunning) {
        // The worker did not pick up the barrier in time (e.g. scheduling
        // delay): the cancel demo below would be racy - skip it.
        std::printf("  barrier did not reach kRunning - skip cancel demo\n");
        release.store(true);
        barrier.waitForDone(5000);
        return;
    }

    auto queued = pool.submit<int>([] { return 123; });
    CtFuture<int> toCancel(std::move(queued));
    toCancel.cancel();
    toCancel.waitReady(5000);
    std::printf("  cancelled future phase=%d (expect kCancelled=%d)\n",
                static_cast<int>(toCancel.phase()),
                static_cast<int>(CtTaskPhase::kCancelled));

    release.store(true);
    const bool barrierDone = barrier.waitForDone(5000);
    if (!barrierDone)
        std::printf("  warning: barrier did not finish within 5s\n");

    // Task-side trace: every task's journey (submitted/running/completed/
    // cancelled). The ring may hold multiple segments per task (submit +
    // worker), so dump all records that fit.
    std::printf("  -- pool task trace --\n");
    std::vector<CtTaskRecord> recs(pool.taskTrace().size());
    const std::size_t n = pool.copyTaskTrace(recs.data(), recs.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::printf("    task %llu phase=%d worker=%llu\n",
                    static_cast<unsigned long long>(recs[i].taskId),
                    static_cast<int>(recs[i].phase),
                    static_cast<unsigned long long>(recs[i].workerThread));
    }
}

// ---------------------------------------------------------------------------
// Demo 2 (MT3): parallel map / filter / reduce over a shared pool.
// Blocking: call from the main thread only.
// ---------------------------------------------------------------------------
void demoParallel(CtThreadPool& pool) {
    std::printf("== demo: CtParallel map/filter/reduce (MT3) ==\n");

    // 300 elements > kDefaultChunk (256): exercises the multi-chunk path.
    // A small vector would run the serial single-chunk fast path instead.
    std::vector<int> data;
    data.reserve(300);
    for (int i = 1; i <= 300; ++i)
        data.push_back(i);

    // map: double every element (results kept in input order).
    const auto doubled = CtParallel::map(pool, data.begin(), data.end(),
                                         [](int x) { return x * 2; });
    std::printf("  map(x*2)[0..9] =");
    for (int i = 0; i < 10 && i < static_cast<int>(doubled.size()); ++i)
        std::printf(" %d", doubled[i]);
    std::printf("  ... (size=%zu)\n", doubled.size());

    // filter: keep only even elements (ordered output).
    const auto evens = CtParallel::filter(pool, data.begin(), data.end(),
                                          [](int x) { return x % 2 == 0; });
    std::printf("  filter(even)[0..9] =");
    for (int i = 0; i < 10 && i < static_cast<int>(evens.size()); ++i)
        std::printf(" %d", evens[i]);
    std::printf("  ... (size=%zu)\n", evens.size());

    // reduce: sum (deterministic chunk order).
    const int sum = CtParallel::reduce(pool, data.begin(), data.end(), 0,
                                       [](int acc, int x) { return acc + x; });
    std::printf("  reduce(sum) = %d (expect %d)\n", sum, 300 * 301 / 2);

    // reduce with a non-associative op: deterministic chunked order (not an
    // exact left fold) - the docs document this explicitly.
    const int sub = CtParallel::reduce(pool, data.begin(), data.end(), 100000,
                                       [](int acc, int x) { return acc - x; });
    std::printf("  reduce(100000 - x) = %d\n", sub);
}

// ---------------------------------------------------------------------------
// Demo 3 (MT2 bridge): background task -> CtTaskDone<int> event delivered
// to the UI thread's event loop.
// ---------------------------------------------------------------------------
void demoBridgeToUiThread(CtThreadPool& pool) {
    std::printf("== demo: CtTaskEventBridge (MT2) ==\n");

    auto dispatcher = std::make_shared<ThreadDemoDispatcher>();

    // THIS thread acts as the UI thread: attach a loop + dispatcher so
    // postTo knows where to deliver the completion events.
    CtEventLoop uiLoop;
    uiLoop.attachToCurrentThread();
    uiLoop.setDispatcher(dispatcher);
    const CtThreadId uiThreadId = currentThreadId();

    int completed = 0;
    int total = 0;
    auto sink = createReceiver<CtDoneSink>(&completed, &total);
    auto token = dispatcher->addHandler<CtTaskDone<int>>(
        sink, [&sink](const CtEvent<CtTaskDone<int>>& ev) { sink->onDone(ev); });

    // Bridge: submit tasks; their completion is posted back to the UI
    // thread (the loop drains the queue and dispatches to the sink).
    CtTaskEventBridge<ThreadDemoEventSet, int> bridge(dispatcher);
    auto f1 = bridge.submit(pool, uiThreadId, [] { return 40; });
    auto f2 = bridge.submit(pool, uiThreadId, [] { return 2; });
    // A task that throws: the done event carries the exception_ptr.
    auto f3 = bridge.submit(pool, uiThreadId, []() -> int {
        throw std::runtime_error("boom");
    });

    // Explicitly wait for the tasks to reach a terminal state BEFORE the
    // futures are destroyed (destruction would otherwise join implicitly).
    // The bridge posts the done events as part of completion.
    if (f1.valid()) f1.waitReady(5000);
    if (f2.valid()) f2.waitReady(5000);
    if (f3.valid()) f3.waitReady(5000);
    (void)f1; (void)f2; (void)f3;

    // Wait for the workers to finish (the bridge posts as part of
    // completion), then run the UI loop until the sink saw 3 done events.
    std::thread quitter([&] {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
        while (total < 3 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uiLoop.quit(); // thread-safe (noexcept); wakes exec()
    });
    const int iterations = uiLoop.exec();
    quitter.join();
    std::printf("  ui loop ran %d drain iterations; done events=%d, "
                "sum of completed values=%d\n", iterations, total, completed);

    // NOTE: `uiLoop` registered itself in the per-thread routing table on
    // attachToCurrentThread(); its destructor unregisters it. quitter has
    // already joined, so no thread can call quit() after this point.
    token = crisp::event::CtHandlerToken{}; // unregister before sink dies
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    // LIFETIME CONTRACT: every CtTaskHandle/CtFuture produced below MUST be
    // destroyed before `pool` goes away (the demo satisfies this: each
    // demo function returns before main's pool.shutdown()).
    CtThreadPool pool;
    pool.start(2); // two workers (min(hardware_concurrency,8) when 0)

    demoSubmitAndWait(pool);
    std::printf("\n");
    demoParallel(pool);
    std::printf("\n");
    demoBridgeToUiThread(pool);

    pool.shutdown();
    std::printf("\nall thread demos done\n");
    return 0;
}
