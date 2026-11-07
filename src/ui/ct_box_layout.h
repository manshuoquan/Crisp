#pragma once

// ct_box_layout.h — UI4 box layout (crisp::ui).
// Explicit two-pass algorithm (docs/ui/03-rewrite-proposal.md §4.4.3):
//   Pass 1 — measure: sizeHint() of each item, sum + stretch weights;
//   Pass 2 — place:   assign final geometry per item (setGeometry).
// NO invalidate() tree recursion (pain point P7): relayout() is called
// explicitly by the owner widget when children/hints change.
//
// Part of the CRISP project. Pure C++17, zero Qt/Win32 dependency.
// The layout holds WEAK references to the widgets (the tree owns the
// strong refs); it does NOT extend any widget's lifetime.

#include <cstdint>
#include <memory>
#include <vector>

#include "ct_geometry.h"       // CtRect / CtSize
#include "ct_paint_types.h"    // CtPaintMetrics (sizeHint via fontMetrics)
#include "ct_paint_backend.h"  // CtPaintBackend (font metrics for sizeHint)
#include "ct_widget_base.h"    // CtWidgetBase::sizeHint (virtual, UI4)
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadAffinity

namespace crisp::ui {

/// (UI4) Box layout with an explicit two-pass algorithm (docs/ui/03-
/// rewrite-proposal.md §4.4.3; Qt 01 §5.1 qGeomCalc model):
///   Pass 1 — measure: sizeHint() of each item, sum + stretch weights;
///   Pass 2 — place:   assign final geometry per item (setGeometry).
/// NO invalidate() tree recursion (pain point P7): relayout() is called
/// explicitly by the owner widget when children/hints change.
class CtBoxLayout {
public:
    enum class Direction : std::uint8_t {
        kHorizontal, ///< children laid left→right
        kVertical,   ///< children laid top→bottom
    };

    /// The layout direction (fixed at construction; UI4 has no
    /// direction-change API — the owner rebuilds for a different axis).
    explicit CtBoxLayout(Direction dir) noexcept;

    /// Non-copyable (owns nested layouts via unique_ptr); movable.
    CtBoxLayout(const CtBoxLayout&) = delete;
    CtBoxLayout& operator=(const CtBoxLayout&) = delete;
    CtBoxLayout(CtBoxLayout&&) noexcept = default;
    CtBoxLayout& operator=(CtBoxLayout&&) noexcept = default;

    /// THREAD CONTRACT (P2-6 fix): the layout is affined to its creating
    /// thread. add/addWidget/relayout/sizeHint assert the calling thread
    /// owns the layout (debug abort; release degrades to a no-op). The
    /// layout's mutable item list and the placed widgets' geometry must
    /// only be touched on the UI thread.
    bool assertSameThread() const noexcept { return m_affinity.assertSameThread(); }

    /// Add a widget item with a stretch weight (0 = no extra space).
    /// Negative stretch is CLAMPED to 0 (contract). The layout stores a
    /// weak_ptr (the tree owns the widget).
    void addWidget(const std::shared_ptr<CtWidgetBase>& widget, int stretch = 0);

    /// Add a nested sub-layout (takes OWNERSHIP of the unique_ptr) with a
    /// stretch weight. Negative stretch is CLAMPED to 0 (contract). The
    /// sub-layout's sizeHint drives its measure.
    void addLayout(std::unique_ptr<CtBoxLayout> subLayout, int stretch = 0);

    /// Run both passes for the given bounding rect (widget-local
    /// coordinates; the owner supplies its content rect). Relays the
    /// placed widgets' geometry via CtWidgetBase::setGeometry. `backend`
    /// MUST be non-null (the measure pass reads widget sizeHints, which
    /// need font metrics for text widgets).
    void relayout(const CtRect& bounds, CtPaintBackend* backend) noexcept;

    /// Preferred size from pass 1 (hint aggregation) WITHOUT placing.
    /// Sum of the item sizeHints along the layout axis; the cross axis is
    /// the maximum. `backend` MUST be non-null.
    CtSize sizeHint(CtPaintBackend* backend) const noexcept;

    /// Number of items (widgets + sub-layouts).
    std::size_t itemCount() const noexcept { return m_items.size(); }

private:
    /// One layout item: a widget (weak) or a nested layout (owned).
    struct CtLayoutItem {
        std::weak_ptr<CtWidgetBase> widget; ///< the widget (weak, tree owns)
        std::unique_ptr<CtBoxLayout> layout; ///< the nested layout (owned)
        int stretch = 0;                     ///< stretch weight (0 = no extra)
        bool isLayout = false;               ///< true for a nested layout item

        /// The preferred size of this item (widget sizeHint or the
        /// nested layout's aggregated hint). `backend` must be non-null.
        CtSize hint(CtPaintBackend* backend) const noexcept {
            if (isLayout)
                return layout ? layout->sizeHint(backend) : CtSize{};
            const auto w = widget.lock();
            if (!w)
                return CtSize{}; // dead widget: zero hint (skipped at place)
            return w->sizeHint(*backend);
        }
    };

    /// The layout axis length of `size` (width for horizontal, height for
    /// vertical).
    int axisLength(const CtSize& size) const noexcept;
    /// The cross-axis length of `size` (height for horizontal, width for
    /// vertical).
    int crossLength(const CtSize& size) const noexcept;
    /// Build a size from the axis + cross lengths (inverse of the above).
    CtSize fromAxis(int axis, int cross) const noexcept;

    /// Compute one item's share of the remaining extra space (qGeomCalc
    /// model, P1-3/P1-4 fix): with stretch weights, `remainingExtra *
    /// itemStretch / remainingStretchTotal` (floor; the caller carries the
    /// remainder into the next item); when ALL stretches are 0, the extra
    /// is divided EVENLY among the live items (no floating tail gap).
    static std::int64_t distributeShare(int itemStretch,
                                        std::int64_t remainingExtra,
                                        std::size_t remainingStretchTotal,
                                        std::size_t liveCount) noexcept;

    Direction m_dir{Direction::kHorizontal}; ///< layout axis
    std::vector<CtLayoutItem> m_items;       ///< items in insertion order
    /// Embedded thread affinity (P2-6 fix: the layout is UI-thread-affined).
    crisp::event::CtThreadAffinity m_affinity;
};

} // namespace crisp::ui
