// ct_label.cpp — UI4 plain-text label implementation (crisp::ui).
//
// The label is the simplest widget: text storage + backend font-metric
// size hint + a thin paint pass. Authoritative spec:
// docs/ui/03-rewrite-proposal.md §4.4.1.

#include "ct_label.h"

#include <utility>

#include "ct_paint_backend.h" // CtPaintBackend (paint surface)
#include "ct_painter.h"       // CtPainter (begin/drawText)
#include "ct_style.h"         // CtStyle (paint queries)
#include "ct_style_basic.h"   // defaultStyle() (UI5)

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
    // exception can cross this noexcept boundary (P2-7 ruling). The
    // style's frame contribution is added (UI5).
    const CtStyle& s = effectiveStyle();
    const CtSize frame = s.sizeHint(*this);
    CtPaintMetrics metrics;
    if (!backend.fontMetrics(CtFont{}, m_text, metrics))
        return CtSize{}; // backend without text support: zero hint
    return CtSize{metrics.width + frame.width, metrics.height + frame.height};
}

void CtLabel::paint(CtPainter& painter, const CtRect& rect) const {
    // The label paints its text at the top-left of `rect`, colored by the
    // style (P3: the control asks the style, never embeds style logic).
    // The painter is owned by the backing store (WARN-2 ruling); already
    // active with the widget-local transform.
    const CtStyle& s = effectiveStyle();
    painter.drawText(CtPoint{rect.x, rect.y}, m_text, CtFont{},
                     s.textColor(*this));
}

} // namespace crisp::ui
