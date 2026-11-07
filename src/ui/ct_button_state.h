#pragma once

// ct_button_state.h — UI5 shared button-state enum (crisp::ui).
// The explicit button state machine's states (UI4 §4.4.2). Lifted into
// its own header so the style interface (ct_style.h) can reference the
// states WITHOUT including the concrete CtPushButton class (WARN-6
// ruling: CtStyle is a pure contract — it must not couple to a control).
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.

#include <cstdint>

namespace crisp::ui {

/// The explicit push-button state (pressed is a state, not a paint-time
/// guess — pain point P3/P8). Shared by the button widget and the style.
enum class CtButtonState : std::uint8_t {
    kNormal,   ///< idle
    kHovered,  ///< pointer inside, not pressed
    kPressed,  ///< pointer inside AND pressed (arm)
    kDisabled, ///< setEnabled(false)
};

} // namespace crisp::ui
