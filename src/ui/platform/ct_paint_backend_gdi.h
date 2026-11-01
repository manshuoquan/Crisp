#pragma once

// ct_paint_backend_gdi.h - UI3 GDI paint backend (platform layer; Win32-only
// header). Optional deliverable (UI3 §1.1 WARN-5): targets a window's DC
// (FillRect/DrawTextW/BitBlt); consumed by CtBackingStore (UI2).
// Authoritative spec: docs/ui/06-ui3-architecture.md §1.1 (item 7), §9.1.

#include <windows.h>  // HDC/HWND (platform layer; Win32-only header, S-4)

#include "../ct_paint_backend.h"   // crisp::ui::CtPaintBackend

namespace crisp::ui {

/// GDI paint backend: renders the CtPainter command batch onto a window's
/// device context (double-buffered via a memory DC, BitBlt on present).
class CtPaintBackendGdi final : public CtPaintBackend {
public:
    /// `hwnd`: BORROWED top-level window handle (valid while the window
    /// lives; never freed by the backend).
    explicit CtPaintBackendGdi(HWND hwnd);

    ~CtPaintBackendGdi() override;

    CtPaintBackendGdi(const CtPaintBackendGdi&) = delete;
    CtPaintBackendGdi& operator=(const CtPaintBackendGdi&) = delete;

    CtSize size() const noexcept override;
    void submitCommands(const CtPaintCmd* cmds, std::size_t count) noexcept override;
    void flush(const CtRegion& damage) noexcept override;
    bool present(const CtRect& rect) noexcept override;
    bool fontMetrics(const CtFont& font, const std::u16string_view& text,
                     CtPaintMetrics& out) const noexcept override;

private:
    HWND m_hwnd = nullptr; ///< BORROWED (window owns it)
    HDC m_windowDc = nullptr;   ///< GetDC at construction, released at dtor
    HDC m_memDc = nullptr;      ///< memory DC (double buffer)
    HBITMAP m_memBitmap = nullptr; ///< back buffer bitmap
    HBITMAP m_oldBitmap = nullptr; ///< restore on dtor
    CtSize m_size{};            ///< cached client size
};

} // namespace crisp::ui
