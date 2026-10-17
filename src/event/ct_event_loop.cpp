// ct_event_loop.cpp — non-template CtEventLoop machinery (M2).
//
// Part of the CRISP event subsystem (crisp::event). §5.3: non-template
// mechanisms live in .cpp files. The template entries (setDispatcher,
// injectEvent) are in ct_event_loop.inl.

#include "ct_event_loop.h"

#include <algorithm>
#include <cassert>
#include <mutex>
#include <thread>

namespace crisp::event {

namespace {

/// Per-thread TLS slot holding the current thread's loop (§7.11 contract #6).
thread_local CtEventLoop* t_currentLoop = nullptr;

} // namespace (anonymous)

CtEventLoop::CtEventLoop() : m_affinityThread(currentThreadId()) {}

CtEventLoop::~CtEventLoop() {
    // destruction & routing unregister (Rev 5, P2-4): remove this loop from the TLS slot and
    // from the thread→queue routing table so no other thread's post can find
    // a dangling queue pointer. Close the queue first so a blocked exec
    // waiter is released.
    if (m_queue) {
        m_queue->close();
        detail::unregisterThreadQueue(m_affinityThread);
    }
    if (t_currentLoop == this)
        t_currentLoop = nullptr;
}

int CtEventLoop::exec() {
    // MUST run on the loop's thread (§7.11 contract #1).
    if (currentThreadId() != m_affinityThread) {
        assert(false && "CtEventLoop::exec on non-affinity thread");
        return 0;
    }

    int iterations = 0;
    m_quit.store(false, std::memory_order_relaxed);

    // Loop until quit() OR the queue is closed (destruction). The queue's
    // waitForEvent blocks when empty; push/close wake it. Guard the queue
    // pointer: exec() without setDispatcher() has no queue (drainOnce also
    // guards via m_hasDispatcher) — assert + return 0 instead of UB.
    if (!m_queue) {
        assert(false && "CtEventLoop::exec: setDispatcher not called");
        return 0;
    }

    while (!m_quit.load(std::memory_order_relaxed) &&
           !m_queue->isClosed()) {
        // 1) Drain the queue (dispatch any pending events into the
        //    dispatcher, then take the dropped report).
        const std::size_t drained = drainOnce();
        iterations += static_cast<int>(drained);

        // 2) Poll all attached platform sources (each may enqueue events).
        for (const auto& source : m_sources) {
            if (!m_quit.load(std::memory_order_relaxed))
                source->poll(*m_queue);
        }

        // 3) Wait for the next event (§14 #8, condition-variable wait).
        //    No events + not quitting → block until a push/close notifies.
        if (!m_quit.load(std::memory_order_relaxed) &&
            !m_queue->isClosed() && m_queue->size() == 0)
            m_queue->waitForEvent();
    }

    return iterations;
}

void CtEventLoop::quit() noexcept {
    m_quit.store(true, std::memory_order_relaxed);
    // Wake the exec wait (if blocked in waitForEvent, §14 #8): close the
    // queue so the predicate (!empty || closed) becomes true.
    if (m_queue)
        m_queue->close();
}

CtThreadId CtEventLoop::affinityThread() const noexcept {
    return m_affinityThread;
}

CtQueueBase& CtEventLoop::queue() noexcept {
    assert(m_hasDispatcher && "CtEventLoop::queue: setDispatcher not called");
    return *m_queue;
}

CtEventLoop* CtEventLoop::current() noexcept {
    return t_currentLoop;
}

void CtEventLoop::attachToCurrentThread() {
    // A thread may host at most one loop at a time.
    assert(!t_currentLoop && "a thread may host at most one CtEventLoop");
    t_currentLoop = this;
}

bool CtEventLoop::attachSource(const std::shared_ptr<CtEventSource>& source) {
    if (!source)
        return false;
    for (const auto& s : m_sources)
        if (s == source)
            return false; // already attached

    source->setAffinityThread(m_affinityThread); // (Rev 12, P2-B)
    m_sources.push_back(source);
    return true;
}

bool CtEventLoop::detachSource(const std::shared_ptr<CtEventSource>& source) {
    const auto it = std::find(m_sources.begin(), m_sources.end(), source);
    if (it == m_sources.end())
        return false;
    m_sources.erase(it);
    return true;
}

std::size_t CtEventLoop::drainOnce() {
    if (!m_hasDispatcher)
        return 0;

    std::size_t count = 0;
    CtPendingEventBase* pe = nullptr;

    // §7.11 contract #2 drain loop: front() records kQueued internally; the
    // dispatcher is ALWAYS called — even for expired targets (dispatchAny
    // decides: live → pipeline, expired → kDropped in the dispatcher trace).
    // popFront(pe) removes BY IDENTITY so a cross-thread push between front
    // and pop cannot evict the wrong element (audit MUST-1 fix).
    while (m_queue->front(pe)) {
        m_dispatcher->dispatchAny(*pe); // friend: CtEventLoop reaches protected
        m_queue->popFront(pe);
        ++count;
    }

    // Dropped report: records kDropped in the queue's trace (overflow count).
    m_queue->popDropped();
    return count;
}

} // namespace crisp::event
