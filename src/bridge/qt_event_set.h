#pragma once

// qt_event_set.h — default bridge event set (M3, §3.2; M4 typed extension).
//
// Part of the CRISP project. The framework-provided default bridge set
// combines the framework timer payload with the raw Qt event payload and
// the M4 typed mouse/key payloads. Applications may define larger closed
// sets (CtEventSet<CtTimerPayload, CtQtEventPayload, CtMousePayload,
// CtKeyPayload, MyPayload...>), but any EventSet used with
// CtQtEventSource<EventSet> MUST contain CtQtEventPayload, CtMousePayload
// AND CtKeyPayload (static_asserted in qt_event_source.h, §3.4 — M4
// contract: the source's std::visit pushes all three, 06 §4).

#include "ct_event_set.h"
#include "ct_timer_event_source.h"  // CtTimerPayload
#include "qt_event_payload.h"       // CtQtEventPayload
#include "qt_mouse_payload.h"       // CtMousePayload
#include "qt_key_payload.h"         // CtKeyPayload

namespace crisp::event {

/// Default bridge event set: framework timer payload + raw Qt event payload
/// + M4 typed mouse/key payloads.
using CtQtEventSet =
    CtEventSet<CtTimerPayload, CtQtEventPayload, CtMousePayload, CtKeyPayload>;

} // namespace crisp::event
