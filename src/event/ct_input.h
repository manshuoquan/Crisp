#pragma once

// ct_input.h — CRISP-native input enums (mouse buttons / key codes /
// modifiers). Zero Qt dependency; the UI subsystem's type-safe input axis
// (docs/ui/README.md "Joint design points" — the M4 bridge payloads keep
// their Qt-enum fields for M3/M4 compatibility; the CORE native types live
// here).
//
// Part of the CRISP project. Design principles (AGENTS.md): type safety
// first — handlers face named enums, never int reinterpretation. Values are
// stable, documented constants; the ONLY sanctioned mapping from platform
// (Qt/Win32) key/mouse values into these enums lives in the platform/bridge
// layer, never in handler code.

#include <cstdint>

namespace crisp::input {

/// (UI pre-M1) CRISP-native mouse button. Mirror of the mouse buttons a GUI
/// stack must carry: primary/secondary/middle + auxiliary. `kNone` is the
/// neutral value (no button involved — e.g. a pure move event).
enum class CtMouseButton : uint8_t {
    kNone = 0,      ///< no button (move-only events)
    kPrimary = 1,   ///< left button (Windows: MK_LBUTTON / WM_LBUTTONDOWN)
    kSecondary = 2, ///< right button (MK_RBUTTON)
    kMiddle = 3,    ///< middle button (MK_MBUTTON)
    kX1 = 4,        ///< first extended button (WM_XBUTTONDOWN, XBUTTON1)
    kX2 = 5,        ///< second extended button (XBUTTON2)
};

/// (UI pre-M1) CRISP-native key code. A stable, small subset sufficient for
/// the first UI milestones: function keys, navigation cluster, editing keys,
/// and the ASCII printable range. Deliberately NOT a 1:1 mirror of any
/// platform scancode set — the platform layer owns the mapping.
enum class CtKeyButton : uint8_t {
    kUnknown = 0, ///< unmapped / platform-unsupported key

    // Function row.
    kF1 = 1, kF2 = 2, kF3 = 3, kF4 = 4, kF5 = 5, kF6 = 6,
    kF7 = 7, kF8 = 8, kF9 = 9, kF10 = 10, kF11 = 11, kF12 = 12,

    // Navigation cluster.
    kArrowUp = 20, kArrowDown = 21, kArrowLeft = 22, kArrowRight = 23,
    kHome = 24, kEnd = 25, kPageUp = 26, kPageDown = 27,

    // Editing keys.
    kEnter = 30, kBackspace = 31, kDelete = 32, kInsert = 33,
    kTab = 34, kEscape = 35, kSpace = 36, kCapsLock = 37,

    // Printable ASCII (lowercase letters; shift state lives in CtModifiers).
    kA = 41, kB = 42, kC = 43, kD = 44, kE = 45, kF = 46, kG = 47,
    kH = 48, kI = 49, kJ = 50, kK = 51, kL = 52, kM = 53, kN = 54,
    kO = 55, kP = 56, kQ = 57, kR = 58, kS = 59, kT = 60, kU = 61,
    kV = 62, kW = 63, kX = 64, kY = 65, kZ = 66,

    // Digits (top row).
    kD0 = 70, kD1 = 71, kD2 = 72, kD3 = 73, kD4 = 74,
    kD5 = 75, kD6 = 76, kD7 = 77, kD8 = 78, kD9 = 79,
};

/// (UI pre-M1) CRISP-native pressed-mouse-button flags. Bitmask of the
/// buttons currently held down; `kNone` = no button pressed. Mirror of the
/// bridge's Qt::MouseButtons aggregate (docs/event-system/06-m4-typed-
/// translation.md §2.2 `buttons` field).
struct CtMouseButtons {
    using mask_type = uint8_t;
    static constexpr mask_type kPrimary = 1u << 0;   ///< left button
    static constexpr mask_type kSecondary = 1u << 1; ///< right button
    static constexpr mask_type kMiddle = 1u << 2;    ///< middle button
    static constexpr mask_type kX1 = 1u << 3;        ///< first extended
    static constexpr mask_type kX2 = 1u << 4;        ///< second extended
    static constexpr mask_type kNone = 0u;           ///< no button pressed

    /// The combined bitmask (zero = none).
    mask_type bits = kNone;

    /// True if any button is held.
    bool any() const noexcept { return bits != kNone; }

    /// True if the given button (or combo) is held.
    bool has(mask_type m) const noexcept {
        return (bits & m) == m;
    }
};

/// (UI pre-M1) CRISP-native modifier flags. Bitmask of the standard GUI
/// modifier keys. Plain bit positions — OR freely; zero = no modifier.
/// (Named as a struct-of-bits; `kNone`/`kNoModifier` = 0.)
struct CtModifiers {
    using mask_type = uint8_t;
    static constexpr mask_type kShift = 1u << 0;    ///< Shift
    static constexpr mask_type kControl = 1u << 1;  ///< Ctrl
    static constexpr mask_type kAlt = 1u << 2;      ///< Alt / Option
    static constexpr mask_type kMeta = 1u << 3;     ///< Meta / Super / Cmd
    static constexpr mask_type kNone = 0u;          ///< no modifier pressed

    /// The combined bitmask (zero = none).
    mask_type bits = kNone;

    /// True if any modifier is set.
    bool any() const noexcept { return bits != kNone; }

    /// True if the given modifier (or combo) is set.
    bool has(mask_type m) const noexcept {
        return (bits & m) == m;
    }
};

/// (UI pre-M1) Mouse action — CRISP-native discriminant. The primary axis
/// for CtMousePayload (mirrors the M4 bridge action enum,
/// docs/event-system/06-m4-typed-translation.md §2.1).
enum class CtMouseAction : uint8_t {
    kMove,     ///< pointer moved (no button change)
    kPress,    ///< a button went down
    kRelease,  ///< a button went up
    kDblClick, ///< double click (time+dist window, platform-supplied)
};

/// (UI pre-M1) Key action — CRISP-native discriminant for press/release.
enum class CtKeyAction : uint8_t {
    kPress,   ///< key went down
    kRelease, ///< key went up
};

} // namespace crisp::input
