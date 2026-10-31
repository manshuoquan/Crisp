// ct_paint_backend_pixel.cpp — UI2 core-layer pixel buffer backend
// implementation (crisp::ui). Executes the recorded CtPaintCmd stream
// (delivered by CtPainter::end via the friend channel) into an off-screen
// RGBA8888 array (the buffer IS the store); present is a no-op + region
// sanity check (MUST-C).
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.1 / §6.1.

#include "ct_paint_backend_pixel.h"

#include <algorithm>
#include <cassert>

#include "ct_paint_cmd.h"  // CtPaintCmd / CtPaintCmdType

namespace crisp::ui {

namespace {

/// Fixed text metrics for the PixelBuffer backend (SUGGEST-1 ruling,
/// docs/ui/05-ui2-architecture.md §3.4): char width 8px, line height 16px,
/// advance width = text.size() x 8. Pinned for offline test assertions;
/// the GDI backend reports real metrics (GetTextExtentPoint32W).
constexpr int kCharWidth = 8;
constexpr int kLineHeight = 16;

} // namespace

CtPaintBackendPixelBuffer::CtPaintBackendPixelBuffer(const CtSize& size)
    : m_size(size),
      m_pixels(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4u,
               static_cast<std::uint8_t>(0)) {
    // NOTE: the vector allocates ONCE at construction and is reused for
    // the backend's whole lifetime (D4 single-buffer reuse, §5.4). The
    // buffer is zero-initialized (transparent black).
    // (CtThreadAffinity is constructed implicitly: records the creating
    // thread — the UI thread guard anchor, WARN-E.)
}

CtSize CtPaintBackendPixelBuffer::size() const noexcept {
    // Thread guard anchor (WARN-E): assert + degrade. In release builds
    // a foreign-thread call returns an EMPTY size so the painter's
    // begin() refuses the session (the §8.2 begin-rejection test).
    if (!m_affinity.assertSameThread())
        return CtSize{};
    return m_size;
}

void CtPaintBackendPixelBuffer::submitCommands(const CtPaintCmd* cmds,
                                               std::size_t count) noexcept {
    // PA4: interface-level batch delivery (replaces the former friend
    // channel). The command array is owned by the painter and stays
    // valid until the synchronous flush() that follows end().
    m_pendingCmds = cmds;
    m_pendingCount = count;
}

void CtPaintBackendPixelBuffer::flush(const CtRegion& damage) noexcept {
    // Thread rule: the backend is the surface and must be drawn on the
    // UI thread only (§5.2); debug abort / release no-op.
    if (!m_affinity.assertSameThread())
        return;
    // Execute the pending command batch delivered by CtPainter::end
    // (the "real" drawing, §4.2.1). `damage` is a hint in UI2 — the
    // recorded commands are executed into the whole buffer; the
    // dirty-rect optimization is a UI2 evolution point (O1).
    if (m_pendingCmds && m_pendingCount > 0) {
        for (std::size_t i = 0; i < m_pendingCount; ++i) {
            const CtPaintCmd& cmd = m_pendingCmds[i];
            if (cmd.type == CtPaintCmdType::kFillRect) {
                // Solid fill over the rect (RGBA8888, no blending — UI2
                // has no alpha blending, §3.1).
                for (int y = cmd.rect.y; y < cmd.rect.bottom(); ++y)
                    fillSpan(y, cmd.rect.x, cmd.rect.right(), cmd.color);
            } else {
                // kDrawText: fixed-metric glyph cells (charWidth x
                // lineHeight) starting at the baseline origin (D7).
                const int w = static_cast<int>(cmd.text.size()) * kCharWidth;
                const int h = kLineHeight;
                for (int dy = 0; dy < h; ++dy)
                    fillSpan(cmd.origin.y + dy, cmd.origin.x,
                             cmd.origin.x + w, cmd.color);
            }
        }
    }
    // The batch is single-use: clear the channel so a later flush without
    // a painter session executes nothing.
    m_pendingCmds = nullptr;
    m_pendingCount = 0;
    (void)damage;
}

bool CtPaintBackendPixelBuffer::present(const CtRect& rect) noexcept {
    // Thread rule: see flush (§5.2).
    if (!m_affinity.assertSameThread())
        return false;
    // MUST-C / WARN-4 ruling: present is a NO-OP for the PixelBuffer
    // backend (the buffer IS the store — there is no window to blit).
    // Region sanity: assert the presented rect is within size() so a
    // store/backend size mismatch surfaces in debug builds.
    assert(rect.x >= 0 && rect.y >= 0 &&
           rect.right() <= m_size.width && rect.bottom() <= m_size.height &&
           "CtPaintBackendPixelBuffer::present: rect outside surface");
    (void)rect;
    return true; // no-op success (J2: pairing proceeds)
}

bool CtPaintBackendPixelBuffer::fontMetrics(
    const CtFont& /*font*/, const std::u16string_view& text,
    CtPaintMetrics& out) const noexcept {
    // Fixed metrics (SUGGEST-1): charWidth = 8, lineHeight = 16,
    // width = text.size() x 8. The font identity is not consulted in the
    // fixed-metric model (GDI backend resolves it for real).
    out.width = static_cast<int>(text.size()) * kCharWidth;
    out.height = kLineHeight;
    return true;
}

CtColor CtPaintBackendPixelBuffer::pixel(int x, int y) const noexcept {
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_size.width) +
         static_cast<std::size_t>(x)) *
        4u;
    return CtColor{m_pixels[idx], m_pixels[idx + 1u], m_pixels[idx + 2u],
                   m_pixels[idx + 3u]};
}

const std::uint8_t* CtPaintBackendPixelBuffer::data() const noexcept {
    return m_pixels.data();
}

void CtPaintBackendPixelBuffer::fillSpan(int y, int x0, int x1,
                                         const CtColor& color) noexcept {
    if (x1 <= x0)
        return;
    // Clamp to the surface (defensive: recorded coords are already
    // translated/clipped, D3 — this guard keeps degenerate input safe).
    const int cy0 = std::max(0, x0);
    const int cy1 = std::min(m_size.width, x1);
    if (y < 0 || y >= m_size.height || cy1 <= cy0)
        return;
    std::uint8_t* row =
        m_pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(m_size.width) * 4u;
    for (int x = cy0; x < cy1; ++x) {
        std::uint8_t* px = row + static_cast<std::size_t>(x) * 4u;
        px[0] = color.r;
        px[1] = color.g;
        px[2] = color.b;
        px[3] = color.a;
    }
}

} // namespace crisp::ui
