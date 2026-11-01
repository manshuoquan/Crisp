#pragma once

// ct_win32_event_source.h - UI3 Win32 message pump AS a CtEventSource
// (platform layer; README exception clause allows Win32).
// EventSet TEMPLATE (MUST-2 ruling): non-template sources have no
// injection channel into CtEventLoop (Rev 10 P2-A / 04 §7.12 contract
// #6) - precedent: ct_timer_event_source.h / qt_event_source.h.
// Authoritative spec: docs/ui/06-ui3-architecture.md §4.2.

#include <windows.h>  // HWND (platform layer; Win32-only header, S-4)

#include "../../event/ct_event_source.h"      // crisp::event::CtEventSource (base)
#include "../../event/ct_event_set.h"         // crisp::event::CtEventSet
#include "../../event/ct_queue.h"             // crisp::event::CtQueueBase
#include "../ct_input_mapper.h"               // crisp::ui::platform::mapper
namespace crisp::ui {

/// Win32 message pump implemented AS a CtEventSource (M2 §7.12): poll()
/// drains the window's message queue (PeekMessageW) and pushes translated
/// events via STRONG-TYPED push (eventSetId() assertion + CtQueue::push,
/// zero allocation). Allocates nothing per message in the translation.
template <typename EventSet>
class CtWin32EventSource final : public crisp::event::CtEventSource {
public:
    /// `hwnd`: the BORROWED top-level window handle (W-5: valid while the
    /// window lives; the source never frees it - the window dtor
    /// deactivates + detaches first, MUST-6/D11).
    /// Platform layer: takes the native HWND type directly (S-4 - the
    /// header stays Win32-only, no portability constraint here).
    explicit CtWin32EventSource(HWND hwnd);

    /// poll: PeekMessageW once per frame; translate each input message
    /// via the mapper; strong-typed push to the loop queue (assert
    /// eventSetId). WM_TIMER -> CtTimerPayload (D5). Non-input messages
    /// are skipped (ValidateRect for WM_PAINT, WARN-4).
    void poll(crisp::event::CtQueueBase& queue) override;

    bool activate() override;       ///< mark the source enabled (S-1: no
                                    ///< hook registration needed - wakeup
                                    ///< comes from exec's timed wait, D10)
    void deactivate() noexcept override;
    std::string_view sourceName() const noexcept override;

    /// UI3 D10/MUST-1: the Win32 message queue is NOT the CtQueue - a new
    /// message does not notify the queue's condition variable, so the loop
    /// must use a TIMED wait to return to poll() and drain PeekMessageW.
    bool needsPollingWakeup() const noexcept override { return true; }

private:
    HWND m_hwnd = nullptr; ///< BORROWED top-level HWND (not owned, MUST-6:
                           ///< valid while the window lives; the event
                           ///< source never frees it - the window dtor
                           ///< deactivates + detaches first, D11)
    bool m_active = false;
    // D7 dbl-click state (owned here, passed to the mapper by ref)
    platform::CtDblClickState m_dblClick;
};

} // namespace crisp::ui
