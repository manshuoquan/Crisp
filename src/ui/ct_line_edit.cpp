// ct_line_edit.cpp — UI4 single-line text editor implementation (crisp::ui).
//
// The editing logic is PURE (no platform calls): key input arrives via
// onKeyInput (CtWidgetBase::dispatchInput forwards the raw key payload
// after the disabled gate), text changes are emitted as
// CtTextChangedPayload semantic events. Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.2.

#include "ct_line_edit.h"

#include <utility>

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
    // is the nearest character boundary to the click x using the fixed
    // PixelBuffer char width (kCharWidthPx). The GDI backend resolves real
    // metrics via CtPaintBackend::fontMetrics (P2-8 fix: the caret model
    // shares the fixed char-width constant with the PixelBuffer backend).
    if (payload.action != crisp::input::CtMouseAction::kPress)
        return;
    constexpr int kCharWidthPx = 8; // PixelBuffer fixed char width
    const int col = payload.x / kCharWidthPx;
    const std::size_t pos = col < 0 ? 0u
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
    // The text hint + the fixed UI4 padding (1 char each side, 1 line).
    // P2-7 ruling: fontMetrics is noexcept (interface contract).
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // no text support: zero hint
    return CtSize{metrics.width + 2 * kTextWidgetPaddingPx,
                  metrics.height};
}

void CtLineEdit::paint(CtPaintBackend& backend, const CtRect& rect) const {
    // UI4 fixed palette (UI5 wires CtStyle). The editor paints its own
    // background + text + caret (when focused); the owner (backing store
    // / window paint pass) drives the painter session.
    // (Deliberately NOT drawing yet: UI4 tests exercise the editing logic
    // and text; the paint pass lands with the UI4 window integration.
    // Painting uses backend primitives only.)
    (void)backend;
    (void)rect;
}

} // namespace crisp::ui
