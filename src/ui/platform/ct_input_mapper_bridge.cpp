// ct_input_mapper_bridge.cpp - UI3 Qt-host mapper (PLATFORM TU).
// Maps M4 bridged payloads (crisp::event, Qt fields) into the core
// crisp::input payloads. MUST-7: this TU is Qt-dependent and lives in the
// platform layer - the core mapper stays Qt-free.
// Authoritative spec: docs/ui/06-ui3-architecture.md section 4.3 (P4).
//
// Hot path: zero allocation except mapBridgeKey with non-empty text
// (QString -> std::u16string, M-4 exempted - NOT the mouse-move hot path).

#include "ct_input_mapper_bridge.h"

#include <QtCore/qnamespace.h>   // Qt::MouseButton / Qt::Key

namespace crisp::ui::platform {

namespace {

/// Translate a Qt mouse action to the CRISP-native action. The bridge
/// action enum (crisp::event::CtMouseAction) mirrors the core enum
/// (crisp::input::CtMouseAction) 1:1 - both are CRISP-native discriminants
/// (Qt type ids stay in the bridge translator, never here).
crisp::input::CtMouseAction toCoreAction(
    crisp::event::CtMouseAction action) noexcept {
    using B = crisp::event::CtMouseAction;
    using C = crisp::input::CtMouseAction;
    switch (action) {
    case B::kMove:     return C::kMove;
    case B::kPress:    return C::kPress;
    case B::kRelease:  return C::kRelease;
    case B::kDblClick: return C::kDblClick;
    }
    return C::kMove;
}

/// Translate a Qt::MouseButton to the CRISP-native button.
crisp::input::CtMouseButton toCoreButton(Qt::MouseButton b) noexcept {
    using C = crisp::input::CtMouseButton;
    switch (b) {
    case Qt::LeftButton:    return C::kPrimary;
    case Qt::RightButton:   return C::kSecondary;
    case Qt::MiddleButton:  return C::kMiddle;
    case Qt::XButton1:      return C::kX1;
    case Qt::XButton2:      return C::kX2;
    default:                return C::kNone;
    }
}

/// Translate the Qt::MouseButtons aggregate to the CRISP bitmask.
crisp::input::CtMouseButtons toCoreButtons(Qt::MouseButtons bs) noexcept {
    crisp::input::CtMouseButtons out;
    if (bs & Qt::LeftButton)    out.bits |= crisp::input::CtMouseButtons::kPrimary;
    if (bs & Qt::RightButton)   out.bits |= crisp::input::CtMouseButtons::kSecondary;
    if (bs & Qt::MiddleButton)  out.bits |= crisp::input::CtMouseButtons::kMiddle;
    if (bs & Qt::XButton1)      out.bits |= crisp::input::CtMouseButtons::kX1;
    if (bs & Qt::XButton2)      out.bits |= crisp::input::CtMouseButtons::kX2;
    return out;
}

/// Translate Qt::KeyboardModifiers to the CRISP modifier bitmask.
crisp::input::CtModifiers toCoreModifiers(Qt::KeyboardModifiers mods) noexcept {
    crisp::input::CtModifiers out;
    if (mods & Qt::ShiftModifier)   out.bits |= crisp::input::CtModifiers::kShift;
    if (mods & Qt::ControlModifier) out.bits |= crisp::input::CtModifiers::kControl;
    if (mods & Qt::AltModifier)     out.bits |= crisp::input::CtModifiers::kAlt;
    if (mods & Qt::MetaModifier)    out.bits |= crisp::input::CtModifiers::kMeta;
    return out;
}

} // namespace

CtInputMapperResult mapBridgeMouse(
    const crisp::event::CtMousePayload& qtFields,
    const CtSize& widgetLocalSize) noexcept {
    // Section 14 contract: a ZERO-size surface has no hittable area - the
    // mapper refuses (isInput==false) instead of emitting a payload.
    if (widgetLocalSize.isEmpty())
        return CtInputMapperResult{std::monostate{}};

    crisp::input::CtMousePayload p;
    p.action = toCoreAction(qtFields.action);
    p.x = qtFields.x;
    p.y = qtFields.y;
    const crisp::input::CtMouseButton coreButton =
        toCoreButton(qtFields.button);
    p.button = (coreButton == crisp::input::CtMouseButton::kNone)
                   ? std::nullopt
                   : std::optional<crisp::input::CtMouseButton>{coreButton};
    p.buttons = toCoreButtons(qtFields.buttons);
    p.modifiers = toCoreModifiers(qtFields.modifiers);
    // D6/MUST-8: the bridge payload carries the timestamp (filled from
    // QMouseEvent::timestamp() by the M4 translator); pass it through.
    p.timestamp = qtFields.timestamp;
    return CtInputMapperResult{p};
}

CtInputMapperResult mapBridgeKey(
    const crisp::event::CtKeyPayload& qtFields) noexcept {
    crisp::input::CtKeyPayload p;
    p.action = (qtFields.action == crisp::event::CtKeyAction::kPress)
                   ? crisp::input::CtKeyAction::kPress
                   : crisp::input::CtKeyAction::kRelease;

    // Non-printing keys go through the shared semantic table (kQtKey);
    // printable ASCII maps directly (S3). Qt::Key 'A'..'Z' are the
    // UPPER-case code points 0x41..0x5A (Qt's Key enum); CRISP keys are
    // lower-case letters (shift state lives in CtModifiers). Digits are
    // 0x30..0x39. Table-driven (MUST-4): no arithmetic on enum values.
    const auto qtKey = static_cast<std::uint32_t>(qtFields.key);
    if (qtKey >= 'A' && qtKey <= 'Z') {
        static constexpr crisp::input::CtKeyButton kAsciiUpper[] = {
            crisp::input::CtKeyButton::kA, crisp::input::CtKeyButton::kB,
            crisp::input::CtKeyButton::kC, crisp::input::CtKeyButton::kD,
            crisp::input::CtKeyButton::kE, crisp::input::CtKeyButton::kF,
            crisp::input::CtKeyButton::kG, crisp::input::CtKeyButton::kH,
            crisp::input::CtKeyButton::kI, crisp::input::CtKeyButton::kJ,
            crisp::input::CtKeyButton::kK, crisp::input::CtKeyButton::kL,
            crisp::input::CtKeyButton::kM, crisp::input::CtKeyButton::kN,
            crisp::input::CtKeyButton::kO, crisp::input::CtKeyButton::kP,
            crisp::input::CtKeyButton::kQ, crisp::input::CtKeyButton::kR,
            crisp::input::CtKeyButton::kS, crisp::input::CtKeyButton::kT,
            crisp::input::CtKeyButton::kU, crisp::input::CtKeyButton::kV,
            crisp::input::CtKeyButton::kW, crisp::input::CtKeyButton::kX,
            crisp::input::CtKeyButton::kY, crisp::input::CtKeyButton::kZ,
        };
        p.key = kAsciiUpper[qtKey - 'A'];
    } else if (qtKey >= '0' && qtKey <= '9') {
        static constexpr crisp::input::CtKeyButton kAsciiDigit[] = {
            crisp::input::CtKeyButton::kD0, crisp::input::CtKeyButton::kD1,
            crisp::input::CtKeyButton::kD2, crisp::input::CtKeyButton::kD3,
            crisp::input::CtKeyButton::kD4, crisp::input::CtKeyButton::kD5,
            crisp::input::CtKeyButton::kD6, crisp::input::CtKeyButton::kD7,
            crisp::input::CtKeyButton::kD8, crisp::input::CtKeyButton::kD9,
        };
        p.key = kAsciiDigit[qtKey - '0'];
    } else {
        p.key = mapKeyToCt(CtKeySourceKind::kQtKey, qtKey);
    }

    // D12: mapBridgeKey KEEPS the bridge text (Qt side has real Unicode
    // text; the Win32 path leaves it empty). QString -> std::u16string may
    // allocate once (M-4 exempted - not the hot path).
    p.text = qtFields.text.toStdU16String();
    p.modifiers = toCoreModifiers(qtFields.modifiers);
    p.isAutoRepeat = qtFields.isAutoRepeat;
    return CtInputMapperResult{p};
}

} // namespace crisp::ui::platform
