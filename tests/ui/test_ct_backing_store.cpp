// Unit tests: crisp::ui backing store (CtBackingStore, UI2 §4.3).
//
// Covers: N addDamage -> 1 flush batching (lastFlushBatchCount == N, MUST-3
// frame sequence); flush writes kPainted + clears widget damage (MUST-2
// kDamage -> kPainted pairing); flush clears the store damage + resets the
// batch count; the full hot path (addDamage + flush) performs ZERO heap
// allocations (§6.1/§6.2); kDamage -> kPainted pairing purity (no
// kPaintState pollution, WARN-A).
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

#include "ui/ct_backing_store.h"
#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_painter.h"
#include "ui/ct_widget_base.h"

#include "test_harness.h"

using crisp::ui::CtBackingStore;
using crisp::ui::CtColor;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtPoint;
using crisp::ui::CtRect;
using crisp::ui::CtRegion;
using crisp::ui::CtSize;
using crisp::ui::CtWidgetBase;
using crisp::ui::CtWidgetPhase;
using crisp::ui::CtWidgetRecord;

#include "alloc_tracking.h" // shared g_allocCount/g_counting (defined in
                            // test_ct_widget_base.cpp, the single anchor TU)

namespace {

TEST_CASE(backing_store_batches_n_damage_into_one_flush)
{
    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtBackingStore store(CtSize{200, 200}, backend);

    EXPECT_EQ(store.lastFlushBatchCount(), 0u);
    store.addDamage(CtRegion{CtRect{0, 0, 10, 10}});
    store.addDamage(CtRegion{CtRect{20, 30, 5, 5}});
    store.addDamage(CtRegion{CtRect{50, 60, 20, 20}});
    EXPECT_EQ(store.lastFlushBatchCount(), 3u); // N merges this frame

    // The dirty region is the single bounding rect of all three (D2).
    store.flush();
    EXPECT_EQ(store.lastFlushBatchCount(), 0u); // reset after flush
}

TEST_CASE(backing_store_flush_writes_kpainted_and_clears_damage)
{
    auto widget = CtWidgetBase::create();
    widget->setGeometry(CtRect{0, 0, 200, 200});

    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtBackingStore store(CtSize{200, 200}, backend);

    // MUST-3 frame sequence: markDamage -> addDamage(affected) -> painter
    // begin/draw/end (-> backend.flush) -> store.flush.
    widget->markDamage(CtRect{0, 0, 10, 10});
    EXPECT_FALSE(widget->dirtyRegion().isEmpty());

    std::vector<std::weak_ptr<CtWidgetBase>> affected{widget};
    store.addDamage(CtRegion{widget->dirtyRegion().bounds}, affected);
    EXPECT_EQ(store.lastFlushBatchCount(), 1u);

    // The painting step of the frame: the painter draws into the store's
    // backend; end() executes the commands (backend.flush).
    crisp::ui::CtPainter painter;
    EXPECT_TRUE(painter.begin(store.backend()));
    painter.fillRect(CtRect{0, 0, 10, 10}, CtColor::white());
    painter.end();

    // store.flush() writes kPainted on the affected widget + clears its
    // damage (MUST-2: kDamage -> kPainted pairing). It does NOT repaint
    // (the drawing already happened in painter.end() -> backend.flush).
    store.flush();

    EXPECT_TRUE(widget->dirtyRegion().isEmpty()); // clearDamage called

    // The widget's trace now contains kDamage (from markDamage) followed
    // by kPainted (from flush): the pairing is complete.
    crisp::ui::CtWidgetRecord out[8];
    const std::size_t n = widget->trace().copyOut(out, 8);
    EXPECT_GE(n, 2u);
    EXPECT_EQ(static_cast<int>(out[n - 2].phase),
              static_cast<int>(CtWidgetPhase::kDamage));
    EXPECT_EQ(static_cast<int>(out[n - 1].phase),
              static_cast<int>(CtWidgetPhase::kPainted));
    EXPECT_EQ(out[n - 1].damageRectCount, 0u); // O5: no batch count on kPainted
}

TEST_CASE(backing_store_flush_clears_damage)
{
    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtBackingStore store(CtSize{200, 200}, backend);

    store.addDamage(CtRegion{CtRect{0, 0, 10, 10}});
    store.addDamage(CtRegion{CtRect{20, 30, 5, 5}});
    store.flush();

    // flush() clears the store-side damage: a second flush() presents
    // nothing (the store's dirty region is empty -> present not called).
    // (present is a no-op on the PixelBuffer backend anyway; the batch
    // count proves the frame is over.)
    EXPECT_EQ(store.lastFlushBatchCount(), 0u);
}

TEST_CASE(backing_store_hot_path_allocates_zero)
{
    auto widget = CtWidgetBase::create();
    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtBackingStore store(CtSize{200, 200}, backend);

    // Warm-up: construct everything + one full frame so the store's
    // reserved list and the widget's ring are set up (allocations BEFORE
    // arming, §6.2).
    std::vector<std::weak_ptr<CtWidgetBase>> affected{widget};
    store.addDamage(CtRegion{CtRect{0, 0, 5, 5}}, affected);
    store.flush();

    // Armed hot path: N addDamage + 1 flush must perform ZERO heap
    // allocations (§6.1: addDamage is O(1) single-rect union + bounded
    // list push; flush only presents + bookkeeping).
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 100; ++i)
        store.addDamage(CtRegion{CtRect{i % 10, 0, 5, 5}}, affected);
    store.flush();
    g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = g_allocCount.load(std::memory_order_relaxed);

    EXPECT_EQ(allocs, 0u);
    EXPECT_EQ(store.lastFlushBatchCount(), 0u); // reset after flush
    EXPECT_FALSE(store.overflowed());
}

TEST_CASE(backing_store_flush_kpainted_pair_has_no_paint_state)
{
    auto widget = CtWidgetBase::create();
    widget->setGeometry(CtRect{0, 0, 200, 200});

    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtBackingStore store(CtSize{200, 200}, backend);

    // The painter's trace sink is a SEPARATE ring (WARN-A): the stack-
    // depth records land THERE, never inside the widget's own kDamage ->
    // kPainted pairing (the pairing stays pure, §7.2).
    crisp::ui::CtWidgetTrace painterTrace;
    crisp::ui::CtPainter painter(&painterTrace);

    widget->markDamage(CtRect{0, 0, 10, 10});
    std::vector<std::weak_ptr<CtWidgetBase>> affected{widget};
    store.addDamage(CtRegion{widget->dirtyRegion().bounds}, affected);

    // MUST-3 frame sequence with the sink wired in.
    EXPECT_TRUE(painter.begin(store.backend()));
    painter.save();
    painter.restore();
    painter.fillRect(CtRect{0, 0, 10, 10}, CtColor::white());
    painter.end(); // -> backend.flush(damage)
    store.flush(); // -> kPainted + clearDamage

    // The widget's ring has only its OWN records: kCreated, kDamage,
    // kPainted — NO kPaintState inside the kDamage -> kPainted pair
    // (pairing purity, SUGGEST; WARN-A).
    crisp::ui::CtWidgetRecord out[8];
    const std::size_t n = widget->trace().copyOut(out, 8);
    bool seenDamage = false;
    bool sawPaintStateInPair = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i].phase == CtWidgetPhase::kDamage) {
            seenDamage = true;
        } else if (out[i].phase == CtWidgetPhase::kPainted) {
            break; // the pair closed; nothing else may sit inside it
        } else if (seenDamage && out[i].phase == CtWidgetPhase::kPaintState) {
            sawPaintStateInPair = true; // pollution
        }
    }
    EXPECT_FALSE(sawPaintStateInPair);
    EXPECT_TRUE(seenDamage);
    EXPECT_TRUE(widget->dirtyRegion().isEmpty());

    // The painter's OWN sink ring carries the kPaintState records (the
    // stack-depth observability channel, WARN-A).
    crisp::ui::CtWidgetRecord sinkOut[8];
    const std::size_t m = painterTrace.copyOut(sinkOut, 8);
    EXPECT_GE(m, 2u);
    bool sawPaintState = false;
    for (std::size_t i = 0; i < m; ++i) {
        if (sinkOut[i].phase == CtWidgetPhase::kPaintState)
            sawPaintState = true;
    }
    EXPECT_TRUE(sawPaintState);
}

} // namespace (anonymous)
