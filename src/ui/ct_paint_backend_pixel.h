#pragma once

// ct_paint_backend_pixel.h — UI2 core-layer pixel buffer backend
// (crisp::ui). Off-screen RGBA8888 buffer (the buffer IS the store —
// present is a no-op + region check, MUST-C). All drawing-correctness
// assertions happen HERE (offline render).
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.1.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ct_paint_backend.h"   // CtPaintBackend (base)
#include "event/ct_thread_affinity.h" // crisp::event::CtThreadAffinity

namespace crisp::ui {

struct CtPaintCmd; ///< fwd: command-batch delivery (submitCommands)

/// Core-layer pixel buffer backend: off-screen RGBA8888 buffer (the
/// buffer IS the store — present is a no-op + region check, §4.2.1 /
/// MUST-C). All drawing-correctness assertions happen HERE (offline
/// render).
/// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.1.
class CtPaintBackendPixelBuffer final : public CtPaintBackend {
public:
    explicit CtPaintBackendPixelBuffer(const CtSize& size);
    ~CtPaintBackendPixelBuffer() override = default;

    CtSize size() const noexcept override;

    /// Receive the recorded command batch of one paint pass (PA4: the
    /// interface-level delivery channel — no friend plumbing). Executes
    /// nothing yet; flush() rasterizes the stored batch.
    void submitCommands(const CtPaintCmd* cmds, std::size_t count) noexcept override;

    /// Execute the recorded commands into the pixel buffer (the "real"
    /// drawing; damage is a hint — UI2 fills the whole surface once).
    void flush(const CtRegion& damage) noexcept override;

    /// Present: NO-OP for the PixelBuffer backend (the buffer IS the
    /// store — WARN-4/MUST-C ruling). Asserts the rect is within size()
    /// (region sanity; the caller's present-consistency is verified via
    /// the store's flush bookkeeping, not a copy). Returns true (no-op
    /// success) so the store completes the kPainted pairing (J2).
    bool present(const CtRect& rect) noexcept override;

    bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                     CtPaintMetrics& out) const noexcept override;

    /// Read pixel at (x,y) — test/assert hook (no bounds check; caller
    /// keeps within size()).
    CtColor pixel(int x, int y) const noexcept;

    /// The raw RGBA8888 backing array (test hook).
    const std::uint8_t* data() const noexcept;

private:
    /// Fill one pixel row span with `color` (write order R,G,B,A).
    void fillSpan(int y, int x0, int x1, const CtColor& color) noexcept;

    CtSize m_size;                          ///< surface size
    std::vector<std::uint8_t> m_pixels;     ///< RGBA8888, size*4 bytes
    crisp::event::CtThreadAffinity m_affinity; ///< thread guard anchor (WARN-E)
    const CtPaintCmd* m_pendingCmds = nullptr; ///< batch delivered via submitCommands
    std::size_t m_pendingCount = 0;            ///< valid commands in the batch
};

} // namespace crisp::ui
