// ct_dispatcher.cpp — non-template CtDispatcherBase implementation.
//
// Part of the CRISP event subsystem (crisp::event). §5.3: non-template
// mechanisms live in .cpp files. COW unregister paths, the token-state
// registry and the destructor's alive-flip (§9.2 契约 #1).

#include "ct_dispatcher.h"

#include <algorithm>

namespace crisp::event {

CtDispatcherBase::~CtDispatcherBase() {
#if !defined(NDEBUG)
    assert(m_inDispatch == 0 &&
           "CtDispatcherBase: destroyed while a dispatch is in progress");
#endif
    // §9.2 契约 #1: flip alive=false + clear owner for EVERY token while
    // holding m_writeMutex — the same mutex the token destructor's
    // unregister path locks, closing the "token read alive=true ->
    // dispatcher mid-destruction" race.
    std::lock_guard<std::mutex> lock(m_writeMutex);
    for (const auto& weakState : m_tokenStates) {
        if (auto state = weakState.lock()) {
            state->alive.store(false, std::memory_order_seq_cst);
            state->owner.reset();
        }
    }
    m_tokenStates.clear();
}

void CtDispatcherBase::unregisterHandler(CtHandlerId id) {
    std::lock_guard<std::mutex> lock(m_writeMutex);

    auto tables = std::make_shared<detail::CtDispatchTables>(*snapshot());
    bool removed = false;
    for (auto& kv : tables->handlers) {
        auto& vec = kv.second;
        const auto before = vec.size();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const detail::CtDispatchTables::HandlerEntry& e) {
                                     return e.id == id;
                                 }),
                  vec.end());
        removed = removed || (vec.size() != before);
    }
    for (auto& kv : tables->filters) {
        auto& vec = kv.second;
        const auto before = vec.size();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const detail::CtDispatchTables::FilterEntry& e) {
                                     return e.id == id;
                                 }),
                  vec.end());
        removed = removed || (vec.size() != before);
    }
    for (auto& kv : tables->observers) {
        auto& vec = kv.second;
        const auto before = vec.size();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const detail::CtDispatchTables::ObserverEntry& e) {
                                     return e.id == id;
                                 }),
                  vec.end());
        removed = removed || (vec.size() != before);
    }

    if (!removed)
        return; // id not found (already unregistered): no publish

    ++tables->version;
    publish(std::move(tables));

    // Remove this token's weak state from the registry (it is dead now).
    m_tokenStates.erase(
        std::remove_if(m_tokenStates.begin(), m_tokenStates.end(),
                       [id](const std::weak_ptr<detail::CtTokenState>& s) {
                           auto state = s.lock();
                           return state && state->id == id;
                       }),
        m_tokenStates.end());
}

void CtDispatcherBase::unregisterReceiver(uint64_t receiverObjectId) {
    std::lock_guard<std::mutex> lock(m_writeMutex);

    auto tables = std::make_shared<detail::CtDispatchTables>(*snapshot());
    bool removed = false;
    for (auto& kv : tables->handlers) {
        auto& vec = kv.second;
        const auto before = vec.size();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [receiverObjectId](const detail::CtDispatchTables::HandlerEntry& e) {
                                     auto target = e.receiver.lock();
                                     return target &&
                                            target->objectId() == receiverObjectId;
                                 }),
                  vec.end());
        removed = removed || (vec.size() != before);
    }

    if (!removed)
        return;

    ++tables->version;
    publish(std::move(tables));
}

std::shared_ptr<detail::CtTokenState> CtDispatcherBase::makeTokenState(CtHandlerId id) {
    auto state = std::make_shared<detail::CtTokenState>();
    state->id = id;
    state->alive.store(true, std::memory_order_seq_cst);
    state->owner = weak_from_this(); // shared_ptr ownership is mandatory (§7.8)
    m_tokenStates.push_back(state);
    return state;
}

} // namespace crisp::event
