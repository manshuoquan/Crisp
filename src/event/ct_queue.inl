// ct_queue.inl — template definitions for CtQueue<EventSet>.
//
// Included at the bottom of ct_queue.h. The header/.inl split keeps the
// declarations readable; the template machinery lives here (language
// exception to the strict .h/.cpp split, AGENTS.md).

#include <algorithm>

namespace crisp::event {

namespace detail {
namespace {

/// True if a pending event should be placed BEFORE `cur` (higher priority
/// first; equal priority keeps insertion order — stable by sequence).
bool pendingBefore(const CtPendingEventBase& lhs, const CtPendingEventBase& rhs) noexcept {
    if (lhs.priority() != rhs.priority())
        return lhs.priority() > rhs.priority();
    return lhs.sequence() < rhs.sequence();
}

} // namespace (anonymous)
} // namespace detail

template <typename EventSet>
template <typename T>
bool CtQueue<EventSet>::push(const std::weak_ptr<CtReceiver>& target,
                             CtEvent<T>&& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the queue's event set");

    std::lock_guard<std::mutex> lock(m_mutex);

    // Closed queue: reject (loop is quitting; the caller sees kUnhandled).
    if (m_closed)
        return false;

    // Sequence: stamped under m_mutex — enqueue-order single source (§7.10
    // contract #9). The pending record carries the queue-stamped value; the
    // sender's CtEvent::sequence() is deliberately NOT used.
    const uint64_t seq = ++m_seqCounter;
    const uint64_t prio = event.priority();

    if (m_events.size() >= kMaxQueued) {
        // Overflow (Rev 12, P3 ruling): drop the TAIL — lowest priority,
        // and among equals the one with the LARGEST sequence (the deque is
        // ordered high-priority-first, equal priority by ascending
        // sequence, so the tail is the newest among equals — audit WARN
        // fix: the old comment said "oldest", which contradicted the
        // ordering). The dropped event is counted; the affinity thread
        // reports it via popDropped() (records kDropped there).
        ++m_dropped;
        m_events.pop_back();
    }

    pending_event pe(target, std::move(event), prio, seq);
    // Priority insertion: upper_bound over the deque with the ordering
    // predicate (higher priority first; stable by sequence for equals).
    auto it = std::upper_bound(
        m_events.begin(), m_events.end(), pe,
        [](const pending_event& lhs, const pending_event& rhs) {
            return detail::pendingBefore(lhs, rhs);
        });
    m_events.insert(it, std::move(pe));
    notifyEvent(); // wake the affinity thread's exec wait (§14 #8)
    return true;
}

template <typename EventSet>
bool CtQueue<EventSet>::pushAny(std::unique_ptr<detail::CtAnyEventBase> payload,
                                std::weak_ptr<CtReceiver> target) {
    if (!payload)
        return false;

    // Cross-EventSet guard (Rev 7, P2-5): the holder carries the EventSet
    // identity of its CREATOR. A payloadTypeKey() check alone cannot detect
    // a cross-set cast when two sets share type T — the static_cast would be
    // UB. Reject + count as dropped (Rev 8, P3: cross-set and overflow both
    // count into m_dropped, §10.1 contract #5).
    if (payload->eventSetId() != CtTypeKey<EventSet>::value()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_dropped;
        return false;
    }

    // Re-home the variant into a pending_event (Rev 5, P1-2/N-3): the
    // holder owns the ONE async-path heap allocation; the deque element is
    // a value (zero further allocation).
    auto* holder = static_cast<detail::CtAnyEventHolder<EventSet>*>(payload.get());

    std::lock_guard<std::mutex> lock(m_mutex);
    // Closed queue: reject (loop is quitting; the caller sees kUnhandled).
    if (m_closed)
        return false;
    const uint64_t seq = ++m_seqCounter;
    // Priority read from the variant — the ONLY source (Rev 10, P1-A: no
    // separate priority parameter).
    const uint64_t prio = std::visit(
        [](const auto& ev) { return ev.priority(); }, holder->value());

    if (m_events.size() >= kMaxQueued) {
        ++m_dropped;
        m_events.pop_back();
    }

    // Re-home: move the variant out of the holder into the pending element,
    // carrying target + queue-stamped sequence.
    pending_event pe(target, std::move(holder->value()), prio, seq);
    auto it = std::upper_bound(
        m_events.begin(), m_events.end(), pe,
        [](const pending_event& lhs, const pending_event& rhs) {
            return detail::pendingBefore(lhs, rhs);
        });
    m_events.insert(it, std::move(pe));
    notifyEvent(); // wake the affinity thread's exec wait (§14 #8)
    return true;
}

template <typename EventSet>
bool CtQueue<EventSet>::front(CtPendingEventBase*& out) {
    // Affinity-thread only (debug assert; release degrades to empty).
    if (!m_affinity.isOnThread()) {
        assert(false && "CtQueue::front on non-affinity thread");
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_events.empty())
        return false;
    out = &m_events.front();

    // §7.10 contract #6: kQueued recorded HERE (the queue is the record body;
    // affinity thread, no data race). Records the REAL payload name via the
    // pending event's typeName() (M4, 04 Rev 10 P3-2) instead of the old
    // "<async>" placeholder.
    CtDispatchContext ctx;
    ctx.m_phase = CtTracePhase::kQueued;
    m_trace.record(out->typeName(), CtTracePhase::kQueued, ctx, 0);
    return true;
}

template <typename EventSet>
void CtQueue<EventSet>::popFront(CtPendingEventBase* which) {
    if (!m_affinity.isOnThread()) {
        assert(false && "CtQueue::popFront on non-affinity thread");
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!which)
        return;
    // Remove BY IDENTITY (address), not position: between front() and here a
    // cross-thread push may have inserted a higher-priority element ahead of
    // the dispatched one (deque reference/pointer stability is a standard
    // guarantee for non-erased elements). Linear scan is O(n) but drain is
    // NOT the sync hot path; n ≤ kMaxQueued.
    for (auto it = m_events.begin(); it != m_events.end(); ++it) {
        if (std::addressof(*it) == which) {
            m_events.erase(it);
            return;
        }
    }
    // Element not found: the drain loop's front() was already invalidated
    // (e.g. a concurrent popFront on the wrong thread — a contract
    // violation). Nothing to remove.
}

template <typename EventSet>
std::size_t CtQueue<EventSet>::size() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events.size();
}

template <typename EventSet>
CtThreadId CtQueue<EventSet>::affinityThread() const noexcept {
    return m_affinity.thread();
}

template <typename EventSet>
CtTrace& CtQueue<EventSet>::trace() noexcept {
    return m_trace;
}

template <typename EventSet>
std::size_t CtQueue<EventSet>::popDropped() {
    // Affinity thread only (debug assert; release degrades to 0).
    if (!m_affinity.isOnThread()) {
        assert(false && "CtQueue::popDropped on non-affinity thread");
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t dropped = m_dropped;
    m_dropped = 0;
    if (dropped > 0) {
        // §7.10 contract #6: kDropped recorded here (affinity thread).
        // Overflow drops carry only a COUNT — no concrete event object
        // survives (the tail was erased at push time), so typeName() is
        // unavailable; the "<async>" placeholder is intentional (audit
        // WARN: documented trade-off, M4 typeName covers front()/dispatchAny
        // drops which DO have the object).
        CtDispatchContext ctx;
        ctx.m_phase = CtTracePhase::kDropped;
        m_trace.record("<async>", CtTracePhase::kDropped, ctx, 0);
    }
    return dropped;
}

template <typename EventSet>
void CtQueue<EventSet>::recordAsyncDrop(uint64_t payloadTypeKey) {
    // Affinity thread only (debug assert; release degrades to no-op).
    if (!m_affinity.isOnThread()) {
        assert(false && "CtQueue::recordAsyncDrop on non-affinity thread");
        return;
    }
    CtDispatchContext ctx;
    ctx.m_phase = CtTracePhase::kDropped;
    m_trace.record(typeNameFor(payloadTypeKey), CtTracePhase::kDropped, ctx, 0);
}

namespace detail {
namespace {

/// Recursive pack walker: return the traits name of the first payload type
/// in Ts... whose CtTypeKey matches `key`; "<unregistered>" if none match.
template <typename... Ts>
struct KeyNameLookup;

template <>
struct KeyNameLookup<> {
    static std::string_view find(uint64_t) noexcept {
        return "<unregistered>";
    }
};

template <typename T, typename... Rest>
struct KeyNameLookup<T, Rest...> {
    static std::string_view find(uint64_t key) noexcept {
        if (CtTypeKey<T>::value() == key)
            return CtEventTraits<T>::name;
        return KeyNameLookup<Rest...>::find(key);
    }
};

/// Unpack a CtEventSet<Ts...> into the pack walker.
template <typename EventSet>
struct KeyNameLookupForSet;

template <typename... Ts>
struct KeyNameLookupForSet<CtEventSet<Ts...>> {
    static std::string_view find(uint64_t key) noexcept {
        return KeyNameLookup<Ts...>::find(key);
    }
};

} // namespace (anonymous)
} // namespace detail

template <typename EventSet>
std::string_view CtQueue<EventSet>::typeNameFor(
    uint64_t payloadTypeKey) const noexcept {
    // Map a CtTypeKey to its traits name for the drop record. The key space
    // is the EventSet's own payloads (bounded, compile-time known); a
    // foreign/unknown key falls back to a non-empty placeholder. CtEvent<T>
    // is NOT default-constructible, so the lookup walks the type pack
    // directly (no variant default-construction).
    return detail::KeyNameLookupForSet<EventSet>::find(payloadTypeKey);
}


template <typename EventSet>
void CtQueue<EventSet>::waitForEvent() {
    // Affinity-thread only (debug assert; release degrades to no-op).
    if (!m_affinity.isOnThread()) {
        assert(false && "CtQueue::waitForEvent on non-affinity thread");
        return;
    }
    // Predicate: event available OR queue closed (quit wakeup). The closed
    // flag prevents both the lost-wakeup race (push before wait) and the
    // re-sleep-on-quit problem (predicate false after notify_all on an
    // empty queue).
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_events.empty() || m_closed; });
}

template <typename EventSet>
void CtQueue<EventSet>::notifyAll() {
    m_cv.notify_all();
}

template <typename EventSet>
void CtQueue<EventSet>::close() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_cv.notify_all();
}

template <typename EventSet>
void CtQueue<EventSet>::notifyEvent() {
    m_cv.notify_one();
}

} // namespace crisp::event
