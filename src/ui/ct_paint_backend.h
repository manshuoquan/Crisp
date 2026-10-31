#pragma once

// ct_paint_backend.h — UI2 paint backend interface (crisp::ui).
// Core-layer PixelBuffer backend is pure C++17 (testable off-screen);
// GDI backend (platform layer) targets a window's backing store.
// Authoritative spec: docs/ui/05-ui2-architecture.md §4.2.
// PA4 (audit MUST): the interface carries the recorded command batch via
// submitCommands() — no dynamic_cast, no friend-channel leakage; any
// future backend (UI3/GDI) receives the batch uniformly.

#include <cstdint>
#include <string_view>

#include "ct_paint_cmd.h"      // CtPaintCmd / CtPaintCmdType
#include "ct_paint_types.h"   // CtSize / CtColor / CtFont / CtPaintMetrics
#include "ct_geometry.h"      // CtRect / CtRegion

namespace crisp::ui {

/// Paint backend: the surface a CtPainter draws onto.
class CtPaintBackend {
public:
    virtual ~CtPaintBackend() = default;

    /// Surface size in pixels; zero size refuses begin(). Thread guard
    /// anchor (WARN-E): asserts the calling thread owns the backend
    /// (debug abort / release returns empty size — the painter's begin()
    /// then fails and the session degrades to no-op).
    virtual CtSize size() const noexcept = 0;

    /// Deliver the recorded command batch of one paint pass to the
    /// backend (PA4; called by CtPainter::end BEFORE flush). The command
    /// array stays valid only for the duration of this call — the
    /// backend must execute (or copy) synchronously. Must be thread-
    /// affined to the UI thread.
    virtual void submitCommands(const CtPaintCmd* cmds, std::size_t count) noexcept = 0;

    /// Commit the batched primitives of one paint pass (called by
    /// CtPainter::end AFTER submitCommands). `damage` = the dirty region
    /// to repaint (single rect, D2). noexcept (BS3: no exception may
    /// cross the painter's noexcept end() boundary). Must be thread-
    /// affined to the UI thread.
    virtual void flush(const CtRegion& damage) noexcept = 0;

    /// Blit the backing store region to the window (double buffer).
    /// `rect` = the flushed dirty rect (present only that). noexcept
    /// (BS3). MUST-C ruling: PixelBuffer backend present = NO-OP + region
    /// sanity check (the buffer IS the store — no window); GDI backend
    /// BitBlt's the dirty rect from its internal memory DC to the window.
    /// Returns TRUE on success (or no-op success for PixelBuffer).
    /// On FALSE the store must NOT write kPainted / clearDamage (J2:
    /// "present succeeded" is the pairing precondition, §7.1).
    virtual bool present(const CtRect& rect) noexcept = 0;

    /// OPTIONAL text metrics capability (D7). Returns false if the backend
    /// does not support text (no drawText). PixelBuffer: fixed values;
    /// GDI: GetTextExtentPoint32W.
    virtual bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                             CtPaintMetrics& out) const noexcept = 0;
};

} // namespace crisp::ui
