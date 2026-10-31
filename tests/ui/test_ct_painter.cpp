// Unit tests: crisp::ui painter state machine (CtPainter, UI2 §4.1).
//
// Covers: begin/end pairing + idempotence + overflow reset; save/restore
// stack balance (incl. empty-stack no-op and full-stack overflow);
// translate-at-record-time (D3); clip intersection; record-style command
// buffer (fillRect/drawText append, u16string_view refs); hot-path ZERO
// heap allocation for fillRect and drawText (§6.2 / MUST-1); non-UI
// thread begin rejection via the backend size() guard (WARN-E).
//
// Hand-written test doubles, no mock framework (project rule).

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "ui/ct_painter.h"
#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_widget_trace.h"

#include "test_harness.h"

using crisp::ui::CtColor;
using crisp::ui::CtFont;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtPainter;
using crisp::ui::CtPoint;
using crisp::ui::CtRect;
using crisp::ui::CtSize;
using crisp::ui::CtWidgetPhase;
using crisp::ui::CtWidgetRecord;
using crisp::ui::CtWidgetTrace;

#include "alloc_tracking.h" // shared g_allocCount/g_counting (defined in
                            // test_ct_widget_base.cpp, the single anchor TU)

namespace {
std::shared_ptr<CtPaintBackendPixelBuffer> g_foreignBackend; ///< worker-created (test)
} // namespace

namespace {

/// Find the first trace record with the given phase; returns true + fills
/// `out` on a match.
bool findRecord(const CtWidgetTrace& trace, CtWidgetPhase phase,
                CtWidgetRecord& out) {
    std::vector<CtWidgetRecord> recs(trace.size());
    const std::size_t n = trace.copyOut(recs.data(), recs.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (recs[i].phase == phase) {
            out = recs[i];
            return true;
        }
    }
    return false;
}

TEST_CASE(painter_begin_end_pairs)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtPainter painter;

    // end() before begin() is idempotent (no-op, safe).
    painter.end();
    EXPECT_FALSE(painter.isActive());

    // begin on a zero-size backend refuses.
    CtPaintBackendPixelBuffer emptyBackend(CtSize{0, 0});
    EXPECT_FALSE(painter.begin(emptyBackend));

    // begin succeeds on a valid backend.
    EXPECT_TRUE(painter.begin(backend));
    EXPECT_TRUE(painter.isActive());

    // repeated begin while active refuses WITHOUT overwriting the session.
    EXPECT_FALSE(painter.begin(backend));
    EXPECT_TRUE(painter.isActive());

    // end() closes the session; begin works again.
    painter.end();
    EXPECT_FALSE(painter.isActive());
    EXPECT_TRUE(painter.begin(backend));
    painter.end();
}

TEST_CASE(painter_save_restore_stack_balance)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtPainter painter;
    EXPECT_TRUE(painter.begin(backend));
    EXPECT_EQ(painter.stackDepth(), 0u);

    // save pushes; restore pops; depth balances.
    painter.save();
    EXPECT_EQ(painter.stackDepth(), 1u);
    painter.save();
    EXPECT_EQ(painter.stackDepth(), 2u);
    painter.restore();
    EXPECT_EQ(painter.stackDepth(), 1u);
    painter.restore();
    EXPECT_EQ(painter.stackDepth(), 0u);

    // Empty-stack restore is a documented no-op (no abort, depth stays 0).
    painter.restore();
    EXPECT_EQ(painter.stackDepth(), 0u);

    // Full stack (kMaxStackDepth = 32): the 33rd save sets overflowed
    // (WARN-1 ruling; debug asserts — abort — release no-op + flag).
    // Audit K3: the debug path aborts (assert(false)), so the overflow
    // assertion is RELEASE-ONLY; in debug builds we verify the 32-deep
    // stack is reached without overflowing (the assert path is the
    // documented contract, §14).
    for (int i = 0; i < 32; ++i)
        painter.save();
    EXPECT_EQ(painter.stackDepth(), 32u);
    EXPECT_FALSE(painter.overflowed());
#ifndef NDEBUG
    // Debug: the 33rd save would abort — do NOT call it here (contract:
    // debug abort, §14). We just verify the stack is full.
#else
    painter.save(); // overflow (release: no-op + flag)
    EXPECT_TRUE(painter.overflowed());
    EXPECT_EQ(painter.stackDepth(), 32u); // no push happened
#endif

    // end() resets the overflow flag (WARN-5 ruling).
    painter.end();
    EXPECT_FALSE(painter.overflowed());
}

TEST_CASE(painter_translate_applies_to_fill_rect)
{
    CtPaintBackendPixelBuffer backend(CtSize{200, 200});
    CtPainter painter;
    EXPECT_TRUE(painter.begin(backend));

    painter.translate(10, 20);
    painter.fillRect(CtRect{5, 5, 30, 40}, CtColor::white());
    EXPECT_EQ(painter.commandCount(), 1u);

    // end() flushes; the command was recorded in DEVICE coordinates (D3):
    // translate(10,20) + rect(5,5,30,40) -> pixels at (15,25).
    painter.end();
    EXPECT_EQ(backend.pixel(15, 25).r, 255);  // inside the rect (white)
    EXPECT_EQ(backend.pixel(14, 24).r, 0);    // outside (before translate)
    EXPECT_EQ(backend.pixel(44, 64).r, 255);  // inside (right/bottom edges)
    EXPECT_EQ(backend.pixel(45, 65).r, 0);    // outside (edges exclusive)
}

TEST_CASE(painter_clip_intersects_correctly)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtPainter painter;
    EXPECT_TRUE(painter.begin(backend));

    // Clip to (10,10,50,50); a fill rect that straddles the clip is
    // intersected at record time (D3): only the intersection draws.
    painter.clip(CtRect{10, 10, 50, 50});
    painter.fillRect(CtRect{0, 0, 100, 100}, CtColor::white());
    painter.end();

    EXPECT_EQ(backend.pixel(9, 9).r, 0);    // outside clip: untouched
    EXPECT_EQ(backend.pixel(10, 10).r, 255); // inside clip: white
    EXPECT_EQ(backend.pixel(59, 59).r, 255);
    EXPECT_EQ(backend.pixel(60, 60).r, 0);  // clip right edge exclusive

    // clip(empty) => subsequent fills produce ZERO visible pixels.
    CtPaintBackendPixelBuffer backend2(CtSize{100, 100});
    CtPainter painter2;
    EXPECT_TRUE(painter2.begin(backend2));
    painter2.clip(CtRect{}); // empty clip: nothing draws (§14)
    painter2.fillRect(CtRect{0, 0, 100, 100}, CtColor::white());
    EXPECT_EQ(painter2.commandCount(), 0u); // command rejected at record
    painter2.end();
    EXPECT_EQ(backend2.pixel(50, 50).r, 0);
}

TEST_CASE(painter_fill_rect_records_command)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtPainter painter;
    EXPECT_TRUE(painter.begin(backend));
    EXPECT_EQ(painter.commandCount(), 0u);

    painter.fillRect(CtRect{0, 0, 10, 10}, CtColor::black());
    painter.fillRect(CtRect{20, 20, 5, 5}, CtColor::white());
    EXPECT_EQ(painter.commandCount(), 2u);

    // end() clears the buffer: a new session starts at zero commands.
    painter.end();
    EXPECT_TRUE(painter.begin(backend));
    EXPECT_EQ(painter.commandCount(), 0u);
    painter.end();
}

TEST_CASE(painter_draw_text_records_command)
{
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});
    CtPainter painter;
    EXPECT_TRUE(painter.begin(backend));

    const std::u16string text = u"Hello";
    const CtFont font{12, u"Segoe UI"};
    painter.drawText(CtPoint{10, 20}, text, font, CtColor::white());
    EXPECT_EQ(painter.commandCount(), 1u);

    // End flushes: the fixed-metric text model (charWidth=8, lineHeight=16)
    // drew a deterministic white block starting at the origin.
    painter.end();
    EXPECT_EQ(backend.pixel(10, 20).r, 255);          // glyph ink
    EXPECT_EQ(backend.pixel(49, 35).r, 255);          // 5 chars * 8px wide
    EXPECT_EQ(backend.pixel(50, 20).r, 0);            // past the text: clear
    EXPECT_EQ(backend.pixel(10, 36).r, 0);            // past line height
}

TEST_CASE(painter_fill_rect_hot_path_allocates_zero)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtPainter painter;

    // Warm-up: construct everything, run one full session so the command
    // buffer and trace ring are fully set up (allocations BEFORE arming).
    EXPECT_TRUE(painter.begin(backend));
    painter.fillRect(CtRect{0, 0, 10, 10}, CtColor::black());
    painter.end();

    // Armed hot path: fillRect x N + end (-> backend.flush) must perform
    // ZERO heap allocations (§6.2 acceptance).
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    EXPECT_TRUE(painter.begin(backend));
    for (int i = 0; i < 100; ++i)
        painter.fillRect(CtRect{i % 10, 0, 5, 5}, CtColor::black());
    EXPECT_EQ(painter.commandCount(), 100u);
    painter.end();
    g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = g_allocCount.load(std::memory_order_relaxed);

    EXPECT_EQ(allocs, 0u);
    EXPECT_FALSE(painter.overflowed());
}

TEST_CASE(painter_draw_text_hot_path_allocates_zero)
{
    CtPaintBackendPixelBuffer backend(CtSize{200, 100});
    CtPainter painter;
    const std::u16string text = u"Hot path text";
    const std::u16string family = u"Segoe UI";

    // Warm-up: one full session with drawText (construction allocations
    // and the u16string setup happen BEFORE arming, §6.2).
    const CtFont warmupFont{12, family};
    EXPECT_TRUE(painter.begin(backend));
    painter.drawText(CtPoint{0, 0}, text, warmupFont, CtColor::black());
    painter.end();

    // MUST-1 acceptance: drawText records the command with family + text
    // as u16string_view REFS — the record path allocates ZERO (§8.2).
    // NOTE: the CtFont is constructed BEFORE arming (CtFont's u16string
    // copy is allowed — non-record path, §3.2); drawText itself only
    // stores the pixelSize value + a family view.
    const CtFont font{12, family};
    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    EXPECT_TRUE(painter.begin(backend));
    for (int i = 0; i < 50; ++i)
        painter.drawText(CtPoint{0, 0}, text, font, CtColor::black());
    EXPECT_EQ(painter.commandCount(), 50u);
    painter.end();
    g_counting.store(false, std::memory_order_relaxed);
    const std::size_t allocs = g_allocCount.load(std::memory_order_relaxed);

    EXPECT_EQ(allocs, 0u);
    EXPECT_FALSE(painter.overflowed());
}

TEST_CASE(painter_begin_rejected_on_foreign_thread)
{
    // The painter carries NO affinity of its own (WARN-6/WARN-E): the
    // thread guard is the BACKEND's size() anchor. A backend constructed
    // on a DIFFERENT thread reports an empty size() to foreign callers in
    // release mode (assertSameThread returns false), so begin() refuses
    // the session — the §8.2 thread-guard acceptance. In debug builds the
    // assertSameThread contract aborts instead (cannot run in a unit
    // test; the release degradation path is what we assert here).
    std::thread worker([] {
        // The backend is created on the WORKER thread: its affinity is
        // bound to the worker (CtThreadAffinity, §5.2). The worker keeps
        // it alive until the join. NOTE: the object is created on the
        // worker but owned (and destroyed) on the main thread — a shared
        // ptr crossed the thread boundary; the backend must not be
        // touched concurrently (it isn't, after the join).
        g_foreignBackend = std::make_shared<CtPaintBackendPixelBuffer>(CtSize{100, 100});
    });
    worker.join();

    CtPainter painter;
    // The main thread is NOT the backend's thread: size() degrades to
    // empty in release -> begin refuses (WARN-E guard anchor).
    EXPECT_FALSE(painter.begin(*g_foreignBackend));
    EXPECT_FALSE(painter.isActive());

    // Subsequent drawing calls are no-ops (documented, §14): the session
    // never started, so nothing is recorded or flushed.
    painter.fillRect(CtRect{0, 0, 10, 10}, CtColor::black());
    painter.drawText(CtPoint{0, 0}, u"x", CtFont{}, CtColor::black());
    painter.save();
    painter.restore();
    EXPECT_EQ(painter.commandCount(), 0u);
    EXPECT_FALSE(painter.overflowed());

    g_foreignBackend.reset(); // RAII destruction (no bare delete)
}

TEST_CASE(painter_sink_records_paint_state_stack_depth)
{
    // WARN-A: with an injected sink, save/restore stack-depth changes are
    // recorded as INDEPENDENT kPaintState records (never kDamage, so the
    // kDamage -> kPainted pairing stays pure).
    CtPaintBackendPixelBuffer backend(CtSize{100, 100});
    CtWidgetTrace sink;
    CtPainter painter(&sink);
    EXPECT_TRUE(painter.begin(backend));

    painter.save();
    painter.save();
    painter.restore();

    CtWidgetRecord rec;
    // The LAST kPaintState record carries the depth AFTER the last change
    // (restore -> depth 1).
    bool found = false;
    std::vector<CtWidgetRecord> recs(sink.size());
    const std::size_t n = sink.copyOut(recs.data(), recs.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (recs[i].phase == CtWidgetPhase::kPaintState) {
            rec = recs[i];
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(rec.damageRectCount, 1u); // depth after the final restore

    // No kDamage records were written by the painter (pairing purity).
    EXPECT_FALSE(findRecord(sink, CtWidgetPhase::kDamage, rec));

    painter.end();
}

} // namespace (anonymous)
