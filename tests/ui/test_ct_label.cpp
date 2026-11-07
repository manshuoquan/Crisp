// Unit tests: crisp::ui label (CtLabel, UI4 §4.4.1).
//
// Covers the plain-text label: text storage, setText + damage, and the
// backend font-metrics sizeHint (PixelBuffer fixed metrics: 8px/char,
// 16px line).
//
// Hand-written test doubles, no mock framework (project rule).

#include <memory>
#include <string>

#include "ui/ct_label.h"
#include "ui/ct_paint_backend_pixel.h"

#include "test_harness.h"

using crisp::ui::CtLabel;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtRect;
using crisp::ui::CtSize;
using crisp::ui::CtWidgetBase;

namespace {

TEST_CASE(label_text_storage)
{
    auto root = CtWidgetBase::create();
    auto label = CtLabel::create(root, u"Hello");
    label->setGeometry(CtRect{0, 0, 100, 20});
    EXPECT_EQ(label->text(), u"Hello");

    label->setText(u"World");
    EXPECT_EQ(label->text(), u"World");
    // setText marks the widget damaged.
    EXPECT_FALSE(label->dirtyRegion().isEmpty());
    label->clearDamage();
}

TEST_CASE(label_set_text_same_is_noop)
{
    auto root = CtWidgetBase::create();
    auto label = CtLabel::create(root, u"x");
    label->setGeometry(CtRect{0, 0, 100, 20});
    label->setText(u"x");
    EXPECT_TRUE(label->dirtyRegion().isEmpty()); // no change, no damage
}

TEST_CASE(label_size_hint_from_backend_metrics)
{
    auto root = CtWidgetBase::create();
    auto label = CtLabel::create(root, u"abcd"); // 4 chars
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    // UI5: the hint = text metrics + the default style's frame
    // (2*8 horizontal + 2*1 border, 2*4 vertical + 2*1 border).
    const CtSize hint = label->sizeHint(backend);
    EXPECT_EQ(hint.width, 4 * 8 + 2 * 8 + 2 * 1);   // text + frame
    EXPECT_EQ(hint.height, 16 + 2 * 4 + 2 * 1);     // line + frame
}

TEST_CASE(label_empty_text_zero_size_hint)
{
    auto root = CtWidgetBase::create();
    auto label = CtLabel::create(root);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});
    const CtSize hint = label->sizeHint(backend);
    EXPECT_EQ(hint.width, 2 * 8 + 2 * 1);  // empty text: frame only
    EXPECT_EQ(hint.height, 16 + 2 * 4 + 2 * 1); // line height + frame
}

} // namespace (anonymous)
