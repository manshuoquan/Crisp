#pragma once

// CtQueue: per-thread, priority-aware async event queue (M2 async core).
//
// Part of the CRISP event subsystem (crisp::event). Implements
// docs/event-system/04-event-system-redesign.md §7.10 + §6.5: the pending-
// event boundary types (CtPendingEventBase/CtPendingEvent), the erased
// queue interface (CtQueueBase), the concrete priority-ordered CtQueue, and
// the erased payload holder (detail::CtAnyEventBase/CtAnyEventHolder).
// The queue is drained ONLY on its affinity thread (the CtEventLoop's
// thread); push/pushAny may be called from any thread (queue mutex).

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "ct_event_set.h"        // CtAnyEvent, CtEventSet
#include "ct_event.h"            // CtEvent, CtEventOrigin, CtTracePhase
#include "ct_receiver.h"         // CtReceiver (pending-event target)
#include "ct_thread_affinity.h"  // CtThreadId, CtThreadAffinity
#include "ct_trace.h"            // CtTrace
#include "ct_dispatch_context.h" // CtDispatchContext (queue is a friend)
#include "ct_type_key.h"         // CtTypeKey

namespace crisp::event::detail {
class CtAnyEventBase; ///< fwd: CtQueueBase::pushAny names it in a unique_ptr
}

namespace crisp::event {

/// Base of a pending (asynchronously queued) event: common delivery fields.
/// ABSTRACT base used ONLY via reference semantics (never stored by value);
/// CtPendingEvent<EventSet> (below) derives from it and is stored BY VALUE
/// in the queue's deque (§7.10 contract #3).
class CtPendingEventBase {
public:
    virtual ~CtPendingEventBase() = default;

    /// Delivery target; empty = broadcast (§8.2).
    const std::weak_ptr<CtReceiver>& target() const noexcept { return m_target; }

    /// True if this is a broadcast (target was EMPTY at push time).
    /// FIXED AT CONSTRUCTION (Rev 6, P1-1 + Rev 7, P1-2): a live expired()
    /// check cannot distinguish "broadcast, must deliver" from "target died,
    /// skip + kDropped" (empty and expired weak_ptrs both report
    /// expired()==true). The ctor sets it via
    /// `!std::weak_ptr<CtReceiver>{}.owner_before(target)` — the default
    /// empty weak_ptr is the strict-weak-order minimum, so the expression is
    /// true exactly when target is EMPTY.
    bool isBroadcast() const noexcept { return m_isBroadcast; }

    /// Ordering hint copied from CtEvent::priority() at push time — the
    /// event's uint64_t priority (§7.3), NOT CtHandlerPriority (§7.15).
    uint64_t priority() const noexcept { return m_priority; }

    /// Enqueue-order sequence number — SINGLE source of truth (Rev 8, P2-1):
    /// stamped by the queue's m_seqCounter at push time. Async events are
    /// NOT re-assigned at drain (§8.1 Step 3 async exception).
    uint64_t sequence() const noexcept { return m_sequence; }

    /// EventSet identity of the queue that CREATED this pending event
    /// (address-derived, §6.1). The drain side (dispatchAny) asserts this ==
    /// ITS OWN set id BEFORE static_casting the payload variant — a
    /// payloadTypeKey() check alone cannot detect a cross-set pending record
    /// when two sets share type T (cast would be UB).
    virtual uint64_t eventSetId() const noexcept = 0;

    /// CtTypeKey of the payload T (for the dispatcher's set-membership
    /// assert before it re-homes the variant into the pipeline).
    virtual uint64_t payloadTypeKey() const noexcept = 0;

    /// Pointer to the concrete CtAnyEvent (variant) inside the deque
    /// element. NON-const member function (Rev 12, P1-1): dispatchAny runs
    /// the §8.1 pipeline which resets accepted and passes a non-const
    /// CtEvent<T>& to handlers/filters.
    virtual void* payloadPtr() noexcept = 0;

protected:
    /// Construct the base with the delivery fields (called by the derived
    /// CtPendingEvent ctor; §7.10). `isBroadcast` is fixed here from the
    /// target's emptiness (Rev 6, P1-1 + Rev 7, P1-2): the default empty
    /// weak_ptr is the strict-weak-order minimum, so
    /// `!empty.owner_before(target)` is true exactly when target is EMPTY.
    CtPendingEventBase(std::weak_ptr<CtReceiver> target, uint64_t priority,
                       uint64_t sequence) noexcept
        : m_target(std::move(target)),
          m_isBroadcast(!std::weak_ptr<CtReceiver>{}.owner_before(m_target)),
          m_priority(priority),
          m_sequence(sequence) {}

    std::weak_ptr<CtReceiver> m_target;  ///< empty = broadcast (§8.2)
    bool m_isBroadcast = false;          ///< fixed at construction (Rev 6, P1-1)
    uint64_t m_priority = 0;             ///< event priority hint (§10.3)
    uint64_t m_sequence = 0;             ///< enqueue-order sequence (Rev 8, P2-1)
};

/// One pending (asynchronously queued) event: the event plus its delivery
/// target. Concrete subclass stored BY VALUE in the queue (§7.10 contract #2).
template <typename EventSet>
struct CtPendingEvent : CtPendingEventBase {
    using any_event = typename EventSet::any_type;

    /// Construct from a concrete event + target (called by CtQueue::push).
    /// Sets m_isBroadcast from the pushed target's emptiness and copies
    /// priority/sequence (stamped by the queue, §7.10 contract #9).
    template <typename T>
    CtPendingEvent(const std::weak_ptr<CtReceiver>& target, CtEvent<T>&& event,
                   uint64_t priority, uint64_t sequence)
        : CtPendingEventBase(target, priority, sequence),
          m_payload(std::move(event)) {}

    /// Construct from an ALREADY-RE-HOMED variant (pushAny path, §7.10
    /// contract #7): the erased holder's variant is moved in directly — no
    /// std::visit needed for the move itself.
    CtPendingEvent(const std::weak_ptr<CtReceiver>& target,
                   any_event&& payload, uint64_t priority, uint64_t sequence)
        : CtPendingEventBase(target, priority, sequence),
          m_payload(std::move(payload)) {}

    uint64_t eventSetId() const noexcept override {
        return CtTypeKey<EventSet>::value();
    }

    uint64_t payloadTypeKey() const noexcept override {
        return std::visit(
            [](const auto& ev) {
                using EventT = std::decay_t<decltype(ev)>;
                using T = typename EventT::payload_type;
                return CtTypeKey<T>::value();
            },
            m_payload);
    }

    void* payloadPtr() noexcept override { return &m_payload; }

    any_event m_payload; ///< CtAnyEvent value (no heap)
};

/// Type-erased base of CtQueue<EventSet>. Lets CtEventLoop and CtEventSource
/// hold a queue WITHOUT depending on the concrete EventSet (Rev 3, N4). All
/// virtual — the sync hot path never touches a queue, so virtual dispatch
/// here is off the hot path.
class CtQueueBase {
public:
    virtual ~CtQueueBase() = default;

    /// Push an erased payload (cross-thread safe). Returns true if accepted,
    /// false on cross-EventSet rejection (counted as dropped, §10.1 contract #5).
    /// Ownership of `payload` transfers to the queue. Async path only — the
    /// ONE heap allocation (the holder) was already spent by the caller.
    virtual bool pushAny(std::unique_ptr<detail::CtAnyEventBase> payload,
                         std::weak_ptr<CtReceiver> target) = 0;

    /// Peek the front pending event (ONLY on the affinity thread; asserts).
    /// On success sets `out` to the front deque element's base subobject,
    /// records kQueued in the queue's own trace, returns true; false when
    /// empty.
    virtual bool front(CtPendingEventBase*& out) = 0;

    /// Remove the front element (ONLY on the affinity thread; asserts).
    /// No trace, no allocation.
    virtual void popFront() = 0;

    /// Drain-and-drop report: number of events dropped since the last call
    /// (overflow at push time). Records kDropped in the queue's own trace
    /// for the reported count (affinity thread).
    virtual std::size_t popDropped() = 0;

    /// Number of queued events (approximate; diagnostics).
    virtual std::size_t size() const noexcept = 0;

    /// The thread that may drain this queue.
    virtual CtThreadId affinityThread() const noexcept = 0;

    /// Address-derived identity of this queue's EventSet (§6.1 scheme).
    virtual uint64_t eventSetId() const noexcept = 0;

    /// Own trace store (per-queue, owned by the affinity thread).
    virtual CtTrace& trace() noexcept = 0;

    /// Block until an event is available or the queue is destroyed (§14 #8
    /// ruling: M2 uses a condition variable, pure C++17 — no platform
    /// dependency). Called by CtEventLoop::exec when the queue is empty;
    /// push/pushAny notify. Spurious wakeups are allowed (callers re-check).
    /// Affinity-thread only (debug assert).
    virtual void waitForEvent() = 0;

    /// Wake the affinity thread unconditionally (used by CtEventLoop::quit
    /// so exec exits its wait even when no event is pushed).
    virtual void notifyAll() = 0;

    /// Close the queue: stop accepting new events and unblock any pending
    /// waitForEvent (the exec loop exits via its quit check). Called by
    /// CtEventLoop::quit and ~CtEventLoop.
    virtual void close() = 0;

    /// True if the queue has been closed (CtEventLoop::quit / destruction).
    virtual bool isClosed() const noexcept = 0;
};

/// A thread-affined, priority-aware queue of type-safe events (M2 async
/// core storage). Events are stored as CtPendingEvent<EventSet> — value
/// semantics, zero heap allocation per stored event (§7.4).
template <typename EventSet>
class CtQueue : public CtQueueBase {
public:
    using any_event = typename EventSet::any_type;
    using pending_event = CtPendingEvent<EventSet>;

    /// Construct bound to the CURRENT thread (embedded CtThreadAffinity).
    CtQueue() = default;

    CtQueue(const CtQueue&) = delete;             ///< per-thread identity
    CtQueue& operator=(const CtQueue&) = delete;

    /// Push an event to `target` (cross-thread safe; takes the queue mutex).
    /// Returns TRUE if accepted (queued). On overflow the TAIL is dropped
    /// (lowest priority, oldest among equals — deque tail, §10.3 #3) to make
    /// room; the drop is counted and reported via popDropped() (Rev 12, P3
    /// ruling: drop-tail, not reject-new). NEVER records trace itself
    /// (contract #6 — kQueued/kDropped are recorded by the affinity thread).
    template <typename T>
    bool push(const std::weak_ptr<CtReceiver>& target, CtEvent<T>&& event);

    /// Erased push (Rev 5, P1-2/N-3 + Rev 10, P1-A): concrete override of
    /// CtQueueBase::pushAny. Asserts eventSetId() == EventSet key (cross-set
    /// rejection → false + dropped count), then static_casts the holder to
    /// detail::CtAnyEventHolder<EventSet> and re-homes its variant into a
    /// pending_event. SEQUENCE SINGLE SOURCE (Rev 10, P1-A): stamped from
    /// THIS queue's m_seqCounter — never read from the sender's CtEvent.
    bool pushAny(std::unique_ptr<detail::CtAnyEventBase> payload,
                 std::weak_ptr<CtReceiver> target) override;

    /// Peek front (affinity thread; records kQueued on success, §7.10 contract #6).
    bool front(CtPendingEventBase*& out) override;

    /// Remove front (affinity thread; no trace).
    void popFront() override;

    /// Number of queued events (approximate; diagnostics).
    std::size_t size() const noexcept override;

    /// True if the affinity thread may drain this queue.
    CtThreadId affinityThread() const noexcept override;

    /// Own trace store (Rev 4, P1-1): the OWNING thread records async
    /// phases here during drain/overflow handling. Never written by push.
    CtTrace& trace() noexcept override;

    /// Drain-and-drop report: events dropped since the last call (overflow).
    /// Owned by the affinity thread; records kDropped for the count.
    std::size_t popDropped() override;

    /// Block until an event is available (§14 #8, condition-variable wait).
    /// Affinity-thread only (debug assert); spurious wakeups allowed.
    void waitForEvent() override;

    /// Wake the affinity thread unconditionally (CtEventLoop::quit path).
    void notifyAll() override;

    /// Close the queue: set the closed flag and wake all waiters.
    void close() override;

    /// True if the queue has been closed.
    bool isClosed() const noexcept override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    uint64_t eventSetId() const noexcept override {
        return CtTypeKey<EventSet>::value();
    }

private:
    /// Notify the affinity thread that an event was pushed (§14 #8).
    /// Called under m_mutex after a successful push/pushAny.
    void notifyEvent();

    // Priority-aware insertion keeps events ordered by §10.3 rules:
    // higher priority first; equal priority keeps insertion order (stable).
    std::deque<pending_event> m_events;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;              ///< wait/notify pair (§14 #8)
    bool m_closed = false;                     ///< closed by CtEventLoop::quit
    CtThreadAffinity m_affinity;               ///< embedded (Rev 4, P2-9)
    std::size_t m_dropped = 0;                 ///< dropped count since last popDropped()
    CtTrace m_trace;                           ///< own trace store (Rev 9, P2-2)
    std::uint64_t m_seqCounter = 0;            ///< enqueue-order counter (Rev 9, P1-1)
    static constexpr std::size_t kMaxQueued = 8192; ///< overflow → drop tail
};

} // namespace crisp::event

namespace crisp::event::detail {

/// Type-erased payload interface (NON-template; Rev 5, N-3). The boundary
/// types (CtQueueBase) cannot depend on a concrete EventSet, so pushAny
/// carries this abstract holder. The concrete templated CtQueue<EventSet>
/// static_casts to its own CtAnyEventHolder<EventSet> AFTER an eventSetId()
/// assert (never otherwise — same discipline as §6.5 erased handlers).
class CtAnyEventBase {
public:
    virtual ~CtAnyEventBase() = default;

    /// CtTypeKey<T> of the held payload (for key-match assert).
    virtual uint64_t payloadTypeKey() const noexcept = 0;

    /// Identity of the EventSet that CREATED this holder (Rev 7, P2-5:
    /// address-derived, §6.1). Two sets that share payload type T have
    /// different identities; a payloadTypeKey() check alone cannot detect a
    /// cross-set cast when T is shared (the static_cast would be UB).
    virtual uint64_t eventSetId() const noexcept = 0;

    /// Pointer to the held variant; the receiving CtQueue<EventSet>
    /// static_casts to `CtAnyEventHolder<EventSet>::any_event` after assert.
    /// NON-const member function (Rev 12, P1-1): the dispatch pipeline
    /// mutates the event (accepted reset, §8.1 Step 3).
    virtual void* payloadPtr() noexcept = 0;
};

/// Concrete holder: owns a CtAnyEvent (variant) through the ONE heap
/// allocation made at the async-path boundary (post / event source
/// injection). pushAny transfers ownership to the queue. Async-path-only
/// allocation — the sync dispatch hot path never constructs one.
template <typename EventSet>
class CtAnyEventHolder final : public CtAnyEventBase {
public:
    using any_event = typename EventSet::any_type;

    template <typename T>
    explicit CtAnyEventHolder(CtEvent<T>&& event) : m_value(std::move(event)) {}

    uint64_t payloadTypeKey() const noexcept override {
        return std::visit(
            [](const auto& ev) {
                using EventT = std::decay_t<decltype(ev)>;
                using T = typename EventT::payload_type;
                return CtTypeKey<T>::value();
            },
            m_value);
    }

    /// Address-derived identity of EventSet (Rev 7, P2-5; §6.1 scheme).
    uint64_t eventSetId() const noexcept override {
        return CtTypeKey<EventSet>::value();
    }

    void* payloadPtr() noexcept override { return &m_value; }

    /// Access the held variant (concrete EventSet known at this site).
    /// const and non-const accessors: CtQueue<EventSet>::pushAny must MOVE
    /// the variant into a CtPendingEvent — a const& cannot move (Rev 6, P2-2).
    const any_event& value() const noexcept { return m_value; }
    any_event& value() noexcept { return m_value; }

private:
    any_event m_value; ///< std::variant<CtEvent<Ts>...> (value semantics)
};

/// Thread→queue routing table (async path, §10.1). CtEventLoop registers
/// its queue at setDispatcher time and unregisters at destruction;
/// post/postTo look up the target queue here. Implemented in
/// ct_event_loop.cpp (non-template mechanism lives in .cpp, §5.3).
void registerThreadQueue(CtThreadId tid, const std::shared_ptr<CtQueueBase>& queue);
void unregisterThreadQueue(CtThreadId tid);

/// Enqueue an erased payload to the queue affined to `targetThread`.
/// Returns true if accepted (queued), false if no queue is registered for
/// that thread or the queue rejected it (cross-EventSet).
bool postErased(CtThreadId targetThread,
                std::unique_ptr<CtAnyEventBase> payload,
                std::weak_ptr<CtReceiver> target);

} // namespace crisp::event::detail

#include "ct_queue.inl"
