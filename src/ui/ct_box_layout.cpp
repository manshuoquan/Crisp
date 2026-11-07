// ct_box_layout.cpp — UI4 box layout implementation (crisp::ui).
//
// Explicit two-pass algorithm (docs/ui/03-rewrite-proposal.md §4.4.3):
// pass 1 measures every item's hint, pass 2 places each item's geometry
// (setGeometry). Stretch weights distribute the extra space; items with
// no hint (zero) get their hint size and no extra. The layout holds weak
// references to widgets (the tree owns the strong refs) and OWNS nested
// sub-layouts (unique_ptr). No tree recursion, no invalidate().

#include "ct_box_layout.h"

#include <algorithm>
#include <utility>

#include "ct_widget_base.h" // CtWidgetBase::setGeometry / sizeHint (UI4)

namespace crisp::ui {

CtBoxLayout::CtBoxLayout(Direction dir) noexcept : m_dir(dir) {}

void CtBoxLayout::addWidget(const std::shared_ptr<CtWidgetBase>& widget,
                            int stretch) {
    if (!assertSameThread())
        return; // release: degrade to no-op
    if (!widget)
        return; // null widget: ignore (defensive, no tree effect)
    CtLayoutItem item;
    item.widget = widget;
    item.stretch = stretch > 0 ? stretch : 0;
    item.isLayout = false;
    m_items.push_back(std::move(item));
}

void CtBoxLayout::addLayout(std::unique_ptr<CtBoxLayout> subLayout,
                            int stretch) {
    if (!assertSameThread())
        return; // release: degrade to no-op
    if (!subLayout)
        return; // null layout: ignore
    CtLayoutItem item;
    item.layout = std::move(subLayout);
    item.stretch = stretch > 0 ? stretch : 0;
    item.isLayout = true;
    m_items.push_back(std::move(item));
}

void CtBoxLayout::relayout(const CtRect& bounds,
                           CtPaintBackend* backend) noexcept {
    if (!assertSameThread())
        return; // release: degrade to no-op
    if (!backend)
        return; // measure requires a backend (contract)
    // Pass 1 — measure: hint per item along the axis; the cross axis is
    // the item's own cross hint (the layout stretches items to the
    // container's cross size).
    const int axisTotal = axisLength(CtSize{bounds.width, bounds.height});

    // Gather the hints once (dead items contribute zero and are skipped at
    // placement). 64-bit accumulation (P1-2 fix): a hint total beyond
    // INT_MAX must not wrap and corrupt the extra computation.
    std::vector<CtSize> hints(m_items.size());
    std::int64_t totalHint = 0;
    std::size_t stretchTotal = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        hints[i] = m_items[i].hint(backend);
        totalHint += std::max(0, axisLength(hints[i]));
        stretchTotal += static_cast<std::size_t>(
            std::max(0, m_items[i].stretch));
    }

    // Extra space to distribute: the container axis minus the total hint.
    // When the hints already exceed the container, the extra is zero
    // (items keep their hint size; no shrinking in UI4).
    const std::int64_t extra64 =
        static_cast<std::int64_t>(axisTotal) > totalHint
            ? static_cast<std::int64_t>(axisTotal) - totalHint
            : 0;

    // Pass 2 — place: each item gets its hint axis length plus its share
    // of the extra. Share rule (P1-3 fix, qGeomCalc model): a REMAINING-
    // based distribution — each item gets `remainingExtra * stretch /
    // remainingStretchTotal` (integer floor), so the leftover pixel from a
    // division is carried into the NEXT item's share instead of being
    // dropped at the end. When all stretches are 0 (P1-4 fix), the extra is
    // distributed EVENLY among the live items so no "floating gap" is left
    // at the tail. The cross axis is the container's cross size.
    const int cross = crossLength(CtSize{bounds.width, bounds.height});
    int pos = 0; // running cursor ALONG the layout axis (0 = bounds origin)

    std::int64_t extraRemaining = extra64;
    std::size_t stretchRemaining = stretchTotal;
    std::size_t liveCount = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i)
        if (m_items[i].isLayout ? (m_items[i].layout != nullptr)
                                : (!m_items[i].widget.expired()))
            ++liveCount;

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isLayout) {
            if (!m_items[i].layout)
                continue;
            const int hintLen = std::max(0, axisLength(hints[i]));
            const std::int64_t share = distributeShare(
                m_items[i].stretch, extraRemaining, stretchRemaining,
                liveCount);
            extraRemaining -= share;
            if (stretchRemaining > 0)
                stretchRemaining -= static_cast<std::size_t>(
                    std::max(0, m_items[i].stretch));
            if (liveCount > 0)
                --liveCount;
            const std::int64_t len = static_cast<std::int64_t>(hintLen) + share;
            const int lenInt = static_cast<int>(len); // contract: fits int
            const CtRect sub =
                (m_dir == Direction::kHorizontal)
                    ? CtRect{bounds.x + pos, bounds.y, lenInt, cross}
                    : CtRect{bounds.x, bounds.y + pos, cross, lenInt};
            m_items[i].layout->relayout(sub, backend);
            pos += lenInt;
            continue;
        }
        const auto w = m_items[i].widget.lock();
        if (!w)
            continue; // dead widget: no placement (skipped)
        const int hintLen = std::max(0, axisLength(hints[i]));
        const std::int64_t share = distributeShare(
            m_items[i].stretch, extraRemaining, stretchRemaining,
            liveCount);
        extraRemaining -= share;
        if (stretchRemaining > 0)
            stretchRemaining -= static_cast<std::size_t>(
                std::max(0, m_items[i].stretch));
        if (liveCount > 0)
            --liveCount;
        const std::int64_t len = static_cast<std::int64_t>(hintLen) + share;
        const int lenInt = static_cast<int>(len); // contract bound: fits int
        if (m_dir == Direction::kHorizontal) {
            w->setGeometry(CtRect{bounds.x + pos, bounds.y,
                                  lenInt, cross});
        } else {
            w->setGeometry(CtRect{bounds.x, bounds.y + pos,
                                  cross, lenInt});
        }
        pos += lenInt;
    }
    (void)pos; // pos == axisTotal when every item is placed (contract)
}

CtSize CtBoxLayout::sizeHint(CtPaintBackend* backend) const noexcept {
    if (!assertSameThread())
        return CtSize{}; // release: degrade to no-op
    if (!backend)
        return CtSize{};
    int axis = 0;
    int cross = 0;
    for (const auto& item : m_items) {
        const CtSize h = item.hint(backend);
        axis += std::max(0, axisLength(h));
        cross = std::max(cross, crossLength(h));
    }
    return fromAxis(axis, cross);
}

int CtBoxLayout::axisLength(const CtSize& size) const noexcept {
    return m_dir == Direction::kHorizontal ? size.width : size.height;
}

int CtBoxLayout::crossLength(const CtSize& size) const noexcept {
    return m_dir == Direction::kHorizontal ? size.height : size.width;
}

std::int64_t CtBoxLayout::distributeShare(
    int itemStretch, std::int64_t remainingExtra,
    std::size_t remainingStretchTotal, std::size_t liveCount) noexcept {
    if (remainingExtra <= 0)
        return 0;
    if (remainingStretchTotal > 0) {
        const std::int64_t s =
            remainingExtra * static_cast<std::int64_t>(
                std::max(0, itemStretch)) /
            static_cast<std::int64_t>(remainingStretchTotal);
        return s;
    }
    if (liveCount == 0)
        return 0;
    // Even split (floor); the caller carries the remainder forward.
    return remainingExtra / static_cast<std::int64_t>(liveCount);
}

CtSize CtBoxLayout::fromAxis(int axis, int cross) const noexcept {
    return m_dir == Direction::kHorizontal ? CtSize{axis, cross}
                                           : CtSize{cross, axis};
}

} // namespace crisp::ui
