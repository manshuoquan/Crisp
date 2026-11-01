#pragma once

// ct_platform_window.h - UI3 platform window abstraction (crisp::ui).
// TWO modes, ONE interface (03 §4.3.1):
//  - Win32 native: owns an HWND; the message pump IS a CtEventSource;
//  - Qt host: owns a QWindow/QWidget; uses the UI3 host wrapper
//    CtQtHostEventSource<CtUiEventSet> (W-1/P7: NOT the M4 source
//    directly - bridge EventSet differs from the UI EventSet).
// Authoritative spec: docs/ui/06-ui3-architecture.md §4.1.

#include <memory>

#include "ct_paint_types.h"        // CtSize (defined here, not ct_geometry.h)
#include "event/ct_event_source.h" // crisp::event::CtEventSource

namespace crisp::ui {

/// Platform window abstraction (QPA QWindow analog, 01 §1).
class CtPlatformWindow {
public:
    virtual ~CtPlatformWindow() = default;

    /// The window's surface size in device pixels (for the backing store).
    virtual CtSize surfaceSize() const noexcept = 0;

    /// Show/hide the native window.
    virtual void show() = 0;
    virtual void hide() noexcept = 0;

    /// The event source this window contributes to the UI loop
    /// (attached via CtEventLoop::attachSource).
    virtual std::shared_ptr<crisp::event::CtEventSource> eventSource() = 0;
};

} // namespace crisp::ui
