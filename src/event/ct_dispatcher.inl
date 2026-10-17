// ct_dispatcher.inl — template definitions for CtDispatcher<EventSet>.
//
// Included at the bottom of ct_dispatcher.h. The header/.inl split keeps the
// declarations readable; the template machinery (language exception to the
// strict .h/.cpp split, AGENTS.md) lives here.

namespace crisp::event {

namespace detail {
namespace {

/// Erase a typed CtHandler<T> into detail::ErasedHandler (§6.5).
template <typename T>
ErasedHandler makeErasedHandler(CtHandler<T> handler) {
    return [h = std::move(handler)](void* ev) {
        h(*static_cast<CtEvent<T>*>(ev));
    };
}

/// Erase a typed CtFilter<T> into detail::ErasedFilter (§6.5).
template <typename T>
ErasedFilter makeErasedFilter(CtFilter<T> filter) {
    return [f = std::move(filter)](void* ev) {
        return f(*static_cast<CtEvent<T>*>(ev));
    };
}

/// Erase a typed CtObserver<T> into detail::ErasedObserver (§6.5).
template <typename T>
ErasedObserver makeErasedObserver(CtObserver<T> observer) {
    return [o = std::move(observer)](const void* ev, const CtDispatchContext& ctx) {
        o(*static_cast<const CtEvent<T>*>(ev), ctx);
    };
}

/// Compare two weak_ptrs by emptiness using owner_before (strict weak order,
/// §6.1): `empty.owner_before(target)` is true iff `target` is NON-empty.
template <typename U>
bool isEmptyWeak(const std::weak_ptr<U>& w) noexcept {
    return !std::weak_ptr<U>{}.owner_before(w);
}

} // namespace (anonymous)
} // namespace detail

template <typename EventSet>
template <typename T>
CtHandlerToken CtDispatcher<EventSet>::addHandler(
    const std::shared_ptr<CtReceiver>& receiver, CtHandler<T> handler,
    std::string_view name) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    if (!handler)
        return {}; // empty handler: reject with a null token

    const CtHandlerId id = nextHandlerId();
    const uint64_t key = CtTypeKey<T>::value();

    auto tables = std::make_shared<detail::CtDispatchTables>(*snapshot());
    detail::CtDispatchTables::HandlerEntry entry;
    entry.receiver = receiver ? std::weak_ptr<CtReceiver>(receiver) : std::weak_ptr<CtReceiver>{};
    entry.fn = detail::makeErasedHandler<T>(std::move(handler));
    entry.id = id;
    entry.priority = CtHandlerPriority::kNormal;
    entry.name = name;
    tables->handlers[key].push_back(std::move(entry));
    ++tables->version;

    // Create the token state BEFORE publish (audit WARN fix): if makeTokenState
    // throws (vector growth bad_alloc), the registration is NOT yet visible —
    // no orphaned registration without an owning token.
    const std::shared_ptr<detail::CtTokenState> state = makeTokenState(id);

    std::lock_guard<std::mutex> lock(m_writeMutex);
    publish(std::move(tables));

    return CtHandlerToken(state);
}

template <typename EventSet>
template <typename T>
CtHandlerToken CtDispatcher<EventSet>::addFilter(CtFilter<T> filter,
                                                 std::string_view name) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    if (!filter)
        return {}; // empty filter: reject with a null token

    const CtHandlerId id = nextHandlerId();
    const uint64_t key = CtTypeKey<T>::value();

    auto tables = std::make_shared<detail::CtDispatchTables>(*snapshot());
    detail::CtDispatchTables::FilterEntry entry;
    entry.fn = detail::makeErasedFilter<T>(std::move(filter));
    entry.filterSeq = 0; // M1: user order not exposed; registration order
    entry.id = id;
    entry.name = name;
    tables->filters[key].push_back(std::move(entry));
    ++tables->version;

    // Token state BEFORE publish (audit WARN fix): no orphaned registration.
    const std::shared_ptr<detail::CtTokenState> state = makeTokenState(id);

    std::lock_guard<std::mutex> lock(m_writeMutex);
    publish(std::move(tables));

    return CtHandlerToken(state);
}

template <typename EventSet>
template <typename T>
CtHandlerToken CtDispatcher<EventSet>::addObserver(CtObserver<T> observer,
                                                   std::string_view name) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    if (!observer)
        return {}; // empty observer: reject with a null token

    const CtHandlerId id = nextHandlerId();
    const uint64_t key = CtTypeKey<T>::value();

    auto tables = std::make_shared<detail::CtDispatchTables>(*snapshot());
    detail::CtDispatchTables::ObserverEntry entry;
    entry.fn = detail::makeErasedObserver<T>(std::move(observer));
    entry.id = id;
    entry.name = name;
    tables->observers[key].push_back(std::move(entry));
    ++tables->version;

    // Token state BEFORE publish (audit WARN fix): no orphaned registration.
    const std::shared_ptr<detail::CtTokenState> state = makeTokenState(id);

    std::lock_guard<std::mutex> lock(m_writeMutex);
    publish(std::move(tables));

    return CtHandlerToken(state);
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::send(
    const std::shared_ptr<CtReceiver>& receiver, CtEvent<T>& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    // §8.1 Step 1 — dual thread-affinity assertion.
    if (!receiver || !receiver->assertSameThread() || !this->assertSameThread()) {
        // Debug: assert() fires first; release: degrade with a kDropped
        // record (semantics: not delivered; see coder report, kWrongThread
        // trace note) and return kUnhandled.
        m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDropped,
                       CtDispatchContext{}, 0);
        return CtDispatchResult::kUnhandled;
    }

    // §9.2 契约 #3 — release: empty guard (member is a padding char);
    // debug: depth counter guard.
#if defined(NDEBUG)
    CtInDispatchGuard guard;
#else
    CtInDispatchGuard guard(m_inDispatch);
#endif

    // §8.1 Step 2 — one COW snapshot for the whole pipeline.
    const auto tables = snapshot();

    // §8.1 Step 3 — sequence + accepted reset + kDispatched.
    event.setSequence(++m_nextSequence);
    event.accepted = false;
    m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDispatched,
                   CtDispatchContext{}, receiver->objectId());

    // §8.1 Step 4 — observers(pre).
    runObservers<T>(tables.get(), event, receiver->objectId());

    // §8.1 Step 5 — filters.
    const CtDispatchResult filterResult =
        runFilters<T>(tables.get(), event, receiver->objectId());
    if (filterResult != CtDispatchResult::kUnhandled)
        return filterResult;

    // §8.1 Step 6 — handlers (directed).
    const CtDispatchResult handlerResult =
        runDirectedHandlers<T>(tables.get(), receiver, event);

    // §8.1 Step 7 — observers(post). Not executed when a filter short-circuited.
    if (handlerResult != CtDispatchResult::kNoReceiver)
        runObservers<T>(tables.get(), event, receiver->objectId());

    return handlerResult;
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::broadcast(CtEvent<T>& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    // §8.2 Step 1 — dispatcher thread-affinity assertion.
    if (!this->assertSameThread()) {
        m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDropped,
                       CtDispatchContext{}, 0);
        return CtDispatchResult::kUnhandled;
    }

    // §9.2 契约 #3 — release: empty guard (member is a padding char);
    // debug: depth counter guard.
#if defined(NDEBUG)
    CtInDispatchGuard guard;
#else
    CtInDispatchGuard guard(m_inDispatch);
#endif

    // §8.2 Step 2 — one COW snapshot for the whole pipeline.
    const auto tables = snapshot();

    // §8.2 Step 3 — sequence + accepted reset + kDispatched (receiverId=0).
    event.setSequence(++m_nextSequence);
    event.accepted = false;
    m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDispatched,
                   CtDispatchContext{}, 0);

    // §8.2 Step 4 — observers(pre), receiverId=0.
    runObservers<T>(tables.get(), event, 0);

    // §8.2 Step 5 — filters.
    const CtDispatchResult filterResult =
        runFilters<T>(tables.get(), event, 0);
    if (filterResult != CtDispatchResult::kUnhandled)
        return filterResult;

    // §8.2 Step 6 — handlers (broadcast: all live receivers + broadcast
    // entries, accept OR).
    const CtDispatchResult handlerResult =
        runBroadcastHandlers<T>(tables.get(), event);

    // §8.2 Step 7 — observers(post), receiverId=0.
    if (handlerResult != CtDispatchResult::kNoReceiver)
        runObservers<T>(tables.get(), event, 0);

    return handlerResult;
}

template <typename EventSet>
void CtDispatcher<EventSet>::dispatchAny(CtPendingEventBase& pending) {
    // §7.11 契约 #2: called by CtEventLoop::exec drain on the affinity
    // thread. The dispatcher's thread == the queue's thread by the
    // setDispatcher contract (Rev 9, P2-4), so no thread assert here.
    //
    // EventSet identity assert (§6.5): the pending event MUST have been
    // created by a queue of THIS EventSet — a payloadTypeKey() check alone
    // cannot detect a cross-set record when two sets share type T (the
    // static_cast below would be UB).
    assert(pending.eventSetId() == CtTypeKey<EventSet>::value());
    if (pending.eventSetId() != CtTypeKey<EventSet>::value())
        return;

    // Directed + target expired: kDropped in the DISPATCHER trace
    // (§7.11 契约 #2 ③ — dispatchAny cannot reach the queue's m_trace).
    // Broadcast (empty target) is always delivered regardless of liveness.
    std::shared_ptr<CtReceiver> target;
    if (!pending.isBroadcast()) {
        target = pending.target().lock();
        if (!target) {
            // Records the REAL payload name (M4, 04 Rev 10 P3-2) instead of
            // the old "<async>" placeholder.
            m_trace.record(pending.typeName(), CtTracePhase::kDropped,
                           CtDispatchContext{}, 0);
            return;
        }
    }

    // Re-home the variant and run the §8.1/§8.2 pipeline. One COW snapshot
    // per pending event (same discipline as send/broadcast, §8.1 Step 2).
    auto* anyPtr = static_cast<typename EventSet::any_type*>(pending.payloadPtr());
    std::visit(
        [&](auto& ev) {
            using EventT = std::decay_t<decltype(ev)>;
            using T = typename EventT::payload_type;
            static_assert(EventSet::template contains<T>,
                          "pending payload is not a member of the "
                          "dispatcher's event set");

            const auto tables = snapshot();
            const uint64_t receiverId =
                pending.isBroadcast() ? 0 : target->objectId();

            // §8.1 Step 3 ASYNC EXCEPTION (Rev 8, P2-1 / Rev 10, P1-1):
            // accepted is reset, kDispatched is recorded with the
            // QUEUE-STAMPED enqueue order — the sequence is NOT re-assigned
            // (async events never go through the sync counter).
            ev.accepted = false;
            m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDispatched,
                           CtDispatchContext{}, receiverId);

            // §8.1 Step 4 — observers(pre).
            runObservers<T>(tables.get(), ev, receiverId);

            // §8.1 Step 5 — filters.
            const CtDispatchResult filterResult =
                runFilters<T>(tables.get(), ev, receiverId);
            if (filterResult != CtDispatchResult::kUnhandled)
                return;

            // §8.1 Step 6 — handlers (directed or broadcast).
            CtDispatchResult handlerResult = CtDispatchResult::kUnhandled;
            if (pending.isBroadcast()) {
                handlerResult = runBroadcastHandlers<T>(tables.get(), ev);
            } else {
                handlerResult =
                    runDirectedHandlers<T>(tables.get(), target, ev);
            }

            // §8.1 Step 7 — observers(post), skipped when a filter
            // short-circuited (handled above by early return).
            if (handlerResult != CtDispatchResult::kNoReceiver)
                runObservers<T>(tables.get(), ev, receiverId);
        },
        *anyPtr);
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::post(
    const std::shared_ptr<CtReceiver>& receiver, CtEvent<T>&& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    // §10.1 契约 #1: route to the receiver's affinity thread's loop queue.
    if (!receiver)
        return CtDispatchResult::kUnhandled;

    auto holder =
        std::make_unique<detail::CtAnyEventHolder<EventSet>>(std::move(event));
    const bool accepted = detail::postErased(
        receiver->affinityThread(), std::move(holder),
        std::weak_ptr<CtReceiver>(receiver));

    // §10.1 契约 #3: accepted (queued) → kHandled; no loop/overflow/
    // cross-set → kUnhandled. NO trace phase recorded here (§7.8 契约 #8).
    return accepted ? CtDispatchResult::kHandled : CtDispatchResult::kUnhandled;
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::postTo(CtThreadId targetThread,
                                                CtEvent<T>&& event) {
    static_assert(CtEventTraits<T>::kIsValid,
                  "CtEvent<T> requires CRISP_EVENT_TYPE(T, ...)");
    static_assert(EventSet::template contains<T>,
                  "T is not a member of the dispatcher's event set");

    // §10.1 契约 #2: explicit target thread; the pending record carries an
    // EMPTY target (broadcast delivery at drain, §8.2).
    auto holder =
        std::make_unique<detail::CtAnyEventHolder<EventSet>>(std::move(event));
    const bool accepted = detail::postErased(
        targetThread, std::move(holder), std::weak_ptr<CtReceiver>{});

    return accepted ? CtDispatchResult::kHandled : CtDispatchResult::kUnhandled;
}

template <typename EventSet>
template <typename T>
void CtDispatcher<EventSet>::runObservers(
    const detail::CtDispatchTables* tables, const CtEvent<T>& event,
    uint64_t receiverId) {
    const auto it = tables->observers.find(CtTypeKey<T>::value());
    if (it == tables->observers.end())
        return;
    // handlerCount = live handlers for THIS event type in the snapshot
    // (audit WARN fix: tables->handlers.size() was the number of REGISTERED
    // TYPES, not handlers — observable data was wrong).
    const auto handlerIt = tables->handlers.find(CtTypeKey<T>::value());
    const std::size_t handlerCount =
        handlerIt == tables->handlers.end() ? 0 : handlerIt->second.size();
    for (const auto& observer : it->second) {
        CtDispatchContext ctx; // private ctor; CtDispatcher is a friend
        ctx.m_phase = CtTracePhase::kObserved;
        ctx.m_receiverId = receiverId;
        ctx.m_filterIndex = 0;
        ctx.m_handlerCount = handlerCount;
        const auto start = std::chrono::steady_clock::now();
        observer.fn(&event, ctx);
        ctx.m_elapsed = std::chrono::steady_clock::now() - start;
        m_trace.record(CtEventTraits<T>::name, CtTracePhase::kObserved,
                       ctx, receiverId);
    }
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::runFilters(
    const detail::CtDispatchTables* tables, CtEvent<T>& event,
    uint64_t receiverId) {
    const auto it = tables->filters.find(CtTypeKey<T>::value());
    if (it == tables->filters.end())
        return CtDispatchResult::kUnhandled; // no filters: pass through

    uint64_t filterIndex = 1; // 1-based execution ordinal (§8.3)
    for (const auto& filter : it->second) {
        CtDispatchContext ctx;
        ctx.m_phase = CtTracePhase::kFiltered;
        ctx.m_receiverId = receiverId;
        ctx.m_filterIndex = filterIndex;
        const auto start = std::chrono::steady_clock::now();
        const bool intercepted = filter.fn(&event);
        ctx.m_elapsed = std::chrono::steady_clock::now() - start;
        if (intercepted) {
            m_trace.record(CtEventTraits<T>::name, CtTracePhase::kFiltered,
                           ctx, receiverId);
            return CtDispatchResult::kFiltered;
        }
        ++filterIndex;
    }
    return CtDispatchResult::kUnhandled;
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::runDirectedHandlers(
    const detail::CtDispatchTables* tables,
    const std::shared_ptr<CtReceiver>& receiver, CtEvent<T>& event) {
    // §8.1 Step 6 — M1 strategy: linear scan of the snapshot vector,
    // lock() each weak_ptr and compare objectId (Rev 4, P2-12).
    const auto it = tables->handlers.find(CtTypeKey<T>::value());
    if (it == tables->handlers.end())
        return CtDispatchResult::kUnhandled;

    for (const auto& entry : it->second) {
        auto target = entry.receiver.lock();
        if (!target || target->objectId() != receiver->objectId())
            continue; // expired or different receiver: skip
        CtDispatchContext ctx; // private ctor; CtDispatcher is a friend
        ctx.m_phase = CtTracePhase::kDelivered;
        ctx.m_receiverId = target->objectId();
        const auto start = std::chrono::steady_clock::now();
        entry.fn(&event);
        ctx.m_elapsed = std::chrono::steady_clock::now() - start;
        m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDelivered,
                       ctx, target->objectId());
        if (event.accepted)
            return CtDispatchResult::kHandled; // first accept stops (§8.1)
    }
    return CtDispatchResult::kUnhandled; // no accept: matched or not (§8.1)
}

template <typename EventSet>
template <typename T>
CtDispatchResult CtDispatcher<EventSet>::runBroadcastHandlers(
    const detail::CtDispatchTables* tables, CtEvent<T>& event) {
    // §8.2 Step 6 — all entries: broadcast (empty receiver) always called;
    // expired receivers skipped; accept accumulated as OR.
    const auto it = tables->handlers.find(CtTypeKey<T>::value());
    if (it == tables->handlers.end())
        return CtDispatchResult::kNoReceiver;

    bool anyCalled = false;
    bool anyAccepted = false;
    for (const auto& entry : it->second) {
        const bool isBroadcastEntry = detail::isEmptyWeak(entry.receiver);
        if (isBroadcastEntry) {
            anyCalled = true;
            CtDispatchContext ctx; // private ctor; CtDispatcher is a friend
            ctx.m_phase = CtTracePhase::kDelivered;
            ctx.m_receiverId = 0;
            const auto start = std::chrono::steady_clock::now();
            entry.fn(&event);
            ctx.m_elapsed = std::chrono::steady_clock::now() - start;
            m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDelivered,
                           ctx, 0);
            anyAccepted = anyAccepted || event.accepted;
            continue;
        }
        auto target = entry.receiver.lock();
        if (!target)
            continue; // expired receiver: skip, keep going (§8.2)
        anyCalled = true;
        CtDispatchContext ctx; // private ctor; CtDispatcher is a friend
        ctx.m_phase = CtTracePhase::kDelivered;
        ctx.m_receiverId = target->objectId();
        const auto start = std::chrono::steady_clock::now();
        entry.fn(&event);
        ctx.m_elapsed = std::chrono::steady_clock::now() - start;
        m_trace.record(CtEventTraits<T>::name, CtTracePhase::kDelivered,
                       ctx, target->objectId());
        anyAccepted = anyAccepted || event.accepted;
    }

    if (!anyCalled)
        return CtDispatchResult::kNoReceiver;
    return anyAccepted ? CtDispatchResult::kHandled
                       : CtDispatchResult::kUnhandled;
}

} // namespace crisp::event
