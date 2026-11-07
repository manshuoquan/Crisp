#pragma once

// ct_style.h — UI5 style interface (crisp::ui).
// The control ASKS the style how to draw (QStyle drawControl model,
// docs/ui/01-qt-gui-architecture.md §4.1) but the style is a pure virtual
// contract, NOT an int-enum + runtime CSS parser (pain points P2/P3).
// Authoritative spec: docs/ui/03-rewrite-proposal.md §4.5.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.

#include <cstdint>

#include "ct_button_state.h" // CtButtonState (shared state enum, WARN-6)
#include "ct_paint_types.h"  // CtColor / CtSize

namespace crisp::ui {

class CtWidgetBase; ///< fwd: style lookups take a const widget reference

/// (UI5) Pure style interface — the control ASKS the style how to draw
/// (QStyle drawControl model, 01 §4.1) but the style is a pure virtual
/// contract, NOT an int-enum + runtime CSS parser (pain points P2/P3).
///
/// SCOPE (open question #7 ruling): the first version is the THREE
/// primitive sets — text color / border color+width / background color.
/// NO padding/margin/font-family/corner-radius; extension goes through
/// ADDING virtual methods, never runtime configuration.
class CtStyle {
public:
    virtual ~CtStyle() = default;

    /// The text color for `w` in its NORMAL state.
    virtual CtColor textColor(const CtWidgetBase& w) const noexcept = 0;

    /// The background fill color for `w`.
    virtual CtColor backgroundColor(const CtWidgetBase& w) const noexcept = 0;

    /// The border color for `w`.
    virtual CtColor borderColor(const CtWidgetBase& w) const noexcept = 0;

    /// The border width (px) for `w` (0 = no border).
    virtual int borderWidth(const CtWidgetBase& w) const noexcept = 0;

    /// Per-state text lookup (states are explicit, UI4 — no runtime
    /// parsing). The state enum is the SHARED CtButtonState (ct_button_state.h,
    /// WARN-6 ruling: the style contract references the state type, not the
    /// concrete control). Other widgets pass kNormal.
    virtual CtColor textColorForState(
        const CtWidgetBase& w,
        CtButtonState state) const noexcept = 0;

    /// Ask the style for a widget's size hint contribution (PM_ analog).
    /// The style's PADDING contribution only (the text extent itself is
    /// the backend's fontMetrics — the style adds the fixed frame).
    virtual CtSize sizeHint(const CtWidgetBase& w) const noexcept = 0;
};

} // namespace crisp::ui
