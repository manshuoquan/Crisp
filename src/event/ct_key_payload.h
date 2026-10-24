#pragma once

// ct_key_payload.h — CRISP-native strong-typed keyboard payload (core layer).
//
// Part of the CRISP project. UI pre-M1: the typed input payload the
// crisp::ui subsystem consumes — ZERO Qt dependency, lives in the core
// library (src/event/). The M4 bridge keeps its own Qt-field version
// (src/bridge/qt_key_payload.h, namespace crisp::event) for M3/M4
// compatibility; THIS type is the migration target (docs/ui/README.md
// "Joint design points", docs/event-system/06 §8 open question #2 — QString
// -> u16string).
//
// Value semantics, hot-path zero allocation (stored by value inside
// CtEvent<CtKeyPayload>).

#include <cstdint>
#include <string>

#include "ct_event.h"    // CtEventTraits primary template (CRISP_EVENT_TYPE)
#include "ct_input.h"    // CtKeyButton / CtModifiers / CtKeyAction
#include "ct_type_key.h" // CRISP_EVENT_TYPE macro

namespace crisp::input {

/// Strong-typed keyboard payload. `key` is the CRISP-native key code;
/// `text` is the UTF-16 unicode text the key produced (may be empty for
/// non-printing keys); `isAutoRepeat` marks key-repeat events.
struct CtKeyPayload {
    CtKeyAction action = CtKeyAction::kPress; ///< press/release
    CtKeyButton key = CtKeyButton::kUnknown;  ///< the pressed key
    std::u16string text;                      ///< unicode text (may be empty)
    CtModifiers modifiers;                    ///< modifier state at event time
    bool isAutoRepeat = false;                ///< true for key-repeat events
};

} // namespace crisp::input

/// EventTraits specialization — required by CtEvent<CtKeyPayload>.
/// (File scope, OUTSIDE namespace crisp::input: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::input::CtKeyPayload, "InputKey");
