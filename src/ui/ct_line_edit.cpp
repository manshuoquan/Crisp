// ct_line_edit.cpp — UI4 single-line text editor implementation (crisp::ui).
//
// The editing logic is PURE (no platform calls): key input arrives via
// onKeyInput (CtWidgetBase::dispatchInput forwards the raw key payload
// after the disabled gate), text changes are emitted as
// CtTextChangedPayload semantic events. Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.2.

#include "ct_line_edit.h"

#include <utility>

#include "ct_paint_backend.h" // CtPaintBackend (paint surface)
#include "ct_painter.h"       // CtPainter (begin/fillRect/drawText)
#include "ct_style.h"         // CtStyle (paint queries)
#include "ct_style_basic.h"   // defaultStyle() (UI5)

namespace crisp::ui {

std::shared_ptr<CtLineEdit> CtLineEdit::create(
    const std::shared_ptr<CtWidgetBase>& parent, std::u16string initial) {
    // P5 sanctioned factory: createWidget<W> (see CtPushButton::create).
    return CtWidgetBase::createWidget<CtLineEdit>(parent, std::move(initial));
}

CtLineEdit::CtLineEdit(const std::shared_ptr<CtWidgetBase>& parent,
                       std::u16string initial)
    : CtWidgetBase(parent), m_text(std::move(initial)) {}

std::u16string_view CtLineEdit::text() const noexcept {
    return m_text;
}

void CtLineEdit::setText(std::u16string text) {
    if (!assertSameThread())
        return;
    if (m_text == text)
        return;
    m_text = std::move(text);
    if (m_cursor > m_text.size())
        m_cursor = m_text.size();
    markDamage(geometry());
    emitTextChanged();
}

std::size_t CtLineEdit::cursorPos() const noexcept {
    return m_cursor;
}

void CtLineEdit::setCursorPos(std::size_t pos) noexcept {
    if (!assertSameThread())
        return;
    const std::size_t clamped =
        pos > m_text.size() ? m_text.size() : pos;
    if (clamped == m_cursor)
        return;
    m_cursor = clamped;
    markDamage(geometry()); // caret moved: repaint the caret column
}

void CtLineEdit::onKeyInput(const crisp::input::CtKeyPayload& payload) {
    // The editing state machine (pure logic, no platform dependency).
    if (!isKeyPress(payload))
        return; // releases never edit

    switch (payload.key) {
    case crisp::input::CtKeyButton::kBackspace:
        eraseBeforeCaret();
        break;
    case crisp::input::CtKeyButton::kDelete:
        eraseAfterCaret();
        break;
    case crisp::input::CtKeyButton::kArrowLeft:
        if (m_cursor > 0) {
            --m_cursor;
            markDamage(geometry());
        }
        break;
    case crisp::input::CtKeyButton::kArrowRight:
        if (m_cursor < m_text.size()) {
            ++m_cursor;
            markDamage(geometry());
        }
        break;
    case crisp::input::CtKeyButton::kHome:
        if (m_cursor != 0) {
            m_cursor = 0;
            markDamage(geometry());
        }
        break;
    case crisp::input::CtKeyButton::kEnd:
        if (m_cursor != m_text.size()) {
            m_cursor = m_text.size();
            markDamage(geometry());
        }
        break;
    default:
        // Printable text: the payload's UTF-16 `text` (empty for
        // non-printing keys — Enter/Escape/Tab produce no text).
        if (!payload.text.empty())
            insertAtCaret(payload.text);
        break;
    }
}

void CtLineEdit::onMouseInput(const crisp::input::CtMousePayload& payload) {
    // Caret placement from the press position (widget-local). The caret
    // is the nearest character boundary to the click x. The GDI backend
    // resolves real metrics via CtPaintBackend::fontMetrics; the caret
    // column uses the SHARED fixed char width (WARN-4) so the click-to-
    // column math cannot drift from the backend's drawText.
    if (payload.action != crisp::input::CtMouseAction::kPress)
        return;
    // P1-1 fix: the click x is widget-local and INCLUDES the style's left
    // frame (padding+border) — the caret column counts TEXT characters
    // from the text origin (textX = rect.x + frame.width/2). A click in
    // the left padding maps to column 0 (clamped below).
    const CtStyle& s = effectiveStyle();
    const CtSize frame = s.sizeHint(*this);
    const int textX = geometry().x + frame.width / 2;
    const int clickFromText = payload.x - textX;
    const int col = clickFromText > 0 ? clickFromText / kFixedCharWidthPx : 0;
    const std::size_t pos =
        col >= static_cast<int>(m_text.size()) ? m_text.size()
                                               : static_cast<std::size_t>(col);
    setCursorPos(pos);
}

void CtLineEdit::insertAtCaret(std::u16string_view text) {
    if (text.empty())
        return;
    m_text.insert(m_cursor, text);
    m_cursor += text.size();
    markDamage(geometry());
    emitTextChanged();
}

void CtLineEdit::eraseBeforeCaret() {
    if (m_cursor == 0)
        return;
    m_text.erase(m_cursor - 1u, 1u);
    --m_cursor;
    markDamage(geometry());
    emitTextChanged();
}

void CtLineEdit::eraseAfterCaret() {
    if (m_cursor >= m_text.size())
        return;
    m_text.erase(m_cursor, 1u);
    markDamage(geometry());
    emitTextChanged();
}

void CtLineEdit::emitTextChanged() {
    crisp::input::CtTextChangedPayload payload;
    payload.text = m_text;
    payload.cursorPos = m_cursor;
    emitSemantic(payload);
}

CtSize CtLineEdit::sizeHint(CtPaintBackend& backend) const noexcept {
    // The text hint + the style's frame contribution (UI5).
    const CtStyle& s = effectiveStyle();
    const CtSize frame = s.sizeHint(*this);
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // no text support: zero hint
    return CtSize{metrics.width + frame.width, metrics.height + frame.height};
}

void CtLineEdit::paint(CtPainter& painter, const CtRect& rect) const {
    // The editor paints its background + text + caret (when focused),
    // colored by the style (P3). The painter is owned by the backing
    // store (WARN-2 ruling); already active with the widget-local
    // transform. The caret is a 1px vertical bar at the caret column
    // (fixed char width from the backend metrics).
    const CtStyle& s = effectiveStyle();

    // Background + border (the editor is a framed box, like the button).
    painter.fillRect(rect, s.backgroundColor(*this));
    const int bw = s.borderWidth(*this);
    if (bw > 0) {
        const CtColor border = s.borderColor(*this);
        painter.fillRect(CtRect{rect.x, rect.y, rect.width, bw}, border);
        painter.fillRect(CtRect{rect.x, rect.bottom() - bw, rect.width, bw}, border);
        painter.fillRect(CtRect{rect.x, rect.y, bw, rect.height}, border);
        painter.fillRect(CtRect{rect.right() - bw, rect.y, bw, rect.height}, border);
    }

    // Text at the content origin (with the style's horizontal padding).
    const CtSize frame = s.sizeHint(*this);
    CtPaintMetrics metrics;
    if (painter.backend()->fontMetrics(CtFont{}, m_text, metrics)) {
        const int textX = rect.x + frame.width / 2;
        const int textY = rect.y + (rect.height - metrics.height) / 2;
        painter.drawText(CtPoint{textX, textY}, m_text, CtFont{},
                         s.textColor(*this));

        // Caret: 1px bar at the caret column (shared fixed char width,
        // WARN-4 — cannot drift from the backend's drawText).
        if (hasFocus()) {
            const int cx = textX + static_cast<int>(m_cursor) * kFixedCharWidthPx;
            painter.fillRect(CtRect{cx, textY, 1, metrics.height},
                             s.textColorForState(*this, CtButtonState::kNormal));
        }
    }
}

} // namespace crisp::ui
