// ct_input_mapper_win32.cpp - UI3 Win32 platform mapper TU (MUST-7):
// Win32 MSG -> CtRawMsg -> mapWin32* -> crisp::input payload.
// The ONLY sanctioned mapping from raw Win32 messages into crisp::input
// (docs/ui/06-ui3-architecture.md §4.3 D3); the CORE TU holds only the
// portable shared table (mapKeyToCt + isDoubleClick).
//
// Hot path: zero heap allocation in translation (principle #5).
// Non-input messages (WM_PAINT/WM_NCHITTEST/WM_SIZE) return monostate.

#include <windows.h>

#include <cmath>

#include "../ct_input_mapper.h"

using crisp::input::CtKeyAction;
using crisp::input::CtKeyButton;
using crisp::input::CtKeyPayload;
using crisp::input::CtModifiers;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMouseButtons;
using crisp::input::CtMousePayload;

namespace crisp::ui::platform {

namespace {

/// Translate a Win32 mouse-message wParam modifier bitmask to CtModifiers.
CtModifiers modifiersFromWParam(std::uintptr_t wParam) noexcept {
    CtModifiers m;
    if (wParam & MK_SHIFT)   m.bits |= CtModifiers::kShift;
    if (wParam & MK_CONTROL) m.bits |= CtModifiers::kControl;
    // Alt is not reported in the mouse-message wParam; queried via
    // GetKeyState(VK_MENU) by the caller (below).
    return m;
}

/// Translate the Win32 keyboard modifier state to CtModifiers.
CtModifiers currentModifiers() noexcept {
    CtModifiers m;
    if (GetKeyState(VK_SHIFT) & 0x8000) m.bits |= CtModifiers::kShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m.bits |= CtModifiers::kControl;
    if (GetKeyState(VK_MENU) & 0x8000) m.bits |= CtModifiers::kAlt;
    if (GetKeyState(VK_LWIN) & 0x8000 ||
        GetKeyState(VK_RWIN) & 0x8000) m.bits |= CtModifiers::kMeta;
    return m;
}

/// Translate the mouse-message wParam pressed-button bitmask to CtMouseButtons.
CtMouseButtons buttonsFromWParam(std::uintptr_t wParam) noexcept {
    CtMouseButtons b;
    if (wParam & MK_LBUTTON)   b.bits |= CtMouseButtons::kPrimary;
    if (wParam & MK_RBUTTON)   b.bits |= CtMouseButtons::kSecondary;
    if (wParam & MK_MBUTTON)   b.bits |= CtMouseButtons::kMiddle;
    if (wParam & MK_XBUTTON1)  b.bits |= CtMouseButtons::kX1;
    if (wParam & MK_XBUTTON2)  b.bits |= CtMouseButtons::kX2;
    return b;
}

/// The changed button for a mouse message id (wParam HIWORD for xbuttons).
CtMouseButton buttonForMsg(std::uint32_t msg, std::uintptr_t wParam) noexcept {
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        return CtMouseButton::kPrimary;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        return CtMouseButton::kSecondary;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        return CtMouseButton::kMiddle;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return (HIWORD(wParam) == 1) ? CtMouseButton::kX1 : CtMouseButton::kX2;
    default:
        return CtMouseButton::kNone;
    }
}

/// The action for a mouse message id.
CtMouseAction actionForMsg(std::uint32_t msg) noexcept {
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: return CtMouseAction::kPress;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
    case WM_XBUTTONUP: return CtMouseAction::kRelease;
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK: return CtMouseAction::kDblClick;
    default: return CtMouseAction::kMove;
    }
}

/// True if `msg` is a mouse message the mapper handles.
bool isMouseMsg(std::uint32_t msg) noexcept {
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

} // namespace

CtInputMapperResult
mapWin32Mouse(const CtRawMsg& msg, const CtSize& widgetLocalSize,
              CtDblClickState& dblClick) noexcept {
    if (!isMouseMsg(msg.msg))
        return CtInputMapperResult{std::monostate{}};

    CtMousePayload p;
    p.action = actionForMsg(msg.msg);
    p.x = msg.ptX;
    p.y = msg.ptY;
    // Buttons: for press/release the message wParam excludes the CHANGED
    // button itself (it is not yet/not anymore pressed) - add it explicitly.
    CtMouseButtons held = buttonsFromWParam(msg.wParam);
    const CtMouseButton changed = buttonForMsg(msg.msg, msg.wParam);
    if (p.action == CtMouseAction::kPress)
        held.bits |= static_cast<CtMouseButtons::mask_type>(changed);
    else if (p.action == CtMouseAction::kRelease)
        held.bits &= ~static_cast<CtMouseButtons::mask_type>(changed);
    p.buttons = held;
    p.button = (changed == CtMouseButton::kNone)
                   ? std::nullopt
                   : std::optional<CtMouseButton>{changed};
    p.modifiers = modifiersFromWParam(msg.wParam);
    // Alt: mouse-message wParam does not report it - merge the keyboard state.
    if (GetKeyState(VK_MENU) & 0x8000)
        p.modifiers.bits |= CtModifiers::kAlt;

    // Timestamp (D6): steady_clock derived from GetMessageTime() (boot-ms
    // relative) + GetTickCount64() at message time. Bound by construction.
    p.timestamp = std::chrono::steady_clock::now() -
                  std::chrono::milliseconds(
                      static_cast<std::int64_t>(msg.bootTimeMs) -
                      static_cast<std::int64_t>(msg.time));

    // Double click (D7/MUST-4): the portable predicate decides; only on a
    // press, using the platform-resolved window (GetDoubleClickTime + 4px).
    // The predicate keeps the last-press state across UP/MOVE messages: a
    // DOWN->UP->DOWN sequence must still see the second DOWN as a double
    // click (the time+dist window inside isDoubleClick guards staleness,
    // so no explicit reset is needed for non-press events).
    if (p.action == CtMouseAction::kPress && p.button) {
        const auto maxGap = std::chrono::milliseconds(GetDoubleClickTime());
        if (isDoubleClick(dblClick, p.timestamp, p.x, p.y, maxGap, 4)) {
            p.action = CtMouseAction::kDblClick;
        }
    }
    // Non-press events leave dblClick untouched: a stale last press simply
    // fails the time window on the next press (single click, D7).

    (void)widgetLocalSize; // UI3 keeps payload coords widget-local already;
                           // client-rect size is only needed for hit-testing
                           // (UI4 global mapping), not for this translation.
    return CtInputMapperResult{p};
}

CtInputMapperResult
mapWin32Key(const CtRawMsg& msg) noexcept {
    if (msg.msg != WM_KEYDOWN && msg.msg != WM_KEYUP)
        return CtInputMapperResult{std::monostate{}};

    const auto vk = static_cast<std::uint32_t>(msg.wParam & 0xFF);
    // Printable ASCII maps directly in the platform TU (S3): VK_A..Z ->
    // kA..kZ, VK_0..9 -> kD0..kD9. Non-printing keys go through the
    // shared semantic table (S3).
    CtKeyButton key = CtKeyButton::kUnknown;
    // Printable ASCII maps directly in the platform TU (S3). Table-driven
    // (MUST-4): no arithmetic on enum values - the table is explicit and
    // the values stay stable documented constants (ct_input.h contract).
    if (vk >= 'A' && vk <= 'Z') {
        static constexpr CtKeyButton kAsciiUpper[] = {
            CtKeyButton::kA, CtKeyButton::kB, CtKeyButton::kC, CtKeyButton::kD,
            CtKeyButton::kE, CtKeyButton::kF, CtKeyButton::kG, CtKeyButton::kH,
            CtKeyButton::kI, CtKeyButton::kJ, CtKeyButton::kK, CtKeyButton::kL,
            CtKeyButton::kM, CtKeyButton::kN, CtKeyButton::kO, CtKeyButton::kP,
            CtKeyButton::kQ, CtKeyButton::kR, CtKeyButton::kS, CtKeyButton::kT,
            CtKeyButton::kU, CtKeyButton::kV, CtKeyButton::kW, CtKeyButton::kX,
            CtKeyButton::kY, CtKeyButton::kZ,
        };
        key = kAsciiUpper[vk - 'A'];
    } else if (vk >= '0' && vk <= '9') {
        static constexpr CtKeyButton kAsciiDigit[] = {
            CtKeyButton::kD0, CtKeyButton::kD1, CtKeyButton::kD2, CtKeyButton::kD3,
            CtKeyButton::kD4, CtKeyButton::kD5, CtKeyButton::kD6, CtKeyButton::kD7,
            CtKeyButton::kD8, CtKeyButton::kD9,
        };
        key = kAsciiDigit[vk - '0'];
    } else {
        key = mapKeyToCt(CtKeySourceKind::kWin32Vk, vk);
    }

    CtKeyPayload p;
    p.action = (msg.msg == WM_KEYDOWN) ? CtKeyAction::kPress
                                       : CtKeyAction::kRelease;
    p.key = key;
    p.modifiers = currentModifiers();
    // text stays EMPTY (MUST-3/D12): a single WM_KEYDOWN carries no Unicode
    // text - the text path belongs to UI4 (WM_CHAR state machine).
    // isAutoRepeat: bit 30 of lParam (0x40000000) marks the repeat.
    p.isAutoRepeat = (msg.msg == WM_KEYDOWN) &&
                     ((msg.lParam & 0x40000000u) != 0u);
    return CtInputMapperResult{p};
}

} // namespace crisp::ui::platform
