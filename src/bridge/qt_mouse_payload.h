#pragma once

// qt_mouse_payload.h — typed mouse payload for the M4 bridge (docs/
// event-system/06-m4-typed-translation.md §2.1–§2.2).
//
// Part of the CRISP project. M3 bridged QMouseEvent as a raw
// CtQtEventPayload; M4 translates it into a strong-typed CtMousePayload
// carrying the action discriminant + local position + button state, so
// CRISP handlers face type-safe fields instead of the int enumeration
// QEvent::Type. CtMouseAction is a CRISP-native discriminant (zero Qt
// dependency) replacing the Qt type id as the primary action axis.
//
// Qt-enum fields (Qt::MouseButton/Qt::KeyboardModifiers) are PERMITTED:
// this payload lives in the bridge layer (src/bridge/), and 05 §5.2
// contract #1 constrains only the core library (src/event/).

#include <cstdint>

#include <QtCore/qnamespace.h>  // Qt::MouseButton, Qt::KeyboardModifiers

#include "ct_event.h"  // CtEventTraits primary template (CRISP_EVENT_TYPE)

namespace crisp::event {

/// (M4) Mouse action — CRISP-native discriminant replacing the Qt type id
/// (QEvent::MouseMove/Press/Release/ButtonDblClick). Zero Qt dependency;
/// the ONLY mapping from QEvent::Type to this enum lives in the bridge
/// translator (qt_translator.cpp, §5.1).
enum class CtMouseAction : uint8_t {
    kMove,     ///< QEvent::MouseMove
    kPress,    ///< QEvent::MousePress
    kRelease,  ///< QEvent::MouseRelease
    kDblClick, ///< QEvent::MouseButtonDblClick
};

/// (M4) Strong-typed mouse payload extracted from QMouseEvent (§2.2).
/// Value semantics; hot-path zero allocation; stored inside
/// CtEvent<CtMousePayload> by value in the bridge set variant.
struct CtMousePayload {
    CtMouseAction action = CtMouseAction::kMove; ///< press/move/release/dblclick
    int x = 0;               ///< local x (QMouseEvent::position().x())
    int y = 0;               ///< local y (QMouseEvent::position().y())
    Qt::MouseButton button = Qt::NoButton;   ///< the changed button
    Qt::MouseButtons buttons = Qt::NoButton; ///< all pressed buttons
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

} // namespace crisp::event

/// EventTraits specialization — required by CtEvent<CtMousePayload>.
/// (File scope, OUTSIDE namespace crisp::event: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::event::CtMousePayload, "Mouse");
