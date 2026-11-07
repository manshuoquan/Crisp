#pragma once

// ct_dispatcher_registry.h — per-thread semantic-event dispatcher registry
// (crisp::ui, UI4). One CtDispatcher<SemanticEventSet> per affinity thread,
// lazily created on first use. UI4 widgets bind their semantic handlers
// (click/text-changed/focus) against the registry entry of their thread.
//
// Part of the CRISP project. Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4 (semantic events) + the event-system
// dispatcher contract (ct_dispatcher.h). Registry design: a deliberately
// small, explicit per-thread table — the GUI-thread-only model means the
// registry is consulted on the UI thread (creation) and handlers fire on
// the same thread (affinity); the mutex guards the rare lazy-create race.

#include <memory>
#include <mutex>
#include <unordered_map>

#include "event/ct_click_payload.h"      // crisp::input::CtClickPayload
#include "event/ct_dispatcher.h"         // crisp::event::CtDispatcher
#include "event/ct_event_set.h"          // crisp::event::CtEventSet
#include "event/ct_focus_payload.h"      // crisp::input::CtFocusPayload
#include "event/ct_text_changed_payload.h" // crisp::input::CtTextChangedPayload
#include "event/ct_thread_affinity.h"    // crisp::event::CtThreadId

namespace crisp::ui {

/// The closed event set of UI4 semantic events (docs/ui/03-rewrite-
/// proposal.md §4.4). The dispatcher of this set carries the widget-level
/// semantic payloads ONLY — raw input (CtMousePayload/CtKeyPayload) lives
/// in the UI3 CtUiEventSet queue, never here.
using CtSemanticEventSet = crisp::event::CtEventSet<
    crisp::input::CtClickPayload,
    crisp::input::CtTextChangedPayload,
    crisp::input::CtFocusPayload>;

/// (UI4) Look up the semantic dispatcher for the current thread, creating
/// it on first use (one per thread, shared across the thread's widgets).
/// Returns nullptr only on allocation failure (the registry is never
/// closed). Called on the widget's affinity thread by CtWidgetBase::
/// semanticDispatcher().
std::shared_ptr<crisp::event::CtDispatcher<CtSemanticEventSet>>
    semanticDispatcherForCurrentThread();

} // namespace crisp::ui
