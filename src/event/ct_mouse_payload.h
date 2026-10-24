#pragma once

// ct_mouse_payload.h — CRISP-native strong-typed mouse payload (core layer).
//
// Part of the CRISP project. UI pre-M1: the typed input payload the
// crisp::ui subsystem consumes — ZERO Qt dependency, lives in the core
// library (src/event/). The M4 bridge keeps its own Qt-field version
// (src/bridge/qt_mouse_payload.h, namespace crisp::event) for M3/M4
// compatibility; THIS type is the migration target (docs/ui/README.md
// "Joint design points", docs/event-system/06 §8 open question #1).
//
// Value semantics, hot-path zero allocation (stored by value inside
// CtEvent<CtMousePayload>).

#include <cstdint>
#include <optional>

#include "ct_event.h"    // CtEventTraits primary template (CRISP_EVENT_TYPE)
#include "ct_input.h"    // CtMouseButton / CtMouseButtons / CtModifiers / CtMouseAction
#include "ct_type_key.h" // CRISP_EVENT_TYPE macro

namespace crisp::input {

/// Strong-typed mouse payload. `x`/`y` are widget-local coordinates.
/// `button` is the CHANGED button (engaged on kPress, released on
/// kRelease; std::nullopt on kMove); `buttons` is the aggregate pressed
/// set; `modifiers` the keyboard modifier state at event time.
struct CtMousePayload {
    CtMouseAction action = CtMouseAction::kMove; ///< press/move/release/dblclick
    int x = 0;               ///< widget-local x
    int y = 0;               ///< widget-local y
    std::optional<CtMouseButton> button; ///< the changed button (see above)
    CtMouseButtons buttons;  ///< all pressed buttons (bitmask, zero = none)
    CtModifiers modifiers;   ///< keyboard modifiers at event time
};

} // namespace crisp::input

/// EventTraits specialization — required by CtEvent<CtMousePayload>.
/// (File scope, OUTSIDE namespace crisp::input: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::input::CtMousePayload, "InputMouse");
