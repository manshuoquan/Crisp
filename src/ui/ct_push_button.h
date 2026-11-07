#pragma once

// ct_push_button.h — UI4 push button (crisp::ui).
// EXPLICIT button state machine (docs/ui/03-rewrite-proposal.md §4.4.2) —
// the states are data, not paint-time guesses (pain point P3/P8). The
// machine is pure logic: unit-testable without a platform; the button
// never reads raw input directly — CtWidgetBase::dispatchInput forwards
// the click legs (press/release) via CtClickPayload.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.

#include <memory>
#include <string>

#include "ct_widget_base.h"

namespace crisp::ui {

/// (UI4) Push-button state machine — EXPLICIT states instead of Qt's
/// entangled style+widget logic (pain points P3/P8). Hover/double-click
/// semantics live here as pure logic.
class CtPushButton : public CtWidgetBase {
public:
    /// The explicit state (pressed is a state, not a paint-time guess).
    enum class CtButtonState : std::uint8_t {
        kNormal,   ///< idle
        kHovered,  ///< pointer inside, not pressed
        kPressed,  ///< pointer inside AND pressed (arm)
        kDisabled, ///< setEnabled(false)
    };

    /// Sanctioned factory (P5): create a button attached to `parent`.
    static std::shared_ptr<CtPushButton> create(
        const std::shared_ptr<CtWidgetBase>& parent,
        std::u16string text = {});

    /// Constructor used by the factory (P5: ctor must not touch the tree).
    explicit CtPushButton(const std::shared_ptr<CtWidgetBase>& parent,
                          std::u16string text);

    /// The explicit state (data, observable).
    CtButtonState state() const noexcept { return m_buttonState; }

    /// The button text (UTF-16; empty = no text).
    std::u16string_view text() const noexcept { return m_text; }

    /// Set the button text; triggers markDamage (UI4: the owner relayouts
    /// via CtBoxLayout — the button itself marks the damage; the
    /// "markDamage + relayout" design contract is satisfied by the owner's
    /// explicit relayout() after a setText).
    void setText(std::u16string text);

    /// Semantic event: emitted (typed) when press+release inside the
    /// button completes — the widget's own payload, exposed to the app
    /// (docs/ui/03-rewrite-proposal.md §4.4.2).
    crisp::event::CtHandlerToken onClick(
        crisp::event::CtHandler<crisp::input::CtClickPayload> handler,
        std::string_view name = {}) {
        return bindClickHandler(std::move(handler), name);
    }

    /// Enable/disable the button (flips kNormal/kHovered → kDisabled;
    /// kPressed → kNormal — a press never survives disable).
    void setEnabled(bool enabled) noexcept;

    /// (UI4) Preferred size: the backend's font metrics for the current
    /// text plus the default button padding (fixed in UI4; UI5 wires the
    /// style's sizeHint contribution).
    CtSize sizeHint(CtPaintBackend& backend) const noexcept override;

protected:
    /// (UI4) The button's own mouse-input hook: runs the explicit state
    /// machine (press/move/release → kNormal/kHovered/kPressed) and
    /// emits the CtClickPayload semantic event on a completed
    /// press+release inside the button. Protected (P2-3 fix): input hooks
    /// are internal; the public entry is CtWidgetBase::dispatchInput.
    void onMouseInput(const crisp::input::CtMousePayload& payload) override;

    /// Paint the button into `backend` within `rect` (widget-local).
    /// The button ASKS the style for the palette (UI5 wires CtStyle; UI4
    /// paints the fixed default palette). Fills the background, draws the
    /// text centered. Called by the owner (CtBackingStore / UI4 window
    /// paint pass) on the UI thread.
    void paint(CtPaintBackend& backend, const CtRect& rect) const;

private:
    /// Transition the explicit state; marks damage on change.
    void setState(CtButtonState next) noexcept;

    /// True while a press is armed (button pressed, release pending).
    bool m_armed = false;
    /// The press position/button (completes the click on a release inside).
    int m_pressX = 0;
    int m_pressY = 0;
    crisp::input::CtMouseButton m_pressButton = crisp::input::CtMouseButton::kPrimary;
    /// The button's EXPLICIT state (P3/P8: states are data, not paint-time
    /// guesses). Distinct from CtWidgetBase::m_state (the base's widget
    /// state struct).
    CtButtonState m_buttonState{CtButtonState::kNormal};
    /// The button text (UTF-16). Not on the hot path (setText is a user
    /// action).
    std::u16string m_text;
};

} // namespace crisp::ui
