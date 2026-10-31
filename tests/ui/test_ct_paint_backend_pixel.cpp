// Unit tests: crisp::ui PixelBuffer paint backend (CtPaintBackendPixelBuffer,
// UI2 §4.2.1).
//
// Covers: offline per-pixel rendering correctness (fillRect writes pixels);
// present = NO-OP + region sanity (MUST-C / WARN-4); fixed font metrics
// (SUGGEST-1: charWidth=8, lineHeight=16, width=text.size() x 8).
//
// Hand-written test doubles, no mock framework (project rule).

#include "ui/ct_paint_backend_pixel.h"
#include "ui/ct_painter.h"

#include <memory>

#include "test_harness.h"

using crisp::ui::CtColor;
using crisp::ui::CtFont;
using crisp::ui::CtPaintBackendPixelBuffer;
using crisp::ui::CtPainter;
using crisp::ui::CtPaintMetrics;
using crisp::ui::CtRect;
using crisp::ui::CtSize;

namespace {

TEST_CASE(pixel_backend_fill_rect_writes_pixels)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 50});
    EXPECT_EQ(backend.size().width, 100);
    EXPECT_EQ(backend.size().height, 50);

    // Initial state: zero-filled buffer (transparent black).
    EXPECT_EQ(backend.pixel(0, 0).r, 0);
    EXPECT_EQ(backend.pixel(0, 0).a, 0);

    // Drive one fill through the painter (the record -> flush pipeline).
    // NOTE: CtPainter carries a ~0.5MB command buffer (WARN-2) — heap-
    // allocate it so the test frame stays shallow (MinGW 8.1 1MB stack).
    auto painter = std::make_unique<CtPainter>();
    EXPECT_TRUE(painter->begin(backend));
    painter->fillRect(CtRect{10, 5, 20, 15}, CtColor{200, 100, 50, 255});
    painter->end(); // -> backend.flush(damage) executes the fill

    // Per-pixel assertion inside the rect: exact RGBA written.
    const CtColor inside = backend.pixel(10, 5);
    EXPECT_EQ(inside.r, 200);
    EXPECT_EQ(inside.g, 100);
    EXPECT_EQ(inside.b, 50);
    EXPECT_EQ(inside.a, 255);
    const CtColor lastIn = backend.pixel(29, 19); // right/bottom edges exclusive
    EXPECT_EQ(lastIn.r, 200);
    EXPECT_EQ(lastIn.g, 100);

    // Just outside the half-open rect: untouched (still transparent).
    EXPECT_EQ(backend.pixel(9, 5).r, 0);
    EXPECT_EQ(backend.pixel(30, 5).r, 0);
    EXPECT_EQ(backend.pixel(10, 4).r, 0);
    EXPECT_EQ(backend.pixel(10, 20).r, 0);

    // A second pass fills over the first: last command wins (no blending).
    auto painter2 = std::make_unique<CtPainter>();
    EXPECT_TRUE(painter2->begin(backend));
    painter2->fillRect(CtRect{10, 5, 20, 15}, CtColor::black());
    painter2->end();
    EXPECT_EQ(backend.pixel(15, 10).r, 0);
    EXPECT_EQ(backend.pixel(15, 10).g, 0);
    EXPECT_EQ(backend.pixel(15, 10).a, 255);

    // data() exposes the raw RGBA8888 array (test hook).
    const std::uint8_t* raw = backend.data();
    EXPECT_TRUE(raw != nullptr);
    // (100 x 50 x 4 bytes per pixel.)
    EXPECT_EQ(backend.size().width * backend.size().height * 4, 20000);
}

TEST_CASE(pixel_backend_present_is_noop_asserts_rect)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 50});

    // present is a NO-OP for the PixelBuffer backend (MUST-C / WARN-4):
    // the buffer IS the store — there is no window to blit. Calling it
    // with a rect within size() is accepted (region sanity assert passes
    // in debug; release no-op).
    backend.present(CtRect{0, 0, 10, 10});

    // A rect exactly at the surface edge is valid (half-open bounds).
    backend.present(CtRect{90, 40, 10, 10});

    // The buffer is unchanged by present (no copy, no write).
    EXPECT_EQ(backend.pixel(5, 5).r, 0);
    EXPECT_EQ(backend.pixel(5, 5).a, 0);

    // NOTE: present with a rect OUTSIDE size() asserts in debug builds
    // (region sanity); in release it is a no-op. The assert path cannot
    // be exercised in a unit test — the store's present-consistency is
    // verified via the flush bookkeeping (backing_store tests).
}

TEST_CASE(pixel_backend_font_metrics_fixed_values)
{
    CtPaintBackendPixelBuffer backend(CtSize{100, 50});

    // Fixed metrics (SUGGEST-1): charWidth=8, lineHeight=16,
    // width = text.size() x 8. The font identity is NOT consulted.
    CtPaintMetrics m;
    EXPECT_TRUE(backend.fontMetrics(CtFont{}, u"", m));
    EXPECT_EQ(m.width, 0);
    EXPECT_EQ(m.height, 16);

    EXPECT_TRUE(backend.fontMetrics(CtFont{}, u"Hello", m));
    EXPECT_EQ(m.width, 40); // 5 chars x 8
    EXPECT_EQ(m.height, 16);

    EXPECT_TRUE(backend.fontMetrics(CtFont{20, u"Any Font"}, u"abcdefgh", m));
    EXPECT_EQ(m.width, 64); // 8 chars x 8
    EXPECT_EQ(m.height, 16);
}

} // namespace (anonymous)
