// ct_handler_token.cpp — non-template CtHandlerToken implementation.
//
// Part of the CRISP event subsystem (crisp::event). §7.7: the destructor
// auto-unregisters; the move-assignment first unregisters a live target.

#include "ct_handler_token.h"

// The destructor calls CtDispatcherBase::unregisterHandler() (protected);
// an incomplete type cannot invoke members, so the full definition is
// required here. Include direction is acyclic: ct_handler_token.cpp is a
// TU, not a header — no circular include.
#include "ct_dispatcher.h"

namespace crisp::event {

CtHandlerToken::~CtHandlerToken() noexcept {
    if (!m_state)
        return;                      // null token: no-op
    if (!m_state->alive.load(std::memory_order_acquire))
        return;                      // dispatcher already gone: skip
    auto owner = m_state->owner.lock();
    if (!owner)
        return;                      // dispatcher mid-destruction: skip
    // unregisterHandler holds m_writeMutex internally; the weak_ptr lock
    // above keeps the dispatcher alive through the call (§9.2 契约 #1).
    owner->unregisterHandler(m_state->id);
}

CtHandlerToken& CtHandlerToken::operator=(CtHandlerToken&& other) noexcept {
    if (this == &other)
        return *this;
    // (Rev 12, P3-2) If THIS token holds a live registration, unregister it
    // first (equivalent to destroy-then-move-construct).
    if (m_state) {
        CtHandlerToken tmp;
        tmp.m_state = std::move(m_state);
        // tmp destructor runs the unregister path at scope exit.
    }
    m_state = std::move(other.m_state);
    return *this;
}

} // namespace crisp::event
