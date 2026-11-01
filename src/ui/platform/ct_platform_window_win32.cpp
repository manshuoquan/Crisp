// ct_platform_window_win32.cpp - UI3 Win32 native CtPlatformWindow
// implementation (platform layer; README exception clause allows Win32).
//
// Owns an HWND (RAII: created in the ctor, destroyed in the dtor). The
// message pump IS a CtWin32EventSource<CtUiEventSet> - the window's
// eventSource() returns it for CtEventLoop::attachSource.
// Destruction order HARD CONTRACT (MUST-6/D11): the dtor FIRST
// deactivates + detaches the event source, THEN destroys the HWND; the
// source never frees the borrowed HWND itself.
//
// Authoritative spec: docs/ui/06-ui3-architecture.md §4.1/§5.2.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "ct_platform_window_win32.h"

#include <utility>

#include "../../event/ct_event_loop.h"  // CtEventLoop::current()/detachSource

namespace crisp::ui {

CtPlatformWindowWin32::CtPlatformWindowWin32(const char* title, int width,
                                             int height) {
    // Register a minimal window class (once).
    static const wchar_t kClassName[] = L"CRISP_UI3_WINDOW";
    static bool s_classRegistered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &CtPlatformWindowWin32::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        return RegisterClassExW(&wc) != 0;
    }();
    (void)s_classRegistered;

    // UTF-8 title -> UTF-16 for CreateWindowExW.
    std::wstring wideTitle;
    if (title) {
        const int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        if (len > 0) {
            wideTitle.resize(static_cast<std::size_t>(len) - 1);
            MultiByteToWideChar(CP_UTF8, 0, title, -1, &wideTitle[0], len);
        }
    }

    m_hwnd = CreateWindowExW(0, kClassName, wideTitle.c_str(),
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             width, height, nullptr, nullptr,
                             GetModuleHandleW(nullptr),
                             reinterpret_cast<LPVOID>(this));
    // CW_USEDEFAULT may size the window differently; the surface size is
    // queried via GetClientRect at show() time - the backing store reads
    // surfaceSize().

    if (m_hwnd) {
        // The source takes the BORROWED HWND (MUST-6: the window owns it).
        m_eventSource = std::make_shared<CtWin32EventSource<CtUiEventSet>>(m_hwnd);
    }
}

CtPlatformWindowWin32::~CtPlatformWindowWin32() {
    // D11/MUST-6: deactivate + detach BEFORE releasing the native resource.
    if (m_eventSource) {
        m_eventSource->deactivate();
        if (auto* loop = crisp::event::CtEventLoop::current())
            loop->detachSource(m_eventSource);
    }
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

CtSize CtPlatformWindowWin32::surfaceSize() const noexcept {
    if (!m_hwnd)
        return CtSize{};
    RECT client{};
    GetClientRect(m_hwnd, &client);
    return CtSize{client.right - client.left, client.bottom - client.top};
}

void CtPlatformWindowWin32::show() {
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_SHOW);
}

void CtPlatformWindowWin32::hide() noexcept {
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

std::shared_ptr<crisp::event::CtEventSource> CtPlatformWindowWin32::eventSource() {
    return m_eventSource;
}

LRESULT CALLBACK CtPlatformWindowWin32::wndProc(HWND hwnd, UINT msg,
                                                WPARAM wParam,
                                                LPARAM lParam) {
    // Retrieve the owning window from the creation parameter (UI3 single
    // window): messages the event source's poll() already handles (input,
    // timer) are ignored here - they never reach DefWindowProc (return 0).
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 1;
    }
    // Default handling for everything else (sizing, close, paint).
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace crisp::ui
