#pragma once

// ct_label.h — UI4 plain-text label (crisp::ui).
// NO rich text (QTextDocument is out of scope, pain point P10): a single
// line of u16 text, drawn via the backend's text primitive. Width/height
// hints come from backend font metrics.
// docs/ui/03-rewrite-proposal.md §4.4.1.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.

#include <memory>
#include <string>

#include "ct_widget_base.h"

namespace crisp::ui {
class CtPainter; ///< fwd: paint() takes the owner-provided painter (UI5)

/// (UI4) Plain-text label. NO rich text (QTextDocument is out of scope,
/// pain point P10): a single line of u16 text, drawn via the backend's
/// text primitive. Width/height hints come from backend font metrics.
class CtLabel : public CtWidgetBase {
public:
    /// Sanctioned factory (P5): create a label attached to `parent`.
    static std::shared_ptr<CtLabel> create(
        const std::shared_ptr<CtWidgetBase>& parent,
        std::u16string text = {});

    /// Constructor used by the factory (P5: ctor must not touch the tree).
    explicit CtLabel(const std::shared_ptr<CtWidgetBase>& parent,
                     std::u16string text);

    /// The label text (UTF-16; empty = no text).
    std::u16string_view text() const noexcept;

    /// Set the label text; triggers markDamage (UI4: the owner relayouts
    /// via CtBoxLayout — the label itself marks the damage; the
    /// "markDamage + relayout" design contract is satisfied by the owner's
    /// explicit relayout() after a setText).
    void setText(std::u16string text);

    /// (UI4) Paint the label into `painter` within `rect` (widget-local):
    /// the text at the top-left of `rect` (single line), colored by the
    /// style. The painter is PROVIDED by the owner (backing store —
    /// WARN-2 ruling, see CtPushButton::paint). `painter` must be active
    /// with the widget-local transform.
    void paint(CtPainter& painter, const CtRect& rect) const;

    /// (UI4) Preferred size: the backend's font metrics for the current
    /// text (one measure pass). Zero if the backend has no text support.
    CtSize sizeHint(CtPaintBackend& backend) const noexcept override;

private:
    /// The label text (UTF-16). Not on the hot path (setText is a user
    /// action).
    std::u16string m_text;
};

} // namespace crisp::ui
