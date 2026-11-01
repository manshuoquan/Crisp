// ct_paint_backend_gdi.cpp - UI3 GDI paint backend (platform layer).
// OPTIONAL deliverable (UI3 §1.1 item 7 / WARN-5): renders the CtPainter
// command batch (CtPaintCmd) onto a window's DC via a memory DC, then
// BitBlt's the dirty rect on present(). CtBackingStore consumes it.
//
// The command set is UI2's: kFillRect (solid color) is executed via
// FillRect with a GDI brush; text (kDrawText) via DrawTextW with the
// resolved font; kBlit stays unsupported in UI3 (no image source yet).
// Thread-affined to the UI thread (CtThreadAffinity guard on entry).
//
// Authoritative spec: docs/ui/06-ui3-architecture.md §1.1 item 7, §9.1.

#include "ct_paint_backend_gdi.h"

#include "../ct_paint_cmd.h"   // CtPaintCmd / CtPaintCmdType
#include "../../event/ct_thread_affinity.h"  // CtThreadAffinity

namespace crisp::ui {

CtPaintBackendGdi::CtPaintBackendGdi(HWND hwnd) : m_hwnd(hwnd) {
    if (!m_hwnd)
        return;
    // KNOWN LIMITATION (registered): the memory DC + m_size are created at
    // construction from the then-current client rect; window RESIZE is NOT
    // handled (no WM_SIZE path) - present() validates against the stale
    // m_size. UI3 positions this backend as the platform-layer smoke
    // deliverable (05 §4.2.2); resize support belongs to UI4.
    m_windowDc = GetDC(m_hwnd);
    RECT client{};
    GetClientRect(m_hwnd, &client);
    m_size = CtSize{client.right - client.left, client.bottom - client.top};
    if (m_windowDc && !m_size.isEmpty()) {
        m_memDc = CreateCompatibleDC(m_windowDc);
        m_memBitmap = CreateCompatibleBitmap(m_windowDc, m_size.width,
                                             m_size.height);
        if (m_memDc && m_memBitmap) {
            m_oldBitmap = static_cast<HBITMAP>(
                SelectObject(m_memDc, m_memBitmap));
        }
    }
}

CtPaintBackendGdi::~CtPaintBackendGdi() {
    if (m_memDc && m_oldBitmap)
        SelectObject(m_memDc, m_oldBitmap);
    if (m_memBitmap)
        DeleteObject(m_memBitmap);
    if (m_memDc)
        DeleteDC(m_memDc);
    if (m_windowDc)
        ReleaseDC(m_hwnd, m_windowDc);
}

CtSize CtPaintBackendGdi::size() const noexcept {
    // Thread guard (UI3 §5.4): debug abort / release empty size so the
    // painter's begin() refuses the session (same as PixelBuffer backend).
    if (!m_affinity.assertSameThread())
        return CtSize{};
    return m_size;
}

void CtPaintBackendGdi::submitCommands(const CtPaintCmd* cmds,
                                       std::size_t count) noexcept {
    // Thread rule (UI3 §5.4): the backend is the surface and must be
    // driven on the UI thread only; debug abort / release no-op.
    if (!m_affinity.assertSameThread())
        return;
    // PA4: interface-level batch delivery. The command array is owned by
    // the painter and stays valid until the synchronous flush() that
    // follows end(). Executed in flush() - not copied (zero allocation).
    m_pendingCmds = cmds;
    m_pendingCount = count;
}

void CtPaintBackendGdi::flush(const CtRegion& damage) noexcept {
    // Thread rule (UI3 §5.4): see submitCommands.
    if (!m_affinity.assertSameThread())
        return;
    if (!m_memDc)
        return;

    // Execute the pending command batch into the memory DC (the "real"
    // drawing, 05 §4.2.2): kFillRect -> FillRect with a solid brush;
    // kDrawText -> DrawTextW with the resolved font. `damage` is a hint
    // in UI2 - the recorded commands are executed whole; the dirty-rect
    // optimization is a UI2 evolution point (O1).
    if (m_pendingCmds && m_pendingCount > 0) {
        for (std::size_t i = 0; i < m_pendingCount; ++i) {
            const CtPaintCmd& cmd = m_pendingCmds[i];
            if (cmd.type == CtPaintCmdType::kFillRect) {
                HBRUSH brush = CreateSolidBrush(
                    RGB(cmd.color.r, cmd.color.g, cmd.color.b));
                if (brush) {
                    RECT rc{cmd.rect.x, cmd.rect.y,
                            cmd.rect.right(), cmd.rect.bottom()};
                    FillRect(m_memDc, &rc, brush);
                    DeleteObject(brush);
                }
            } else {
                // kDrawText: DrawTextW with the resolved font (family
                // UTF-16 -> wchar_t on Windows; empty = default face).
                HFONT hf = CreateFontW(-static_cast<int>(cmd.fontPixelSize),
                                       0, 0, 0, FW_NORMAL, 0, 0, 0,
                                       DEFAULT_CHARSET, 0, 0,
                                       CLEARTYPE_QUALITY, 0,
                                       cmd.family.empty()
                                           ? L"Segoe UI"
                                           : reinterpret_cast<const wchar_t*>(
                                                 cmd.family.data()));
                if (hf) {
                    HGDIOBJ old = SelectObject(m_memDc, hf);
                    SetTextColor(m_memDc,
                                 RGB(cmd.color.r, cmd.color.g, cmd.color.b));
                    SetBkMode(m_memDc, TRANSPARENT);
                    RECT rc{cmd.origin.x, cmd.origin.y,
                            cmd.origin.x + 1024, cmd.origin.y + 1024};
                    const int len = static_cast<int>(cmd.text.size());
                    // D7: single-line, as-is rendering (no wrapping, no
                    // '&' mnemonics, no alignment).
                    DrawTextW(m_memDc,
                              reinterpret_cast<LPCWSTR>(cmd.text.data()),
                              len, &rc,
                              DT_LEFT | DT_TOP | DT_NOCLIP |
                                  DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(m_memDc, old);
                    DeleteObject(hf);
                }
            }
        }
    }
    // The batch is single-use: clear the channel so a later flush without
    // a painter session executes nothing.
    m_pendingCmds = nullptr;
    m_pendingCount = 0;
    (void)damage;
}

bool CtPaintBackendGdi::present(const CtRect& rect) noexcept {
    // Thread rule (UI3 §5.4): see submitCommands.
    if (!m_affinity.assertSameThread())
        return false;
    if (!m_memDc || !m_windowDc)
        return false;
    // Out-of-surface rect: report FAILURE (the store must NOT write
    // kPainted / clearDamage when present fails - J2 pairing precondition).
    // UNIFORM policy: any edge outside the surface is a failure (no
    // clamping - a clamped blit would claim success for a region the
    // surface never painted).
    if (rect.x < 0 || rect.y < 0 ||
        rect.right() > m_size.width || rect.bottom() > m_size.height ||
        rect.width <= 0 || rect.height <= 0)
        return false;
    return BitBlt(m_windowDc, rect.x, rect.y, rect.width, rect.height,
                  m_memDc, rect.x, rect.y, SRCCOPY) != 0;
}

bool CtPaintBackendGdi::fontMetrics(const CtFont& font,
                                    const std::u16string_view& text,
                                    CtPaintMetrics& out) const noexcept {
    // Thread rule (UI3 §5.4): const entry but the GDI DC is stateful and
    // single-threaded; debug abort / release false.
    if (!m_affinity.assertSameThread())
        return false;
    // UI2 fixed-metric contract; GDI resolution via GetTextExtentPoint32W.
    if (!m_windowDc)
        return false;
    // font.family is std::u16string (UTF-16 code units). On Windows,
    // wchar_t IS UTF-16 (2 bytes), so the reinterpret cast is the
    // sanctioned platform mapping (B2 documented: Windows-only backend).
    HFONT hf = CreateFontW(-font.pixelSize, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                           font.family.empty()
                               ? L"Segoe UI"
                               : reinterpret_cast<const wchar_t*>(
                                     font.family.data()));
    if (!hf)
        return false;
    HGDIOBJ old = SelectObject(m_windowDc, hf);
    SIZE s{};
    const int len = static_cast<int>(text.size());
    const BOOL ok = GetTextExtentPoint32W(
        m_windowDc, reinterpret_cast<LPCWSTR>(text.data()), len, &s);
    SelectObject(m_windowDc, old);
    DeleteObject(hf);
    if (!ok)
        return false;  // metrics failed - report the capability as absent
    out.width = s.cx;
    out.height = s.cy;
    return true;
}

} // namespace crisp::ui
