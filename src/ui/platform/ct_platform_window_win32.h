#pragma once

// ct_platform_window_win32.h - UI3 Win32 native CtPlatformWindow (platform
// layer; Win32-only header, S-4). OWNs the HWND (RAII); the message pump is
// a CtWin32EventSource<CtUiEventSet> (D2).
// Destruction order HARD CONTRACT (MUST-6/D11): dtor FIRST deactivates +
// detaches the event source, THEN destroys the HWND.
// Authoritative spec: docs/ui/06-ui3-architecture.md §4.1/§5.2.

#include <windows.h>  // HWND (platform layer; Win32-only header, S-4)

#include <memory>
#include <string>

#include "../ct_platform_window.h"    // crisp::ui::CtPlatformWindow
#include "ct_win32_event_source.h"    // CtWin32EventSource<CtUiEventSet>
namespace crisp::ui {

/// Win32 native platform window (mode 1 of the dual-mode CtPlatformWindow).
class CtPlatformWindowWin32 final : public CtPlatformWindow {
public:
    /// Create a top-level window. `title` is UTF-8; `width`/`height` are the
    /// requested client size in pixels (the OS may adjust; use surfaceSize()).
    CtPlatformWindowWin32(const char* title, int width, int height);

    ~CtPlatformWindowWin32() override;

    CtPlatformWindowWin32(const CtPlatformWindowWin32&) = delete;
    CtPlatformWindowWin32& operator=(const CtPlatformWindowWin32&) = delete;

    /// The client rect in device pixels (for the backing store).
    CtSize surfaceSize() const noexcept override;

    /// The native window handle (platform layer; UI3 §5.4 GDI backend
    /// targets the window DC via the borrowed HWND).
    HWND nativeHandle() const noexcept { return m_hwnd; }

    void show() override;
    void hide() noexcept override;

    /// The Win32 message pump as an event source (CtUiEventSet).
    std::shared_ptr<crisp::event::CtEventSource> eventSource() override;

private:
    /// Window procedure (minimal: WM_NCCREATE stores `this`; everything
    /// else defers to DefWindowProcW - the message pump's poll() handles
    /// input messages via PeekMessageW, not via the wndproc).
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam);

    HWND m_hwnd = nullptr;                          ///< OWNED (RAII)
    std::shared_ptr<CtWin32EventSource<CtUiEventSet>> m_eventSource; ///< pump
};

} // namespace crisp::ui
