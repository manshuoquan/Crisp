// ct_push_button.cpp — UI4 push button implementation (crisp::ui).
//
// The state machine is PURE logic (no platform calls): transitions happen
// from the onMouseInput hook (CtWidgetBase::dispatchInput forwards the
// raw mouse payload after the disabled gate). Painting is a thin
// fixed-palette pass (UI5 wires the style). Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.2.

#include "ct_push_button.h"

#include <utility>

#include "ct_paint_backend.h" // CtPaintBackend (paint surface)
#include "ct_painter.h"       // CtPainter (begin/fillRect/drawText)
#include "ct_style.h"         // CtStyle (paint queries)
#include "ct_style_basic.h"   // defaultStyle() (UI5)

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
    // Coordinates are WIDGET-LOCAL (crisp::input contract) — the local
    // hit test (dual-run O4 fix: hitTest is parent-local; the payload is
    // local, so use localHitTest).
    const bool inside = localHitTest(payload.x, payload.y);

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
    // The text hint + the style's frame contribution (UI5): the backend's
    // font metrics resolve the text size (PixelBuffer: fixed 8x16; GDI:
    // real metrics); the style adds its horizontal/vertical padding.
    const CtStyle& s = effectiveStyle();
    const CtSize frame = s.sizeHint(*this);
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // no text support: zero hint
    return CtSize{metrics.width + frame.width, metrics.height + frame.height};
}

void CtPushButton::paint(CtPainter& painter, const CtRect& rect) const {
    // The button paints ITS OWN surface by ASKING the style (P3: the
    // control never embeds style logic — the style decides the palette,
    // the painter records the primitives). The painter is owned by the
    // backing store (WARN-2 ruling: no 0.5MB record buffer on the control
    // call stack); it is ALREADY active with the widget-local transform.
    const CtStyle& s = effectiveStyle();

    // Background fill (the border region is drawn over it — WARN-1: the
    // style hint reserves the border so the box fits).
    painter.fillRect(rect, s.backgroundColor(*this));

    // Border (style borderWidth > 0): four edge rects inside `rect`.
    const int bw = s.borderWidth(*this);
    if (bw > 0) {
        const CtColor border = s.borderColor(*this);
        painter.fillRect(CtRect{rect.x, rect.y, rect.width, bw}, border); // top
        painter.fillRect(CtRect{rect.x, rect.bottom() - bw, rect.width, bw}, border); // bottom
        painter.fillRect(CtRect{rect.x, rect.y, bw, rect.height}, border); // left
        painter.fillRect(CtRect{rect.right() - bw, rect.y, bw, rect.height}, border); // right
    }

    // Text: centered in the content rect (the style's text color per
    // state). The backend's font metrics give the text extent.
    CtPaintMetrics metrics;
    if (painter.backend()->fontMetrics(CtFont{}, m_text, metrics)) {
        const int tx = rect.x + (rect.width - metrics.width) / 2;
        const int ty = rect.y + (rect.height - metrics.height) / 2;
        painter.drawText(CtPoint{tx, ty}, m_text, CtFont{},
                         s.textColorForState(*this, m_buttonState));
    }
}

} // namespace crisp::ui
