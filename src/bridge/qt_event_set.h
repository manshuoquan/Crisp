#pragma once

// qt_event_set.h — default bridge event set (M3, §3.2).
//
// Part of the CRISP project. The framework-provided default bridge set
// combines the framework timer payload with the raw Qt event payload.
// Applications may define larger closed sets
// (CtEventSet<CtTimerPayload, CtQtEventPayload, MyPayload...>), but any
// EventSet used with CtQtEventSource<EventSet> MUST contain
// CtQtEventPayload (static_asserted in qt_event_source.h, §3.4).

#include "ct_event_set.h"
#include "ct_timer_event_source.h"  // CtTimerPayload
#include "qt_event_payload.h"       // CtQtEventPayload

namespace crisp::event {

/// Default bridge event set: framework timer payload + raw Qt event payload.
using CtQtEventSet = CtEventSet<CtTimerPayload, CtQtEventPayload>;

} // namespace crisp::event
