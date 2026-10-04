#pragma once

// CtEventLoop: per-thread event loop (M2 async core).
//
// Part of the CRISP event subsystem (crisp::event). Implements
// docs/event-system/04-event-system-redesign.md §7.11: connects the queue
// to the dispatcher — drains the affinity queue, drives attached platform
// sources, and provides the thread→loop routing primitive used by
// post/postTo. Qt analog: QEventLoop + QCoreApplication::exec.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "ct_queue.h"            // CtQueueBase, CtQueue, CtPendingEventBase
#include "ct_event_source.h"     // CtEventSource (attachSource/detachSource)
#include "ct_dispatcher.h"       // CtDispatcherBase, CtDispatcher<EventSet>
#include "ct_thread_affinity.h"  // CtThreadId
#include "ct_event.h"            // CtEvent, CtEventOrigin, CtTracePhase
#include "ct_dispatch_context.h" // CtDispatchContext (loop's record path)
#include "ct_trace.h"            // CtTrace

namespace crisp::event {

/// The per-thread event loop. Owns one CtQueue and drives one dispatcher.
/// Qt analog: QEventLoop::exec().
class CtEventLoop {
public:
    CtEventLoop();
    ~CtEventLoop();

    CtEventLoop(const CtEventLoop&) = delete;
    CtEventLoop& operator=(const CtEventLoop&) = delete;

    /// Attach the dispatcher this loop drains into.
    /// A loop may attach only one dispatcher at a time. Lazily creates
    /// m_queue (CtQueue<EventSet>) here — the constructor cannot know
    /// EventSet. HARD CONTRACT (Rev 8, P2-4): only ONE setDispatcher call
    /// with the SAME EventSet is allowed; a second call (including a
    /// different EventSet) asserts — the queue's EventSet must equal the
    /// dispatcher's EventSet (§7.11 contract #3). Also asserts
    /// dispatcher->assertSameThread() — the dispatcher must be created on
    /// the loop's thread (Rev 9, P2-4), so async broadcast does not trip the
    /// §8.2 Step 1 affinity assert at drain time.
    template <typename EventSet>
    void setDispatcher(std::shared_ptr<CtDispatcher<EventSet>> dispatcher);

    /// Enter the loop: drain the queue and process platform sources until
    /// quit() is called. Blocking; MUST run on the loop's thread.
    /// Returns the number of iterations processed.
    int exec();

    /// Ask the loop to exit after the current iteration.
    /// Thread-safe; may be called from any thread.
    void quit() noexcept;

    /// The thread this loop is affined to (set at construction).
    CtThreadId affinityThread() const noexcept;

    /// The queue this loop drains (used by post/postTo, §10). Precondition:
    /// setDispatcher() has been called (asserts otherwise).
    CtQueueBase& queue() noexcept;

    /// The loop affined to the CURRENT thread, or nullptr if the current
    /// thread has no loop attached. Per-thread TLS lookup (§7.11 contract #6).
    static CtEventLoop* current() noexcept;

    /// Attach THIS loop to the current thread (TLS registration).
    /// A thread may host at most one loop at a time (asserts otherwise).
    void attachToCurrentThread();

    /// Attach a platform event source to this loop. The loop owns it
    /// (shared_ptr); exec() polls all attached sources each iteration
    /// (§7.12 contract #3). Sets the source's affinity thread on success.
    /// Returns false if the source is already attached.
    bool attachSource(const std::shared_ptr<CtEventSource>& source);

    /// Detach a source; returns false if not attached. Idempotent.
    bool detachSource(const std::shared_ptr<CtEventSource>& source);

    /// Template bridge for APPLICATION/BRIDGE-layer manual injection
    /// (Rev 10, P2-A): injects a concrete event into THIS loop's queue.
    /// Asserts eventSetId() == EventSet key BEFORE the downcast (the
    /// strong-typed push path bypasses pushAny's check), then calls
    /// CtQueue<EventSet>::push(target, std::move(event)). T is deduced from
    /// the argument. Callable only after setDispatcher (asserts otherwise).
    template <typename EventSet, typename T>
    void injectEvent(std::weak_ptr<CtReceiver> target, CtEvent<T>&& event);

private:
    /// Drain one queue batch + dropped report (the exec() inner loop, §7.11
    /// contract #2). Returns the number of events dispatched this round.
    std::size_t drainOnce();

    std::shared_ptr<CtDispatcherBase> m_dispatcher; ///< attached dispatcher
    /// Per-thread queue; lazily created in setDispatcher (Rev 5, P2-3).
    /// SHARED_PTR (M2 routing decision, §10.1 contract #1/#7): the thread→queue
    /// routing table must hold a shared_ptr so a concurrent post() from
    /// another thread can keep the queue alive through its push — a
    /// unique_ptr would dangle the moment the loop quits. The loop still
    /// owns the primary reference; unregisterThreadQueue removes the table
    /// entry at destruction (§7.11 contract #7).
    std::shared_ptr<CtQueueBase> m_queue;
    std::atomic<bool> m_quit{false};
    CtThreadId m_affinityThread = 0;                ///< loop's thread (set at construction)
    bool m_hasDispatcher = false;                   ///< setDispatcher completed
    std::vector<std::shared_ptr<CtEventSource>> m_sources; ///< attached sources (Rev 8, P2-2)
};

} // namespace crisp::event

#include "ct_event_loop.inl"
