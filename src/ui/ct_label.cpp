// ct_label.cpp — UI4 plain-text label implementation (crisp::ui).
//
// The label is the simplest widget: text storage + backend font-metric
// size hint + a thin paint pass. Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.1.

#include "ct_label.h"

#include <utility>

namespace crisp::ui {

std::shared_ptr<CtLabel> CtLabel::create(
    const std::shared_ptr<CtWidgetBase>& parent, std::u16string text) {
    // P5 sanctioned factory: createWidget<W> (see CtPushButton::create).
    return CtWidgetBase::createWidget<CtLabel>(parent, std::move(text));
}

CtLabel::CtLabel(const std::shared_ptr<CtWidgetBase>& parent,
                 std::u16string text)
    : CtWidgetBase(parent), m_text(std::move(text)) {}

std::u16string_view CtLabel::text() const noexcept {
    return m_text;
}

void CtLabel::setText(std::u16string text) {
    if (!assertSameThread())
        return;
    if (m_text == text)
        return;
    m_text = std::move(text);
    markDamage(geometry()); // the full widget (no per-glyph damage yet)
}

CtSize CtLabel::sizeHint(CtPaintBackend& backend) const noexcept {
    // The backend's fontMetrics is noexcept (interface contract), so no
    // exception can cross this noexcept boundary (P2-7 ruling).
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // backend without text support: zero hint
    return CtSize{metrics.width, metrics.height};
}

void CtLabel::paint(CtPaintBackend& backend, const CtRect& rect) const {
    // UI4 fixed palette (UI5 wires CtStyle). The label draws its text at
    // the top-left of `rect`; the owner (backing store / window paint
    // pass) drives the painter session.
    // (Deliberately NOT drawing yet: UI4 tests exercise the text storage
    // and size hint; the paint pass lands with the UI4 window integration.
    // Painting uses backend primitives only.)
    (void)backend;
    (void)rect;
}

} // namespace crisp::ui
