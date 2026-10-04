#pragma once

// CtReceiver: base class for objects that can receive events.
//
// Part of the CRISP event subsystem (crisp::event). The ONLY object a
// CtDispatcher directly addresses. Holds a globally unique objectId, the
// owning thread (CtThreadAffinity), and a type-safe bindHandler<T>
// registration entry point that delegates to CtDispatcher::addHandler.

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "ct_event.h"
#include "ct_handler_token.h"    // CtHandlerToken (§7.7; bindHandler returns it — self-contained)
#include "ct_thread_affinity.h"  // §7.14 (CtThreadAffinity)

// (Rev 5, P1-4) NO #include "ct_dispatcher.h" here — that would create a
// circular dependency (ct_dispatcher.h includes ct_receiver.h). CtDispatcher
// is forward-declared; bindHandler<T> only names it as a dependent parameter
// type, resolved at template instantiation time. ct_dispatcher.h (the
// "richer" header) is the one that includes ct_receiver.h.
// (Rev 12, P2-A) CtDispatcherBase is also forward-declared for the
// m_dispatchers weak_ptr member.
namespace crisp::event {
class CtDispatcherBase;
class CtDispatchContext; ///< fwd: CtObserver<T> names it as a reference parameter
template <typename EventSet>
class CtDispatcher;
}

namespace crisp::event {

/// Handler: the delivery callback for CtEvent<T> to one receiver.
/// Non-const event reference so the handler may accept()/ignore() (§7.3).
template <typename T>
using CtHandler = std::function<void(CtEvent<T>&)>;

/// Filter: returns true to intercept (event NOT delivered); false passes.
template <typename T>
using CtFilter = std::function<bool(CtEvent<T>&)>;

/// Observer: pure, read-only callback for one dispatch step.
/// Const event reference — observation MUST NOT affect the outcome (§8.3).
template <typename T>
using CtObserver = std::function<void(const CtEvent<T>&, const CtDispatchContext&)>;

/// Base class for objects that can receive events.
///
/// Ownership rule (MANDATORY): a CtReceiver MUST be owned by a
/// std::shared_ptr for its entire lifetime. CtDispatcher and CtQueue store
/// only std::weak_ptr<CtReceiver> (never extending lifetime); bindHandler()
/// relies on shared_from_this(). Creating a CtReceiver on the stack or via
/// new is a contract violation — the class provides CtReceiver::create<T>()
/// as the only sanctioned construction path.
class CtReceiver : public std::enable_shared_from_this<CtReceiver> {
public:
    /// Default-construct the receiver; assigns a globally unique objectId.
    CtReceiver();

    CtReceiver(const CtReceiver&) = delete;         ///< identity is not copyable
    CtReceiver& operator=(const CtReceiver&) = delete;

    /// Virtual destructor: receivers may be subclassed. Unregisters all
    /// handlers bound to this receiver across every dispatcher.
    virtual ~CtReceiver();

    /// Globally unique, stable object id (used in traces, tables, queues).
    uint64_t objectId() const noexcept { return m_objectId; }

    /// The thread this receiver is affined to (see §7.14 / §10.2).
    /// Set at construction to the creating thread's id.
    CtThreadId affinityThread() const noexcept { return m_affinity.thread(); }

    /// True if the calling thread owns this receiver.
    bool isOnThread() const noexcept { return m_affinity.isOnThread(); }

    /// Assert the calling thread owns this receiver. Debug: abort on
    /// mismatch; release: returns false (caller degrades, §7.14).
    bool assertSameThread() const noexcept { return m_affinity.assertSameThread(); }

    /// Register a handler for event payload type T on THIS receiver,
    /// delivered by `dispatcher`.
    ///
    /// Application-facing convenience entry point: DELEGATES to
    /// `dispatcher->addHandler<T>(shared_from_this(), handler, name)`
    /// (§7.8) — there is exactly ONE registration table (the dispatcher's
    /// COW tables). Also registers `dispatcher` into m_dispatchers (weak)
    /// so unbindAll() can find and bulk-unregister (Rev 12, P2-A).
    ///
    /// Requires: CtEventTraits<T>::kIsValid and T ∈ EventSet of `dispatcher`
    /// (static_asserted there).
    ///
    /// Returns a move-only RAII token; destroying it unregisters (§7.7).
    /// `name` is an optional human-readable tag stored for traces.
    template <typename EventSet, typename T>
    CtHandlerToken bindHandler(const std::shared_ptr<CtDispatcher<EventSet>>& dispatcher,
                               CtHandler<T> handler,
                               std::string_view name = {}) {
        noteDispatcher(dispatcher);
        return dispatcher->template addHandler<T>(shared_from_this(),
                                                  std::move(handler), name);
    }

    /// Unregister all handlers bound to this receiver (used by ~CtReceiver
    /// and by CtDispatcher's receiver reaping; see §9). Typically not called
    /// directly by application code. Affects every dispatcher this receiver
    /// registered with.
    void unbindAll();

    /// (Rev 12, P2-A) Register `dispatcher` into the weak dispatcher list so
    /// unbindAll() can reach it. Idempotent per dispatcher pointer.
    void noteDispatcher(const std::weak_ptr<CtDispatcherBase>& dispatcher);

private:
    /// Global atomic counter (independent key space from CtTypeKey).
    static uint64_t allocateObjectId();

    uint64_t m_objectId = 0; ///< assigned by allocateObjectId()
    CtThreadAffinity m_affinity; ///< embedded affinity (Rev 2, P1-6)
    /// (Rev 12, P2-A) Dispatchers this receiver registered with — bindHandler
    /// registers, unbindAll traverses. NOT a second handler table (§7.5).
    std::vector<std::weak_ptr<CtDispatcherBase>> m_dispatchers;
};

/// Sanctioned factory: create a receiver (or subclass R) owned by shared_ptr.
/// Only receivers held by shared_ptr may be dispatched to.
template <typename R = CtReceiver, typename... Args>
std::shared_ptr<R> createReceiver(Args&&... args) {
    return std::make_shared<R>(std::forward<Args>(args)...);
}

} // namespace crisp::event
