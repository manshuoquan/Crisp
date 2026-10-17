#pragma once

// qt_bridge.h — M3/M4 Qt bridge public entry point.
//
// Part of the CRISP project. Include this header to get the whole bridge:
//   - CtQtEventPayload  (qt_event_payload.h, §3.1)
//   - CtQtEventSet      (qt_event_set.h, §3.2)
//   - CtQtTranslatorTable / kIgnoredQtEvents (qt_translator.h, §3.3)
//   - CtQtEventSource   (qt_event_source.h, §3.4)
//   - CtMousePayload / CtMouseAction (qt_mouse_payload.h, 06 §2)
//   - CtKeyPayload / CtKeyAction     (qt_key_payload.h, 06 §2)
//   - postToQt          (qt_sink.h, §3.5)
//
// The bridge layer is the ONLY code in the project that touches Qt headers;
// crisp::event core stays Qt-free (§1.3 boundary #2).

#include "qt_event_payload.h"
#include "qt_event_set.h"
#include "qt_translator.h"
#include "qt_event_source.h"
#include "qt_sink.h"
