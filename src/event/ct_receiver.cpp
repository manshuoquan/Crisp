// ct_receiver.cpp — non-template CtReceiver implementation.
//
// Part of the CRISP event subsystem (crisp::event). §7.5: objectId
// allocation, unbindAll traversal of the dispatcher weak list.

#include "ct_receiver.h"

// unbindAll() calls CtDispatcherBase::unregisterReceiver() (protected);
// an incomplete type cannot invoke members, so the full definition is
// required here. Include direction is acyclic: ct_receiver.cpp is a TU,
// not a header — no circular include.
#include "ct_dispatcher.h"

#include <atomic>

namespace crisp::event {

uint64_t CtReceiver::allocateObjectId() {
    static std::atomic<uint64_t> s_nextObjectId{1};
    return s_nextObjectId.fetch_add(1);
}

CtReceiver::CtReceiver() : m_objectId(allocateObjectId()) {}

CtReceiver::~CtReceiver() {
    unbindAll();
}

void CtReceiver::noteDispatcher(const std::weak_ptr<CtDispatcherBase>& dispatcher) {
    // Deduplicate: a receiver may register many handlers through one
    // dispatcher; keep a single entry so unbindAll() visits it once.
    for (const auto& existing : m_dispatchers) {
        if (!existing.owner_before(dispatcher) && !dispatcher.owner_before(existing)) {
            return; // already tracked
        }
    }
    m_dispatchers.push_back(dispatcher);
}

void CtReceiver::unbindAll() {
    // (Rev 12, P2-A) Traverse the dispatcher weak list; lock() failures
    // (dispatcher gone / already unregistered) are skipped — stale entries
    // are harmless (§9.1 契约 #4).
    for (auto& weakDispatcher : m_dispatchers) {
        auto dispatcher = weakDispatcher.lock();
        if (dispatcher) {
            dispatcher->unregisterReceiver(m_objectId);
        }
    }
    m_dispatchers.clear();
}

} // namespace crisp::event
