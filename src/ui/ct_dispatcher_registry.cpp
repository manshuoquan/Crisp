// ct_dispatcher_registry.cpp — per-thread semantic-event dispatcher
// registry (crisp::ui, UI4). See ct_dispatcher_registry.h.
//
// Part of the CRISP project. One CtDispatcher<CtSemanticEventSet> per
// affinity thread, lazily created on first use; widgets of a thread share
// the dispatcher, so binding a handler does not allocate per widget.

#include "ct_dispatcher_registry.h"

#include <utility>

namespace crisp::ui {

std::shared_ptr<crisp::event::CtDispatcher<CtSemanticEventSet>>
semanticDispatcherForCurrentThread() {
    // The per-thread table: one entry per affinity thread (the GUI thread
    // model means this is consulted on the UI thread; the mutex guards the
    // rare lazy-create race, e.g. two widgets created on different threads).
    static std::mutex s_mutex;
    static std::unordered_map<crisp::event::CtThreadId,
                              std::shared_ptr<crisp::event::CtDispatcher<CtSemanticEventSet>>>
        s_dispatchers;

    const auto tid = crisp::event::currentThreadId();
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_dispatchers.find(tid);
    if (it != s_dispatchers.end())
        return it->second;

    auto dispatcher = std::make_shared<crisp::event::CtDispatcher<CtSemanticEventSet>>();
    s_dispatchers.emplace(tid, dispatcher);
    return dispatcher;
}

} // namespace crisp::ui
