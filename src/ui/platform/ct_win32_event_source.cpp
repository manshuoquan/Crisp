// ct_win32_event_source.cpp - UI3 Win32 message pump AS a CtEventSource.
// poll() drains the window's message queue (PeekMessageW), translates each
// input message via the platform mapper, and STRONG-TYPED pushes into the
// loop queue (MUST-2: eventSetId assertion + CtQueue::push, zero heap
// allocation). WM_TIMER -> CtTimerPayload (D5). Non-input messages are
// skipped; WM_PAINT is validated so the update region stops re-posting
// (WARN-4). kUnknown VK codes are recorded in the queue's trace (MUST-5).
//
// Authoritative spec: docs/ui/06-ui3-architecture.md §4.2/§7.1.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601  // GetTickCount64 requires >= 0x0600
#endif

#include "ct_win32_event_source.h"

#include <cassert>

#include "../../event/ct_queue.h"   // CtQueue<EventSet> (strong-typed push)
#include "../../event/ct_receiver.h" // std::weak_ptr<CtReceiver> (broadcast)
#include "../../event/ct_type_key.h" // CtTypeKey (eventSetId assertion)

using crisp::event::CtEvent;
using crisp::event::CtEventOrigin;
using crisp::event::CtQueue;
using crisp::event::CtQueueBase;
using crisp::event::CtReceiver;
using crisp::event::CtTimerPayload;
using crisp::event::CtTypeKey;

namespace crisp::ui {

template <typename EventSet>
CtWin32EventSource<EventSet>::CtWin32EventSource(HWND hwnd)
    : m_hwnd(hwnd) {}

template <typename EventSet>
void CtWin32EventSource<EventSet>::poll(CtQueueBase& queue) {
    if (!m_active || !m_hwnd)
        return; // deactivated or no window - idle (MUST-6 guard)

    // EventSet identity assert BEFORE the downcast (§7.12 contract #6).
    if (queue.eventSetId() != CtTypeKey<EventSet>::value()) {
        assert(false && "CtWin32EventSource polled against a queue of a "
                        "different EventSet");
        return;
    }
    auto& concrete = static_cast<CtQueue<EventSet>&>(queue);

    // Widget-local size for the mapper (WARN-4: client rect). Queried ONCE
    // per poll (the frame boundary) - not per message (the hot path).
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const crisp::ui::CtSize widgetLocalSize{
        client.right - client.left,
        client.bottom - client.top};

    MSG msg;
    // One PeekMessageW sweep per poll (M2 §7.12 polling model): drain all
    // pending messages, removing them from the queue (PM_REMOVE).
    while (PeekMessageW(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
        // WM_TIMER -> CtTimerPayload (D5): the timer id is the wParam.
        if (msg.message == WM_TIMER) {
            CtEvent<crisp::event::CtTimerPayload> ev(
                crisp::event::CtEventOrigin::kSystem,
                crisp::event::CtTimerPayload{
                    static_cast<int>(msg.wParam)});
            concrete.push(std::weak_ptr<crisp::event::CtReceiver>{},
                          std::move(ev));
            continue;
        }

        // Translate the message via the platform mapper (portable carrier).
        platform::CtRawMsg raw;
        raw.hwnd = reinterpret_cast<std::uintptr_t>(msg.hwnd);
        raw.msg = static_cast<std::uint32_t>(msg.message);
        raw.wParam = static_cast<std::uintptr_t>(msg.wParam);
        raw.lParam = static_cast<std::uintptr_t>(msg.lParam);
        raw.time = static_cast<std::uint32_t>(msg.time);
        raw.bootTimeMs = static_cast<std::uint32_t>(GetTickCount64());
        raw.ptX = static_cast<int32_t>(LOWORD(msg.lParam));
        raw.ptY = static_cast<int32_t>(HIWORD(msg.lParam));

        // Translate via the platform mapper. Key first, then mouse - each
        // mapper returns monostate for messages it does not handle, so the
        // double call is exact (both are pure, zero allocation).
        platform::CtInputMapperResult result =
            platform::mapWin32Key(raw);
        if (!platform::isInput(result))
            result = platform::mapWin32Mouse(raw, widgetLocalSize, m_dblClick);

        // Strong-typed push of the mapped payload (zero allocation, MUST-2).
        if (std::holds_alternative<crisp::input::CtMousePayload>(result)) {
            CtEvent<crisp::input::CtMousePayload> ev(
                crisp::event::CtEventOrigin::kSystem,
                std::get<crisp::input::CtMousePayload>(result));
            concrete.push(std::weak_ptr<crisp::event::CtReceiver>{},
                          std::move(ev));
        } else if (std::holds_alternative<crisp::input::CtKeyPayload>(result)) {
            auto& keyPayload = std::get<crisp::input::CtKeyPayload>(result);
            // kUnknown VK (D8/MUST-5): the payload is STILL delivered
            // (never silently dropped) - the receiver sees key==kUnknown -
            // AND the source records the event in the queue trace via
            // recordAsyncDrop (the ONLY public source-side trace channel,
            // M3 bridge §3.3 contract #5 precedent) so the unmapped key is
            // observable to diagnostics (D8: never silently dropped,
            // observable). The mapper stays pure (MUST-5).
            if (keyPayload.key == crisp::input::CtKeyButton::kUnknown) {
                queue.recordAsyncDrop(
                    CtTypeKey<crisp::input::CtKeyPayload>::value());
            }
            CtEvent<crisp::input::CtKeyPayload> ev(
                crisp::event::CtEventOrigin::kSystem,
                keyPayload);
            concrete.push(std::weak_ptr<crisp::event::CtReceiver>{},
                          std::move(ev));
        }
        // std::monostate: non-input message - skip.

        // WM_PAINT: validate the region so Windows stops re-posting it
        // (WARN-4; the update region is consumed by the UI2 paint path).
        if (msg.message == WM_PAINT)
            ValidateRect(m_hwnd, nullptr);
    }
}

template <typename EventSet>
bool CtWin32EventSource<EventSet>::activate() {
    if (m_active)
        return true;
    m_active = true;
    return true;
}

template <typename EventSet>
void CtWin32EventSource<EventSet>::deactivate() noexcept {
    m_active = false;
}

template <typename EventSet>
std::string_view CtWin32EventSource<EventSet>::sourceName() const noexcept {
    return "CtWin32EventSource";
}

// S-3 explicit instantiation: the template class's vtable/out-of-line
// members would otherwise never be emitted (the header lives in a library,
// the instantiation point is the app). MUST-2: instantiated with CtUiEventSet.
template class CtWin32EventSource<crisp::ui::CtUiEventSet>;

} // namespace crisp::ui
