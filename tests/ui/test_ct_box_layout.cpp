// Unit tests: crisp::ui box layout (CtBoxLayout, UI4 §4.4.3).
//
// Covers the explicit two-pass algorithm: measure (sizeHint aggregation)
// + place (setGeometry). Golden hand-computed cases: stretch weight
// distribution, nested layouts, dead-widget skipping.
//
// Hand-written test doubles, no mock framework (project rule).

#include <memory>
#include <vector>

#include "ui/ct_box_layout.h"
#include "ui/ct_label.h"
#include "ui/ct_paint_backend.h"
#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_widget_base.h"

#include "test_harness.h"

using crisp::ui::CtBoxLayout;
using crisp::ui::CtLabel;
using crisp::ui::CtPaintBackend;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtRect;
using crisp::ui::CtSize;
using crisp::ui::CtWidgetBase;

namespace {

/// A plain widget with a fixed sizeHint (test double for layout items).
class CtFixedWidget : public CtWidgetBase {
public:
    explicit CtFixedWidget(const std::shared_ptr<CtWidgetBase>& parent,
                           CtSize hint)
        : CtWidgetBase(parent), m_hint(hint) {}

    CtSize sizeHint(CtPaintBackend&) const noexcept override { return m_hint; }

private:
    CtSize m_hint;
};

std::shared_ptr<CtFixedWidget> fixed(const std::shared_ptr<CtWidgetBase>& root,
                                     int w, int h) {
    return CtWidgetBase::createWidget<CtFixedWidget>(root, CtSize{w, h});
}

TEST_CASE(box_layout_places_children_along_axis)
{
    auto root = CtWidgetBase::create();
    auto a = fixed(root, 20, 10);
    auto b = fixed(root, 30, 10);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    CtBoxLayout layout(CtBoxLayout::Direction::kHorizontal);
    layout.addWidget(a, 0);
    layout.addWidget(b, 0);

    // Container 100 wide, hints total 50, extra 50. With all stretches 0
    // (P1-4 fix), the extra is split EVENLY among the live items: each gets
    // +25, so a = 45, b = 55 — the layout fills the container (no floating
    // tail gap).
    layout.relayout(CtRect{0, 0, 100, 20}, &backend);
    EXPECT_EQ(a->geometry().x, 0);
    EXPECT_EQ(a->geometry().width, 45);
    EXPECT_EQ(b->geometry().x, 45);
    EXPECT_EQ(b->geometry().width, 55);
    EXPECT_EQ(b->geometry().right(), 100);
    // Cross axis: the container's height.
    EXPECT_EQ(a->geometry().height, 20);
    EXPECT_EQ(b->geometry().height, 20);
}

TEST_CASE(box_layout_stretch_distributes_extra_space)
{
    auto root = CtWidgetBase::create();
    auto a = fixed(root, 20, 10);
    auto b = fixed(root, 20, 10);
    auto c = fixed(root, 20, 10);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    CtBoxLayout layout(CtBoxLayout::Direction::kHorizontal);
    layout.addWidget(a, 1); // 1/3 of the extra
    layout.addWidget(b, 2); // 2/3 of the extra
    layout.addWidget(c, 0); // no extra

    // Container 80 wide: hints total 60, extra 20.
    layout.relayout(CtRect{0, 0, 80, 20}, &backend);
    // Remainder-carrying stretch (qGeomCalc, P1-3): a = 20 + 20*1/3 = 26,
    // b = 20 + 14*2/2 = 34, c = 20 (no extra). The leftover pixel from the
    // first division is carried into b — the layout ends EXACTLY at 80.
    EXPECT_EQ(a->geometry().width, 26);
    EXPECT_EQ(b->geometry().width, 34);
    EXPECT_EQ(c->geometry().width, 20);
    EXPECT_EQ(a->geometry().x, 0);
    EXPECT_EQ(b->geometry().x, 26);
    EXPECT_EQ(c->geometry().x, 60);
    EXPECT_EQ(c->geometry().right(), 80); // no floating tail gap (P1-4)
}

TEST_CASE(box_layout_nested_sub_layout)
{
    auto root = CtWidgetBase::create();
    auto innerA = fixed(root, 10, 5);
    auto innerB = fixed(root, 10, 5);
    auto outerWidget = fixed(root, 40, 5);

    auto inner = std::make_unique<CtBoxLayout>(
        CtBoxLayout::Direction::kHorizontal);
    inner->addWidget(innerA, 0);
    inner->addWidget(innerB, 0);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    CtBoxLayout outerLayout(CtBoxLayout::Direction::kVertical);
    outerLayout.addWidget(outerWidget, 0);
    outerLayout.addLayout(std::move(inner), 0);

    // Nested layout places its items when the outer relayouts. The outer
    // is VERTICAL: hints total 5+5=10 in a 50-high container → extra 40 is
    // split evenly (P1-4): each item gets +20, so outerWidget height = 25
    // (y 0..25) and the inner layout sits below it (y 25, height 25).
    // The inner sub-rect is 100 wide, 25 high; hints 10+10=20 → extra 80
    // split evenly: each inner item gets +40 (width 50).
    outerLayout.relayout(CtRect{0, 0, 100, 50}, &backend);
    EXPECT_EQ(innerA->geometry().x, 0);   // inner's own origin (x=0)
    EXPECT_EQ(innerA->geometry().y, 25);  // below outerWidget
    EXPECT_EQ(innerA->geometry().width, 50);
    EXPECT_EQ(innerB->geometry().x, 50);
    EXPECT_EQ(innerB->geometry().y, 25);
    EXPECT_EQ(innerB->geometry().width, 50);
    EXPECT_EQ(innerB->geometry().right(), 100); // fills the inner sub
    EXPECT_EQ(outerWidget->geometry().y, 0); // first item: top
    EXPECT_EQ(outerWidget->geometry().height, 25);
    EXPECT_EQ(outerWidget->geometry().width, 100); // vertical: cross = width
}

TEST_CASE(box_layout_size_hint_aggregates)
{
    auto root = CtWidgetBase::create();
    auto a = fixed(root, 10, 20);
    auto b = fixed(root, 30, 5);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});

    CtBoxLayout layout(CtBoxLayout::Direction::kHorizontal);
    layout.addWidget(a, 0);
    layout.addWidget(b, 0);

    // Horizontal: axis = sum of widths (40), cross = max height (20).
    const CtSize hint = layout.sizeHint(&backend);
    EXPECT_EQ(hint.width, 40);
    EXPECT_EQ(hint.height, 20);
}

TEST_CASE(box_layout_dead_widget_skipped)
{
    auto root = CtWidgetBase::create();
    auto live = fixed(root, 20, 10);
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});
    {
        auto dead = fixed(root, 30, 10); // destroyed at scope end
        CtBoxLayout layout(CtBoxLayout::Direction::kHorizontal);
        layout.addWidget(live, 0);
        layout.addWidget(dead, 0);
        layout.relayout(CtRect{0, 0, 60, 20}, &backend);
        // Both alive: hints 20+30=50, extra 10 even-split -> +5 each.
        EXPECT_EQ(live->geometry().x, 0);
        EXPECT_EQ(live->geometry().width, 25);
    } // dead destroyed: the layout holds only weak refs
    // After the dead widget is gone, a relayout skips it (zero hint): the
    // single live item gets the whole container (60).
    CtBoxLayout layout(CtBoxLayout::Direction::kHorizontal);
    layout.addWidget(live, 0);
    layout.relayout(CtRect{0, 0, 60, 20}, &backend);
    EXPECT_EQ(live->geometry().x, 0);
    EXPECT_EQ(live->geometry().width, 60);
}

} // namespace (anonymous)
