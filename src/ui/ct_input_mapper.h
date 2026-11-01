#pragma once

// ct_input_mapper.h - UI3 THE only sanctioned mapping from platform
// input (Win32 messages / Qt bridged events) into crisp::input payloads
// (docs/ui/README.md "Joint design points", ct_input.h header contract).
// Authoritative spec: docs/ui/06-ui3-architecture.md sections 3.3/3.4/4.3.
//
// THREE blocks merged into ONE header (S1): section 3.3 CtUiEventSet
// (crisp::ui) + section 3.4 CtInputMapperResult + section 4.3 mapper core
// (crisp::ui::platform). DECISION D3 (section 10): the mapper is the
// SINGLE mapping boundary shared by BOTH modes - Win32 maps raw MSG
// (platform TU); Qt-host maps bridged M4 payloads (platform TU) - so the
// widget layer sees IDENTICAL payloads.
// MUST-7: the CORE TU implements only the PORTABLE SHARED MAPPING TABLE
// (mapKeyToCt + isDoubleClick); mapWin32* (platform/ct_input_mapper_win32.cpp)
// and mapBridge* (platform/ct_input_mapper_bridge.cpp) live in platform TUs.
//
// NOTE (MUST-7 revision): the CORE mapper header carries NO Qt/Win32
// dependency - the mapBridge* signatures live in the PLATFORM layer
// (platform/ct_input_mapper_bridge.h, Qt-host mode) because they
// reference the bridge payloads (which include QString/Qt::Key). The
// core TU implements only the portable shared mapping table (uint32_t
// keys, see platform/ct_input_mapper_win32.cpp / _bridge.cpp).

#include <chrono>
#include <cstdint>
#include <variant>

#include "event/ct_input.h"          // crisp::input enums
#include "event/ct_event_set.h"      // crisp::event::CtEventSet (CtUiEventSet)
#include "event/ct_timer_event_source.h" // crisp::event::CtTimerPayload (D5)
#include "event/ct_mouse_payload.h"  // crisp::input::CtMousePayload
#include "event/ct_key_payload.h"    // crisp::input::CtKeyPayload
#include "ct_paint_types.h"          // CtSize (defined here, M-3)

// NOTE: ct_timer_event_source.h is CORE event-system header (crisp::event,
// zero Qt/Win32 dependency) - CtEventTraits<CtTimerPayload> needs the full
// definition for CtEventSet's static_asserts. NOT a MUST-7 violation: the
// core mapper stays Qt/Win32-free.

// Block 1 (S1): section 3.3 - CtUiEventSet (crisp::ui). MUST stay in
// crisp::ui: the UI input EventSet carried by CtWin32EventSource<CtUiEventSet>.
namespace crisp::ui {

/// UI input EventSet (MUST-2): the strong-typed event set carried by
/// CtWin32EventSource<CtUiEventSet> - mouse + key input plus WM_TIMER
/// (D5). Precedent: ct_timer_event_source.h / qt_event_source.h are
/// EventSet templates (04 section 7.12 contract #6).
using CtUiEventSet = crisp::event::CtEventSet<
    crisp::input::CtMousePayload,   ///< mouse input (typed)
    crisp::input::CtKeyPayload,     ///< keyboard input (typed)
    crisp::event::CtTimerPayload>;  ///< WM_TIMER (D5)

} // namespace crisp::ui

// Block 2 + 3 (S1): section 3.4 result type + section 4.3 mapper core
// (crisp::ui::platform).
namespace crisp::ui::platform {

/// Result of mapping one platform message: an input event (mouse or key)
/// or "not input" (WM_PAINT etc.). Compile-time discriminated union
/// (MUST-9 ruling, principle #1): callers visit() the variant - no int
/// kind magic numbers, no invalid states. std::monostate = "not input".
using CtInputMapperResult =
    std::variant<std::monostate,   ///< message carries no UI input
                 crisp::input::CtMousePayload,
                 crisp::input::CtKeyPayload>;

/// True if the result mapped to an input event (not monostate).
inline bool isInput(const CtInputMapperResult& r) noexcept {
    return !std::holds_alternative<std::monostate>(r);
}

/// Platform key source kind for the shared mapping table (P3): the
/// non-printing key value spaces differ (VK 8-bit vs Qt::Key enum), so
/// each platform TU normalizes its values to the shared semantic table.
enum class CtKeySourceKind : std::uint8_t {
    kWin32Vk = 0, ///< Windows virtual-key code (8-bit)
    kQtKey = 1,   ///< Qt::Key enum value
};

/// PORTABLE shared mapping table entry point (P3/M-1): maps a platform
/// key value (normalized by kind) to a CRISP CtKeyButton via the shared
/// SEMANTIC table (kF1/kArrowUp/kEnter... share one row across
/// platforms; each platform TU supplies its own value for a semantic
/// key). Returns kUnknown for values outside the table (never silently
/// drops, D8). CORE TU (ct_input_mapper.cpp), zero allocation.
crisp::input::CtKeyButton mapKeyToCt(CtKeySourceKind kind,
                                     std::uint32_t platformKey) noexcept;

/// Portable raw Win32 message (WARN-3): the mapper stays type-safe and
/// portable - the platform TU converts MSG -> CtRawMsg.
struct CtRawMsg {
    std::uintptr_t hwnd = 0;   ///< window handle
    std::uint32_t msg = 0;     ///< WM_* id
    std::uintptr_t wParam = 0; ///< message wParam
    std::uintptr_t lParam = 0; ///< message lParam
    std::uint32_t time = 0;    ///< GetMessageTime() ms
    std::uint32_t bootTimeMs = 0; ///< GetTickCount64() ms at message time
                                  ///< (P5: required by D6 timestamp conversion)
    int32_t ptX = 0;           ///< client x (LOWORD(lParam) for mouse)
    int32_t ptY = 0;           ///< client y (HIWORD(lParam) for mouse)
};

/// Double-click detection state (MUST-4): passed to mapWin32Mouse BY
/// REFERENCE so the mapper stays a pure function with explicit,
/// testable state (GetDoubleClickTime + distance window). Owned by
/// CtWin32EventSource (platform TU); zero allocation.
/// m_ member prefix per AGENTS.md (public data members of a state type).
struct CtDblClickState {
    std::chrono::steady_clock::time_point m_lastPress{};
    int m_lastX = 0;
    int m_lastY = 0;
    bool m_hasLast = false;
    /// Reset when the window is destroyed / a non-press event arrives.
    void reset() noexcept { m_hasLast = false; }
};

/// Map a Win32 message (portable CtRawMsg) to a crisp::input payload.
/// Returns monostate for messages that carry no UI input.
/// PLATFORM TU (platform/ct_input_mapper_win32.cpp).
CtInputMapperResult
    mapWin32Mouse(const CtRawMsg& msg, const CtSize& widgetLocalSize,
                  CtDblClickState& dblClick) noexcept;
CtInputMapperResult
    mapWin32Key(const CtRawMsg& msg) noexcept;

/// PORTABLE double-click predicate (W-7): the time+distance window logic
/// lives in the CORE TU (ct_input_mapper.cpp) so it is testable without
/// any platform dependency; mapWin32Mouse (platform TU) calls it with
/// the GetDoubleClickTime()/distance-window values it resolves.
/// Returns true when a press at (x,y) at `now` completes a double click
/// against `state` (updating the state to the new press either way).
bool isDoubleClick(CtDblClickState& state,
                   std::chrono::steady_clock::time_point now,
                   int x, int y,
                   std::chrono::milliseconds maxGap,
                   int maxDistance) noexcept;

} // namespace crisp::ui::platform