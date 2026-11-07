// ct_style_basic.cpp — UI5 basic style implementation (crisp::ui).
//
// Deterministic per-state mapping from a compile-time constant palette.
// NO runtime parsing (P2/P3): the lookups are pure data reads.
// Authoritative spec: docs/ui/03-rewrite-proposal.md §4.5.

#include "ct_style_basic.h"

#include "ct_widget_base.h" // CtWidgetBase (disabled/focus state)

namespace crisp::ui {

CtColor CtBasicStyle::textColor(const CtWidgetBase& w) const noexcept {
    return textColorForState(
        w, w.isEnabled() ? CtButtonState::kNormal : CtButtonState::kDisabled);
}

CtColor CtBasicStyle::backgroundColor(const CtWidgetBase& w) const noexcept {
    (void)w;
    return m_palette.background;
}

CtColor CtBasicStyle::borderColor(const CtWidgetBase& w) const noexcept {
    (void)w;
    return m_palette.border;
}

int CtBasicStyle::borderWidth(const CtWidgetBase& w) const noexcept {
    (void)w;
    return m_palette.borderWidth;
}

CtColor CtBasicStyle::textColorForState(
    const CtWidgetBase& /*w*/,
    CtButtonState state) const noexcept {
    switch (state) {
    case CtButtonState::kHovered:
        return m_palette.textHovered;
    case CtButtonState::kPressed:
        return m_palette.textPressed;
    case CtButtonState::kDisabled:
        return m_palette.textDisabled;
    case CtButtonState::kNormal:
    default:
        return m_palette.textNormal;
    }
}

CtSize CtBasicStyle::sizeHint(const CtWidgetBase& /*w*/) const noexcept {
    // The style's fixed frame contribution (padding + border). The text
    // extent itself comes from the backend's fontMetrics in the widget's
    // sizeHint (CtPushButton/CtLineEdit add this to the text metrics).
    // WARN-1 fix: the border width is part of the frame — paint draws
    // the border INSIDE `rect`, so the hint must reserve it.
    return CtSize{2 * m_palette.horizontalPadding + 2 * m_palette.borderWidth,
                  2 * m_palette.verticalPadding + 2 * m_palette.borderWidth};
}

} // namespace crisp::ui
