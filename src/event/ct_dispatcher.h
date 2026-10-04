#pragma once

// CtDispatcher: type-safe synchronous event dispatcher (M1 sync core).
//
// Part of the CRISP project. Implements docs/event-system/04-event-system-
// redesign.md §7.8 + §6.5 + §8: COW dispatch tables, the fixed pipeline
// observers(pre) -> filters -> handlers -> observers(post), and the
// send/broadcast entry points. Replaces v0's mutable-table dispatcher.

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ct_event.h"
#include "ct_event_set.h"        // CtEventSet, CtAnyEvent
#include "ct_handler_token.h"    // CtHandlerId, CtHandlerToken, CtHandlerPriority
#include "ct_receiver.h"         // CtReceiver, CtHandler, CtFilter, CtObserver
#include "ct_dispatch_context.h" // CtDispatchContext
#include "ct_thread_affinity.h"  // CtThreadAffinity, CtThreadId (Rev 5, N-13)
#include "ct_trace.h"            // CtTrace, CtTracePhase
#include "ct_type_key.h"         // CtTypeKey
#include "ct_queue.h"            // CtPendingEventBase, CtQueueBase (M2: full
                                 // type now available for dispatchAny/post —
                                 // §12.1 step 9; queue never includes this
                                 // header, so the include graph stays acyclic)

namespace crisp::event::detail {

/// Type-erased registration callbacks (Rev 7, P2-1 + Rev 8, P1-1 — precise
/// definitions so the coder need not guess; signatures ALIGN with §5.2's
/// public aliases). All are std::function wrappers built ONCE at
/// registration time (write path, allocation allowed); dispatch invokes them
/// with a pointer cast back to the concrete CtEvent<T> — the SAME disciplined
/// cast as §6.5's key-match assert (§8.1/§8.2 never call them otherwise).
///   - ErasedHandler:  void(void* event)            — event = CtEvent<T>*
///   - ErasedFilter:   bool(void* event)            — event = CtEvent<T>*
///   - ErasedObserver: void(const void* event, const CtDispatchContext& ctx)
///                                                   — event = const CtEvent<T>*
using ErasedHandler = std::function<void(void*)>;
using ErasedFilter = std::function<bool(void*)>;
using ErasedObserver = std::function<void(const void*, const CtDispatchContext&)>;

/// One immutable snapshot of all registration tables.
/// Read-only during dispatch; replaced wholesale on mutation (COW, §6.5).
struct CtDispatchTables {
    struct HandlerEntry {
        std::weak_ptr<CtReceiver> receiver;   ///< target receiver (or null = broadcast)
        detail::ErasedHandler fn;             ///< type-erased callable (safe, §6.5)
        CtHandlerId id = 0;                   ///< unique registration id (order key)
        CtHandlerPriority priority{CtHandlerPriority::kNormal};
        std::string_view name;                ///< registration name for traces
    };
    struct FilterEntry {
        detail::ErasedFilter fn;
        uint64_t filterSeq = 0;               ///< user-assigned deterministic order
        CtHandlerId id = 0;
        std::string_view name;
    };
    struct ObserverEntry {
        detail::ErasedObserver fn;
        CtHandlerId id = 0;
        std::string_view name;
    };

    std::unordered_map<uint64_t, std::vector<HandlerEntry>> handlers;   // key: CtTypeKey<T>
    std::unordered_map<uint64_t, std::vector<FilterEntry>>  filters;    // key: CtTypeKey<T>
    std::unordered_map<uint64_t, std::vector<ObserverEntry>> observers; // key: CtTypeKey<T>
    uint64_t version = 0;               ///< bumped on every publish
};

} // namespace crisp::event::detail

namespace crisp::event {

/// Non-template base of the dispatcher (Rev 7, P2-1): holds the COW tables,
/// the write mutex, token states, the sequence counter and the trace store.
/// Templated CtDispatcher<EventSet> derives from it to reuse this machinery.
///
/// Ownership rule (MANDATORY, Rev 10, P2-3): a CtDispatcher MUST be created
/// via std::make_shared and held by shared_ptr. CtDispatcherBase inherits
/// enable_shared_from_this; makeTokenState() relies on shared_from_this() —
/// a stack-allocated dispatcher throws std::bad_weak_ptr on the first
/// addHandler. Stack/new construction is a contract violation.
class CtDispatcherBase : public std::enable_shared_from_this<CtDispatcherBase> {
public:
    CtDispatcherBase() = default;
    virtual ~CtDispatcherBase();

    CtDispatcherBase(const CtDispatcherBase&) = delete;
    CtDispatcherBase& operator=(const CtDispatcherBase&) = delete;

    /// The trace store of this dispatcher (observability, §7.9).
    CtTrace& trace() noexcept { return m_trace; }

    /// Dispatcher-owned thread affinity (§8.2 Step 1 base for broadcast).
    bool isOnThread() const noexcept { return m_affinity.isOnThread(); }

    /// Assert the calling thread owns this dispatcher (§7.14). Debug: abort.
    /// Release: returns false so callers degrade (send/broadcast Step 1).
    bool assertSameThread() const noexcept { return m_affinity.assertSameThread(); }

protected:
    /// Internal unregister path invoked by CtHandlerToken's destructor.
    /// Holds m_writeMutex; the caller already locked the owner weak_ptr
    /// (reference count holds the dispatcher alive, §9.2 契约 #1).
    void unregisterHandler(CtHandlerId id);

    /// (Rev 12, P2-A) Bulk unregister ALL Handler entries whose receiver
    /// matches receiverObjectId. Holds m_writeMutex, COW-publishes. Called
    /// by CtReceiver::unbindAll() (§7.5, §9.1 契约 #4) — the receiver is a
    /// friend (below) so it can reach this protected entry; NOT callable by
    /// application code directly.
    void unregisterReceiver(uint64_t receiverObjectId);

    /// Create the shared token state for one registration and track it
    /// (weakly) so the destructor can flip alive=false for every token.
    std::shared_ptr<detail::CtTokenState> makeTokenState(CtHandlerId id);

    /// Dispatch a type-erased pending event (M2 drain path, §7.11 契约 #2).
    /// Called by CtEventLoop::exec during drain; runs the §8.1/§8.2 pipeline
    /// on the pending event WITHOUT re-assigning its sequence (async events
    /// keep the queue-stamped enqueue order, §8.1 Step 3 async exception).
    /// The concrete templated CtDispatcher<EventSet> provides the real
    /// definition (§12.1 step 9) — this pure virtual is the erased entry.
    virtual void dispatchAny(CtPendingEventBase& pending) = 0;

    /// COW snapshot access (see §6.5): std::atomic_load(&m_tables).
    std::shared_ptr<const detail::CtDispatchTables> snapshot() const {
        return std::atomic_load(&m_tables);
    }

    /// COW publish under m_writeMutex (write path only, §6.5).
    void publish(std::shared_ptr<const detail::CtDispatchTables> tables) {
        std::atomic_store(&m_tables, std::move(tables));
    }

    /// Allocate the next global registration id (monotonic, §6.1).
    static CtHandlerId nextHandlerId() noexcept {
        static std::atomic<CtHandlerId> s_nextId{1};
        return s_nextId.fetch_add(1);
    }

    std::mutex m_writeMutex;                          ///< guards registration/unregistration
    std::shared_ptr<const detail::CtDispatchTables> m_tables =
        std::make_shared<const detail::CtDispatchTables>(); ///< COW snapshot; atomic_load/store
    CtThreadAffinity m_affinity;                      ///< dispatcher thread affinity (§7.8)
    CtTrace m_trace;
    /// (Rev 11, P1-2) SYNC sequence counter (§8.1/§8.2 Step 3). send/broadcast
    /// run on the dispatcher's OWNING thread only (dual assertion, Rev 11,
    /// P1-1), so a plain uint64_t is safe. Async events use the queue's
    /// m_seqCounter instead — this counter is never shared across threads.
    uint64_t m_nextSequence = 0;
    /// (Rev 6, P1-3) debug dispatch-depth guard (§9.2 契约 #3): in release
    /// (NDEBUG) the guard is an EMPTY member — zero overhead; in debug it is
    /// a real size_t counter. The two branches MUST differ.
#if defined(NDEBUG)
    // (MinGW 8.1 ignores [[maybe_unused]] on data members; the member is
    // private and GCC never warns about unused private fields, so the
    // attribute is dropped to silence -Wattributes.)
    char m_inDispatchPadding = 0;   ///< release: empty, zero cost
#else
    std::size_t m_inDispatch = 0;                    ///< debug: dispatch-depth guard
#endif

    /// (Rev 6, P2-3) Every live CtTokenState created by makeTokenState is
    /// registered here (weak) so the destructor can flip alive=false + clear
    /// owner for ALL tokens while holding m_writeMutex (§9.2 契约 #1).
    /// unregisterHandler removes its own entry.
    std::vector<std::weak_ptr<detail::CtTokenState>> m_tokenStates;

    /// (Rev 3, N3) CtHandlerToken's destructor calls unregisterHandler(id)
    /// on the owning dispatcher base. The method is protected; the token is
    /// neither a derived class nor a member, so it MUST be a friend.
    friend class CtHandlerToken;

    /// (Rev 5, P1-3) CtEventLoop::exec calls dispatchAny (protected) during
    /// drain (§7.11 契约 #2). The loop is neither a derived class nor a
    /// member, so it MUST be a friend.
    friend class CtEventLoop;

    /// (Rev 12, P2-A) CtReceiver::unbindAll() calls unregisterReceiver
    /// (protected, §7.8). The receiver is neither a derived class nor a
    /// member, so it MUST be a friend.
    friend class CtReceiver;
};

/// RAII depth guard for the debug dispatch-depth counter (§9.2 契约 #3).
/// In release builds it is empty (zero cost); in debug it increments and
/// decrements m_inDispatch around send/broadcast.
#if defined(NDEBUG)
class CtInDispatchGuard {
public:
    CtInDispatchGuard() noexcept = default;
};
#else
class CtInDispatchGuard {
public:
    explicit CtInDispatchGuard(std::size_t& depth) noexcept : m_depth(&depth) {
        ++(*m_depth);
    }
    ~CtInDispatchGuard() noexcept { --(*m_depth); }

    CtInDispatchGuard(const CtInDispatchGuard&) = delete;
    CtInDispatchGuard& operator=(const CtInDispatchGuard&) = delete;

private:
    std::size_t* m_depth = nullptr;
};
#endif

/// Type-safe, event-set-bounded dispatcher.
///
/// EventSet is a CtEventSet<Ts...> (§7.4): all addHandler<T>/send<T> etc.
/// require T ∈ EventSet (static_assert). This is the ONLY public dispatcher
/// type applications use; CtDispatcherBase is an implementation detail.
template <typename EventSet>
class CtDispatcher : public CtDispatcherBase {
public:
    using event_set = EventSet;

    /// Register a handler for event type T on receiver.
    /// Requires: CtEventTraits<T>::kIsValid && EventSet::contains<T>.
    /// `receiver == nullptr` registers a BROADCAST entry (§8.2: delivered
    /// to all live receivers of type T). Returns a move-only RAII token.
    template <typename T>
    CtHandlerToken addHandler(const std::shared_ptr<CtReceiver>& receiver,
                              CtHandler<T> handler,
                              std::string_view name = {});

    /// Register a filter for event type T. Return true to intercept (event
    /// NOT delivered); false passes through. Filters run before handlers.
    template <typename T>
    CtHandlerToken addFilter(CtFilter<T> filter,
                             std::string_view name = {});

    /// Register a pure observer for event type T (const, read-only; runs
    /// both pre-dispatch and post-dispatch, see §8.1/§8.3).
    template <typename T>
    CtHandlerToken addObserver(CtObserver<T> observer,
                               std::string_view name = {});

    /// Synchronously dispatch `event` to a single receiver's handlers.
    /// Requires: T ∈ EventSet (static_assert) and the current thread OWNS
    /// both `receiver` and this dispatcher (dual thread-affinity assert,
    /// §8.1 Step 1). Zero heap allocations on the hot path (§11.1).
    template <typename T>
    CtDispatchResult send(const std::shared_ptr<CtReceiver>& receiver,
                          CtEvent<T>& event);

    /// Broadcast: deliver to ALL live receivers registered for T
    /// (publish-subscribe). Same thread-affinity assert base as send
    /// (dispatcher only, §8.2 Step 1).
    template <typename T>
    CtDispatchResult broadcast(CtEvent<T>& event);

    /// Queue `event` to be delivered on the receiver's affinity thread
    /// (async, §10). Full M2 semantics (§10.1): routes to the target
    /// thread's CtEventLoop queue via the erased pushAny boundary (one heap
    /// allocation for the holder). Returns kHandled if the event was
    /// ACCEPTED (queued), kUnhandled if no loop/queue exists for the target
    /// thread or the queue rejected it (overflow / cross-EventSet).
    /// NO trace phase recorded here (§7.8 契约 #8 — post may run on any
    /// thread; kQueued/kDropped are recorded by the affinity thread).
    template <typename T>
    CtDispatchResult post(const std::shared_ptr<CtReceiver>& receiver,
                          CtEvent<T>&& event);

    /// Queue `event` to an EXPLICIT target thread (async, §10). Same
    /// routing as post but bypasses the receiver's affinity — the pending
    /// record carries an EMPTY target (broadcast delivery, §8.2).
    template <typename T>
    CtDispatchResult postTo(CtThreadId targetThread,
                            CtEvent<T>&& event);

    /// Read the current trace snapshot (observability, §7.9).
    /// Hides CtDispatcherBase::trace() (non-const) — intended (§7.8 契约 #9).
    const CtTrace& trace() const noexcept { return m_trace; }

private:
    /// (Rev 7, P1-5 / M2) Concrete override of the base pure virtual: the
    /// drain path (§7.11 契约 #2). Runs the §8.1/§8.2 pipeline on the
    /// pending event — WITHOUT the sync-path thread assertion (drain runs
    /// on the queue's affinity thread == dispatcher's thread by setDispatcher
    /// contract, Rev 9 P2-4) and WITHOUT re-assigning the sequence (async
    /// events keep the queue-stamped enqueue order). Expired directed
    /// targets: kDropped in the dispatcher trace (§7.11 契约 #2 ③).
    void dispatchAny(CtPendingEventBase& pending) override;

    /// Run the observers for one phase (pre or post, §8.1 Step 4/7).
    template <typename T>
    void runObservers(const detail::CtDispatchTables* tables,
                      const CtEvent<T>& event, uint64_t receiverId);

    /// Run the filters; returns kFiltered on interception (or kNone to
    /// continue to handlers, §8.1 Step 5).
    template <typename T>
    CtDispatchResult runFilters(const detail::CtDispatchTables* tables,
                                const CtEvent<T>& event, uint64_t receiverId);

    /// Deliver to one matching receiver's handler(s) (send, §8.1 Step 6).
    template <typename T>
    CtDispatchResult runDirectedHandlers(const detail::CtDispatchTables* tables,
                                         const std::shared_ptr<CtReceiver>& receiver,
                                         CtEvent<T>& event);

    /// Deliver to all live receivers + broadcast entries (broadcast, §8.2
    /// Step 6). Returns kHandled/kUnhandled/kNoReceiver.
    template <typename T>
    CtDispatchResult runBroadcastHandlers(const detail::CtDispatchTables* tables,
                                          CtEvent<T>& event);
};

} // namespace crisp::event

#include "ct_dispatcher.inl"
