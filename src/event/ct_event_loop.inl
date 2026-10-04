// ct_event_loop.inl — template definitions for CtEventLoop.
//
// Included at the bottom of ct_event_loop.h. Template methods only
// (setDispatcher, injectEvent); the non-template machinery (exec, quit,
// routing) lives in ct_event_loop.cpp.

namespace crisp::event {

template <typename EventSet>
void CtEventLoop::setDispatcher(
    std::shared_ptr<CtDispatcher<EventSet>> dispatcher) {
    if (!dispatcher) {
        assert(false && "CtEventLoop::setDispatcher: null dispatcher");
        return;
    }

    // HARD CONTRACT (Rev 8, P2-4): one loop attaches one dispatcher — the
    // queue's EventSet must equal the dispatcher's EventSet. A second call
    // (including a different EventSet) is a programming error.
    if (m_hasDispatcher) {
        assert(false && "CtEventLoop::setDispatcher: dispatcher already set");
        return;
    }

    // Thread constraint (Rev 9, P2-4): the dispatcher must have been created
    // on the loop's thread. Without this, drain-time async broadcast would
    // trip the §8.2 Step 1 dispatcher-affinity assert (A-thread dispatcher,
    // B-thread loop).
    if (!dispatcher->assertSameThread()) {
        assert(false && "CtEventLoop::setDispatcher: dispatcher must be "
                        "created on the loop's thread");
        return;
    }

    // Lazily create the queue (Rev 5, P2-3): only this template instantiation
    // point knows EventSet. SHARED_PTR so the routing table can hold the
    // queue alive during a concurrent post (M2 decision, §7.11 contract #7).
    m_queue = std::make_shared<CtQueue<EventSet>>();
    m_dispatcher = std::move(dispatcher);
    m_hasDispatcher = true;

    // Register the queue in the thread→queue routing table so post/postTo
    // on OTHER threads can find it (§10.1 contract #1).
    detail::registerThreadQueue(m_affinityThread, m_queue);
}

template <typename EventSet, typename T>
void CtEventLoop::injectEvent(std::weak_ptr<CtReceiver> target,
                              CtEvent<T>&& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the injected event set");

    if (!m_hasDispatcher) {
        assert(false && "CtEventLoop::injectEvent: setDispatcher not called");
        return;
    }

    // eventSetId() assert before the downcast (Rev 8, P2-4): the
    // strong-typed push path bypasses pushAny's check, so a cross-set
    // downcast here would be unchecked UB.
    if (m_queue->eventSetId() != CtTypeKey<EventSet>::value()) {
        assert(false && "CtEventLoop::injectEvent: EventSet mismatch");
        return;
    }

    auto& concrete = static_cast<CtQueue<EventSet>&>(*m_queue);
    concrete.push(std::move(target), std::move(event));
}

} // namespace crisp::event
