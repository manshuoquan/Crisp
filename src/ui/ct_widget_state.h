#pragma once

// ct_widget_state.h — explicit widget state (crisp::ui, UI1).
// Replaces QWidgetData's bitfields + WA_* attribute macros (pain point P12).
// Plain struct-of-bools; hot-path cost unchanged (member of CtWidgetBase,
// no heap).
// Authoritative spec: docs/ui/04-ui1-architecture.md §2.5/§2.6.

#include <cstdint>

namespace crisp::ui {

/// Focus policy enum (UI4 consumes; defined in UI1 so CtWidgetState is
/// complete — same "define now, produce later" rule as CtWidgetPhase).
enum class CtFocusPolicy : uint8_t {
    kNoFocus = 0,   ///< not focusable via Tab/click (UI4)
    kClickFocus,    ///< focusable by click only (UI4)
    kTabFocus,      ///< focusable by Tab/Backtab only (UI4)
    kStrongFocus,   ///< both click and Tab (UI4)
};

/// Explicit widget state — replaces bitfields (P12).
struct CtWidgetState {
    bool visible = true;    ///< participates in layout + painting (UI1: flag only)
    bool enabled = true;    ///< disabled widgets reject input (§3.6, §3.10)
    bool focusable = false; ///< participates in focus chain (UI4)
    bool hasFocus = false;  ///< current focus owner (UI4)
    bool hovered = false;   ///< UI4 state machine owns transitions; UI1: inert flag
    CtFocusPolicy focusPolicy{CtFocusPolicy::kNoFocus}; ///< UI4 consumes
};

} // namespace crisp::ui
