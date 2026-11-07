// Unit tests: crisp::ui basic style + widget paint via style (UI5 §4.5).
//
// Covers: deterministic per-state palette mapping (table-driven), the
// style's sizeHint frame contribution, and the P3 contract — a control
// paints ONLY through the style (same widget, different style → different
// pixels, asserted on the PixelBuffer backend).
//
// Hand-written test doubles, no mock framework (project rule).

#include <memory>
#include <string>

#include "ui/ct_style_basic.h"
#include "ui/ct_push_button.h"
#include "ui/ct_label.h"
#include "ui/ct_line_edit.h"
#include "ui/ct_paint_backend.h"
#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_painter.h"
#include "ui/ct_widget_base.h"

#include "test_harness.h"

using crisp::ui::CtBasicStyle;
using crisp::ui::CtButtonState;
using crisp::ui::CtColor;
using crisp::ui::CtLabel;
using crisp::ui::CtLineEdit;
using crisp::ui::CtPaintBackend;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtPainter;
using crisp::ui::CtPushButton;
using crisp::ui::CtRect;
using crisp::ui::CtSize;
using crisp::ui::CtStyle;
using crisp::ui::CtWidgetBase;

namespace {

/// A custom style with a distinctive palette (same interface, different
/// data — the P3 mock-free assertion driver).
class CtDistinctStyle final : public CtStyle {
public:
    CtColor textColor(const CtWidgetBase&) const noexcept override {
        return CtColor{10, 20, 30, 255};
    }
    CtColor backgroundColor(const CtWidgetBase&) const noexcept override {
        return CtColor{200, 100, 50, 255};
    }
    CtColor borderColor(const CtWidgetBase&) const noexcept override {
        return CtColor{1, 2, 3, 255};
    }
    int borderWidth(const CtWidgetBase&) const noexcept override { return 2; }
    CtColor textColorForState(const CtWidgetBase&,
                              CtButtonState state) const noexcept override {
        return (state == CtButtonState::kDisabled)
                   ? CtColor{90, 90, 90, 255}
                   : CtColor{10, 20, 30, 255};
    }
    CtSize sizeHint(const CtWidgetBase&) const noexcept override {
        return CtSize{20, 10};
    }
};

TEST_CASE(style_basic_palette_deterministic)
{
    CtBasicStyle style;
    // Table-driven: each state maps to the fixed palette color.
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"X");
    btn->setEnabled(true);

    EXPECT_EQ(style.textColor(*btn), CtBasicStyle::kDefaultPalette.textNormal);
    EXPECT_EQ(style.backgroundColor(*btn),
              CtBasicStyle::kDefaultPalette.background);
    EXPECT_EQ(style.borderColor(*btn), CtBasicStyle::kDefaultPalette.border);
    EXPECT_EQ(style.borderWidth(*btn),
              CtBasicStyle::kDefaultPalette.borderWidth);

    // Per-state: hovered/pressed/disabled map deterministically.
    EXPECT_EQ(style.textColorForState(
                  *btn, CtPushButton::CtButtonState::kHovered),
              CtBasicStyle::kDefaultPalette.textHovered);
    EXPECT_EQ(style.textColorForState(
                  *btn, CtPushButton::CtButtonState::kPressed),
              CtBasicStyle::kDefaultPalette.textPressed);
    EXPECT_EQ(style.textColorForState(
                  *btn, CtPushButton::CtButtonState::kDisabled),
              CtBasicStyle::kDefaultPalette.textDisabled);

    // Disabled widget: textColor reports the disabled palette entry.
    btn->setEnabled(false);
    EXPECT_EQ(style.textColor(*btn), CtBasicStyle::kDefaultPalette.textDisabled);
}

TEST_CASE(style_basic_size_hint_is_padding_frame)
{
    CtBasicStyle style;
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"X");
    const CtSize frame = style.sizeHint(*btn);
    EXPECT_EQ(frame.width, 2 * CtBasicStyle::kDefaultPalette.horizontalPadding
                               + 2 * CtBasicStyle::kDefaultPalette.borderWidth);
    EXPECT_EQ(frame.height, 2 * CtBasicStyle::kDefaultPalette.verticalPadding
                                + 2 * CtBasicStyle::kDefaultPalette.borderWidth);
}

TEST_CASE(style_button_size_hint_adds_frame)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"abcd"); // 4 chars = 32px wide
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    // Default style frame: 2*8 horizontal padding + 2*1 border;
    // 2*4 vertical padding + 2*1 border (WARN-1: the border is reserved).
    const CtSize hint = btn->sizeHint(backend);
    EXPECT_EQ(hint.width, 4 * 8 + 2 * 8 + 2 * 1);
    EXPECT_EQ(hint.height, 16 + 2 * 4 + 2 * 1);
}

/// Paint a widget into `backend` via a local painter session (the UI5
/// paint contract: the OWNER provides the painter; the widget records
/// into it).
template <typename Widget>
void paintWidget(Widget& w, CtPaintBackend& backend) {
    CtPainter painter;
    if (!painter.begin(backend))
        return;
    w.paint(painter, w.geometry());
    painter.end();
}

TEST_CASE(style_button_paints_background_and_text_via_style)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"OK");
    btn->setGeometry(CtRect{0, 0, 40, 24});
    CtPaintBackendPixelBuffer backend(CtSize{100, 60});

    // Paint through the DEFAULT style.
    paintWidget(*btn, backend);

    // The button's background (top-left corner) is the default palette.
    const CtColor bg = backend.pixel(1, 1);
    EXPECT_EQ(bg, CtBasicStyle::kDefaultPalette.background);

    // The border is the default palette border (1px ring): pixel (0,0).
    EXPECT_EQ(backend.pixel(0, 0), CtBasicStyle::kDefaultPalette.border);

    // The text area (center) is NOT the background (text pixels drawn).
    const CtColor mid = backend.pixel(20, 12);
    EXPECT_NE(mid, CtBasicStyle::kDefaultPalette.background);
}

TEST_CASE(style_same_widget_different_style_different_pixels)
{
    auto root = CtWidgetBase::create();
    auto btn = CtPushButton::create(root, u"OK");
    btn->setGeometry(CtRect{0, 0, 40, 24});

    // Paint with the DEFAULT style.
    CtPaintBackendPixelBuffer backendDefault(CtSize{100, 60});
    paintWidget(*btn, backendDefault);
    const CtColor defaultBg = backendDefault.pixel(1, 1);
    const CtColor defaultBorder = backendDefault.pixel(0, 0);

    // Paint the SAME widget with a DIFFERENT style (P3: the control only
    // asks the style — different style → different pixels, mock-free).
    CtDistinctStyle distinct;
    btn->setStyle(&distinct);
    CtPaintBackendPixelBuffer backendDistinct(CtSize{100, 60});
    paintWidget(*btn, backendDistinct);
    const CtColor distinctBg = backendDistinct.pixel(1, 1);
    const CtColor distinctBorder = backendDistinct.pixel(0, 0);

    EXPECT_NE(defaultBg, distinctBg);
    EXPECT_NE(defaultBorder, distinctBorder);
}

TEST_CASE(style_line_edit_paints_caret_when_focused)
{
    auto root = CtWidgetBase::create();
    auto edit = CtLineEdit::create(root, u"abc");
    edit->setGeometry(CtRect{0, 0, 60, 24});
    edit->setCursorPos(3u);

    // Focused: the caret bar (1px at col 3 * 8px + padding) is drawn.
    edit->setFocusFlag(true);
    CtPaintBackendPixelBuffer backendFocus(CtSize{100, 60});
    paintWidget(*edit, backendFocus);

    // Not focused: no caret pixel.
    edit->setFocusFlag(false);
    CtPaintBackendPixelBuffer backendNoFocus(CtSize{100, 60});
    paintWidget(*edit, backendNoFocus);

    // The caret x = textX (9, after the 18px frame/2) + 3*8 (24) = 33;
    // compare a vertical column JUST at the caret bar (x=33), inside the
    // text band (y=12).
    const CtColor caretFocus = backendFocus.pixel(33, 12);
    const CtColor caretNoFocus = backendNoFocus.pixel(33, 12);
    EXPECT_NE(caretFocus, caretNoFocus);
}

TEST_CASE(style_label_paints_text_with_style_color)
{
    auto root = CtWidgetBase::create();
    auto label = CtLabel::create(root, u"Hi");
    label->setGeometry(CtRect{0, 0, 40, 20});

    CtPaintBackendPixelBuffer backend(CtSize{100, 60});
    paintWidget(*label, backend);

    // The label draws only text (no background fill): the area OUTSIDE
    // the text span stays transparent (0,0,0,0), the text span is filled
    // with the style's text color. Assert the first glyph cell pixel.
    const CtColor textPx = backend.pixel(1, 5);
    EXPECT_EQ(textPx, CtBasicStyle::kDefaultPalette.textNormal);
}

} // namespace (anonymous)
