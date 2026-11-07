// ct_push_button.cpp — UI4 push button implementation (crisp::ui).
//
// The state machine is PURE logic (no platform calls): transitions happen
// from the onMouseInput hook (CtWidgetBase::dispatchInput forwards the
// raw mouse payload after the disabled gate). Painting is a thin
// fixed-palette pass (UI5 wires the style). Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.2.

#include "ct_push_button.h"

#include <utility>

namespace crisp::ui {

std::shared_ptr<CtPushButton> CtPushButton::create(
    const std::shared_ptr<CtWidgetBase>& parent, std::u16string text) {
    // P5 sanctioned factory: createWidget<W> builds the shared_ptr, sets
    // the weak self-reference and attaches to the tree (the ctor itself
    // must not touch the tree).
    return CtWidgetBase::createWidget<CtPushButton>(parent, std::move(text));
}

CtPushButton::CtPushButton(const std::shared_ptr<CtWidgetBase>& parent,
                           std::u16string text)
    : CtWidgetBase(parent), m_text(std::move(text)) {}

void CtPushButton::setText(std::u16string text) {
    if (!assertSameThread())
        return;
    if (m_text == text)
        return;
    m_text = std::move(text);
    markDamage(geometry()); // the full widget (no per-glyph damage yet)
}

void CtPushButton::setEnabled(bool enabled) noexcept {
    if (!assertSameThread())
        return;
    // Explicit disable transition: kPressed -> kNormal (a press never
    // survives disable); kNormal/kHovered -> kDisabled; kDisabled -> the
    // matching enabled state (kNormal). An armed press is disarmed on
    // disable.
    const CtButtonState before = m_buttonState;
    if (!enabled) {
        m_buttonState = CtButtonState::kDisabled;
        m_armed = false;
    } else if (m_buttonState == CtButtonState::kDisabled) {
        m_buttonState = CtButtonState::kNormal;
    }
    if (m_buttonState != before)
        markDamage(geometry());
    CtWidgetBase::setEnabled(enabled);
}

void CtPushButton::onMouseInput(const crisp::input::CtMousePayload& payload) {
    // The explicit state machine (pure logic, no platform dependency).
    // Coordinates are widget-local (crisp::input contract).
    const bool inside = hitTest(payload.x, payload.y);

    switch (payload.action) {
    case crisp::input::CtMouseAction::kPress: {
        if (!inside)
            break; // press outside: ignore
        m_armed = true;
        m_pressX = payload.x;
        m_pressY = payload.y;
        m_pressButton = payload.button.value_or(
            crisp::input::CtMouseButton::kPrimary);
        setState(CtButtonState::kPressed);
        break;
    }
    case crisp::input::CtMouseAction::kRelease: {
        // P2-5 fix: a release without a CHANGED button (e.g. a mapper that
        // sends kRelease + nullopt button) must not complete a click — the
        // complete condition requires the released button to MATCH the
        // armed press button.
        const bool complete =
            m_armed && inside && payload.button.has_value() &&
            payload.button.value() == m_pressButton;
        m_armed = false;
        if (complete) {
            // The click completes: emit the semantic event (the app's
            // onClick handlers receive it).
            crisp::input::CtClickPayload click;
            click.x = m_pressX;
            click.y = m_pressY;
            click.button = m_pressButton;
            click.buttons = payload.buttons;
            click.modifiers = payload.modifiers;
            click.timestamp = payload.timestamp;
            emitSemantic(click);
        }
        setState(inside ? CtButtonState::kHovered : CtButtonState::kNormal);
        break;
    }
    case crisp::input::CtMouseAction::kMove:
    case crisp::input::CtMouseAction::kDblClick: {
        // Hover tracking: inside → kHovered (unless armed), outside → kNormal.
        if (!m_armed) {
            const CtButtonState target =
                inside ? CtButtonState::kHovered : CtButtonState::kNormal;
            setState(target);
        }
        break;
    }
    }
}

void CtPushButton::setState(CtButtonState next) noexcept {
    if (m_buttonState == next)
        return;
    m_buttonState = next;
    markDamage(geometry());
}

CtSize CtPushButton::sizeHint(CtPaintBackend& backend) const noexcept {
    // The text hint + the fixed UI4 padding (2 chars each side, 1 line).
    // The backend's font metrics resolve the text size (PixelBuffer:
    // fixed 8x16; GDI: real metrics). UI5 wires the style's contribution.
    // P2-7 ruling: fontMetrics is noexcept (interface contract).
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // no text support: zero hint
    return CtSize{metrics.width + 2 * kTextWidgetPaddingPx,
                  metrics.height};
}

void CtPushButton::paint(CtPaintBackend& backend, const CtRect& rect) const {
    // UI4 fixed palette (UI5 wires CtStyle). The button paints its own
    // background + centered text; the owner (backing store / window paint
    // pass) drives the painter session.
    // (Deliberately NOT drawing yet: UI4 tests exercise the state machine
    // and text/size hint; the paint pass lands with the UI4 window
    // integration. Painting uses backend primitives only.)
    (void)backend;
    (void)rect;
}

} // namespace crisp::ui
