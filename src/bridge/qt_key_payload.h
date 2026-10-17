#pragma once

// qt_key_payload.h — typed keyboard payload for the M4 bridge (docs/
// event-system/06-m4-typed-translation.md §2.1, §2.3).
//
// Part of the CRISP project. M3 bridged QKeyEvent as a raw
// CtQtEventPayload; M4 translates it into a strong-typed CtKeyPayload
// carrying the action discriminant + key + text + modifiers, so CRISP
// handlers face type-safe fields instead of the int enumeration
// QEvent::Type. CtKeyAction is a CRISP-native discriminant (zero Qt
// dependency) for press/release.
//
// Qt-enum fields (Qt::Key/Qt::KeyboardModifiers) and QString text are
// PERMITTED: this payload lives in the bridge layer (src/bridge/), and
// 05 §5.2 contract #1 constrains only the core library (src/event/).

#include <cstdint>

#include <QString>
#include <QtCore/qnamespace.h>  // Qt::Key, Qt::KeyboardModifiers

#include "ct_event.h"  // CtEventTraits primary template (CRISP_EVENT_TYPE)

namespace crisp::event {

/// (M4) Key action — CRISP-native discriminant for press/release.
/// Zero Qt dependency; the ONLY mapping from QEvent::Type to this enum
/// lives in the bridge translator (qt_translator.cpp, §5.2).
enum class CtKeyAction : uint8_t {
    kPress,    ///< QEvent::KeyPress
    kRelease,  ///< QEvent::KeyRelease
};

/// (M4) Strong-typed keyboard payload extracted from QKeyEvent (§2.3).
/// Value semantics; stored inside CtEvent<CtKeyPayload> by value.
/// `key` is Qt::Key (NOT int — principle #1 forbids int-enum
/// reinterpretation at the handler boundary; the explicit cast lives in
/// the bridge translator only, §5.2).
struct CtKeyPayload {
    CtKeyAction action = CtKeyAction::kPress; ///< press/release
    Qt::Key key = Qt::Key_unknown;            ///< the pressed key
    QString text;                             ///< unicode text (may be empty)
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool isAutoRepeat = false;
};

} // namespace crisp::event

/// EventTraits specialization — required by CtEvent<CtKeyPayload>.
/// (File scope, OUTSIDE namespace crisp::event: the CRISP_EVENT_TYPE macro
/// opens `namespace crisp::event { ... }` itself, see ct_type_key.h.)
CRISP_EVENT_TYPE(crisp::event::CtKeyPayload, "Key");
