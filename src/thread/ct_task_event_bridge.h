#pragma once

// CtTaskEventBridge: thread -> event completion bridge (MT2 §3.1).
//
// Part of the CRISP threading subsystem (crisp::thread). Connects a
// CtFuture<T> to the event subsystem: when the task completes, the bridge
// posts CtEvent<CtTaskDone<T>> to a target thread's CtEventLoop via
// CtDispatcher::postTo (the ONLY cross-thread delivery channel — design
// §3.1 rule 1: the worker never calls user code directly; it only posts).
//
// Usage:
//   auto dispatcher = std::make_shared<CtDispatcher<MyEventSet>>();
//   dispatcher->addHandler<CtTaskDone<int>>([](const CtEvent<CtTaskDone<int>>& e) {
//       // runs on the target thread's event loop (delivered via postTo)
//       auto& done = e.payload;
//       if (done.result) { /* std::get<int>(*done.result) ... */ }
//   });
//
//   CtTaskEventBridge<MyEventSet, int> bridge(dispatcher);
//   auto future = bridge.submit(pool, uiThreadId, [] { return compute(); });
//   // future.waitReady() / future.cancel() available; the handler above
//   // receives the completion event on the UI thread.
//
// The user's EventSet must contain CtTaskDone<T> (static_asserted).

#include <memory>
#include <type_traits>
#include <utility>

#include "event/ct_dispatcher.h"   // crisp::event::CtDispatcher<EventSet>
#include "event/ct_event.h"        // CtEvent<T>, CtEventOrigin
#include "event/ct_event_set.h"    // crisp::event::CtEventSet<Ts...>
#include "ct_future.h"             // CtFuture<T>
#include "ct_task_event.h"         // CtTaskDone<T>
#include "ct_thread_pool.h"        // CtThreadPool, CtTaskHandle<T>

namespace crisp::thread {

namespace detail {

/// True if EventSet (a CtEventSet<Ts...>) contains payload type T.
template <typename EventSet, typename T>
struct EventSetContains : std::false_type {};

template <typename... Ts, typename T>
struct EventSetContains<crisp::event::CtEventSet<Ts...>, T>
    : std::disjunction<std::is_same<Ts, T>...> {};

} // namespace detail

/// Bridge between crisp::thread futures and crisp::event dispatchers.
/// EventSet must include CtTaskDone<T> for every T used.
///
/// DELIVERY FAILURE OBSERVABILITY (audit W1): if the bridge is given a
/// task-side trace (via setTaskTrace), a failed postTo (target thread has
/// no loop / queue closed / overflow / allocation failure) is recorded as a
/// kDropped phase with the FAILED TARGET THREAD in workerThread and
/// cancelByOwner=false. Without a trace, the failure is silently dropped
/// (documented best-effort — see CtTaskDone).
template <typename EventSet, typename T>
class CtTaskEventBridge {
public:
    /// Binds to a dispatcher (the delivery target for posted completions).
    explicit CtTaskEventBridge(
        std::shared_ptr<crisp::event::CtDispatcher<EventSet>> dispatcher)
        : m_dispatcher(std::move(dispatcher)) {
        static_assert(detail::EventSetContains<EventSet, CtTaskDone<T>>::value,
                      "EventSet must contain CtTaskDone<T>");
    }

    /// Optional: install a task-side trace for delivery-failure records
    /// (audit W1). The bridge records kDropped with workerThread == the
    /// FAILED TARGET THREAD when postTo is not accepted (no loop / closed /
    /// overflow / allocation failure).
    void setTaskTrace(CtTaskTrace* trace) noexcept { m_trace = trace; }

    /// Submit a task returning T; on completion, post CtEvent<CtTaskDone<T>>
    /// to `targetThread`'s event loop (via dispatcher->postTo). Returns the
    /// future (waitReady/cancel still work). A returned INVALID future
    /// means the pool rejected the submit (queue full / not running) — check
    /// the pool's task trace for the kDropped record (audit SUGGEST).
    template <typename Fn>
    CtFuture<T> submit(CtThreadPool& pool,
                       crisp::event::CtThreadId targetThread,
                       Fn&& fn);

    /// Wire an EXISTING future (e.g. from pool.submit) to post its
    /// completion event to `targetThread`'s loop.
    CtFuture<T> onThread(CtFuture<T> future,
                         crisp::event::CtThreadId targetThread);

private:
    std::shared_ptr<crisp::event::CtDispatcher<EventSet>> m_dispatcher;
    CtTaskTrace* m_trace = nullptr; ///< optional failure record sink
};

// ---- implementation ----

template <typename EventSet, typename T>
template <typename Fn>
CtFuture<T> CtTaskEventBridge<EventSet, T>::submit(CtThreadPool& pool,
                                                   crisp::event::CtThreadId targetThread,
                                                   Fn&& fn) {
    // Wire the pool's task trace by default (code-review WARN-1: delivery
    // failure must be observable without an explicit setTaskTrace call).
    if (!m_trace)
        m_trace = &pool.taskTrace();
    auto handle = pool.submit<T>(std::forward<Fn>(fn));
    if (!handle.valid())
        return CtFuture<T>{};
    CtFuture<T> future(std::move(handle));
    return onThread(std::move(future), targetThread);
}

template <typename EventSet, typename T>
CtFuture<T> CtTaskEventBridge<EventSet, T>::onThread(CtFuture<T> future,
                                                     crisp::event::CtThreadId targetThread) {
    if (!future.valid())
        return future;
    auto dispatcher = m_dispatcher;
    CtTaskTrace* trace = m_trace;
    // Capture the task (shared_ptr) for result/phase access in the notifier;
    // keeps the task alive until the notifier runs.
    auto task = future.handle().task();
    future.setNotifier([dispatcher, trace, task, targetThread]() mutable {
        const auto phase = task->phase();
        if (phase != CtTaskPhase::kCompleted && phase != CtTaskPhase::kCancelled)
            return; // not terminal (shouldn't happen — notifier fires at terminal)
        CtTaskDone<T> done;
        done.phase = phase;
        done.taskId = task->id();
        if (phase == CtTaskPhase::kCompleted)
            done.result = task->result();
        auto ev = crisp::event::CtEvent<CtTaskDone<T>>(std::move(done));
        if (!dispatcher)
            return;
        // postTo: the ONLY cross-thread channel (design §3.1 rule 1).
        // Delivery failure (no loop on target / closed queue / overflow) is
        // observable: record kDropped in the task trace if one was installed
        // (audit W1). Exceptions from postTo (bad_alloc in the holder) are
        // ALSO recorded — not silently swallowed (audit recheck WARN-2).
        bool delivered = false;
        try {
            delivered = dispatcher->template postTo(targetThread, std::move(ev)) ==
                        crisp::event::CtDispatchResult::kHandled;
        } catch (...) {
            delivered = false; // holder allocation failed
        }
        if (!delivered && trace) {
            // §3.1 rule 3: delivery failure is observable — record kDropped
            // (independent phase, audit recheck WARN) with the failed target
            // thread in workerThread (audit W1 marker).
            CtTaskRecord rec;
            rec.phase = CtTaskPhase::kDropped;
            rec.taskId = task->id();
            rec.submitThread = task->submitThreadId;
            rec.workerThread = targetThread; // failed target (no sentinel)
            rec.queuedLatencyUs = 0;
            rec.runDurationUs = 0;
            rec.cancelByOwner = false; // not owner-cancelled: delivery failed
            trace->record(rec);
        }
    });
    return future;
}

} // namespace crisp::thread