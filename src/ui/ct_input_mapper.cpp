// ct_input_mapper.cpp - UI3 CORE mapper TU: the PORTABLE SHARED MAPPING
// TABLE only (MUST-7). ZERO Qt/Win32 compile dependency - mapWin32* lives
// in platform/ct_input_mapper_win32.cpp, mapBridge* in
// platform/ct_input_mapper_bridge.cpp.
// Authoritative spec: docs/ui/06-ui3-architecture.md section 4.3.
//
// The shared SEMANTIC table maps normalized platform key values to
// CtKeyButton. Non-printing key value spaces differ across platforms
// (Win32 VK 8-bit vs Qt::Key enum) - each platform TU normalizes its
// values to the semantic key name (kF1/kArrowUp/kEnter share ONE row);
// printable ASCII is mapped directly by the platform TUs (S3).
// Zero allocation on every path (hot path, principle #5).

#include <cstdlib>

#include "ct_input_mapper.h"

namespace crisp::ui::platform {

crisp::input::CtKeyButton mapKeyToCt(CtKeySourceKind kind,
                                      std::uint32_t platformKey) noexcept {
    // Non-printing semantic keys. Each row is keyed by semantic name and
    // shared across platforms; the platform TU supplies its own value for
    // the key (P3/M-1). Win32 values are VK_* codes; Qt values are the
    // Qt::Key enum values (documented in the platform TUs).
    switch (kind) {
    case CtKeySourceKind::kWin32Vk: {
        switch (platformKey) {
        case 0x70:  return crisp::input::CtKeyButton::kF1;      // VK_F1
        case 0x71:  return crisp::input::CtKeyButton::kF2;      // VK_F2
        case 0x72:  return crisp::input::CtKeyButton::kF3;      // VK_F3
        case 0x73:  return crisp::input::CtKeyButton::kF4;      // VK_F4
        case 0x74:  return crisp::input::CtKeyButton::kF5;      // VK_F5
        case 0x75:  return crisp::input::CtKeyButton::kF6;      // VK_F6
        case 0x76:  return crisp::input::CtKeyButton::kF7;      // VK_F7
        case 0x77:  return crisp::input::CtKeyButton::kF8;      // VK_F8
        case 0x78:  return crisp::input::CtKeyButton::kF9;      // VK_F9
        case 0x79:  return crisp::input::CtKeyButton::kF10;     // VK_F10
        case 0x7A:  return crisp::input::CtKeyButton::kF11;     // VK_F11
        case 0x7B:  return crisp::input::CtKeyButton::kF12;     // VK_F12
        case 0x26:  return crisp::input::CtKeyButton::kArrowUp;    // VK_UP
        case 0x28:  return crisp::input::CtKeyButton::kArrowDown;  // VK_DOWN
        case 0x25:  return crisp::input::CtKeyButton::kArrowLeft;  // VK_LEFT
        case 0x27:  return crisp::input::CtKeyButton::kArrowRight; // VK_RIGHT
        case 0x24:  return crisp::input::CtKeyButton::kHome;       // VK_HOME
        case 0x23:  return crisp::input::CtKeyButton::kEnd;        // VK_END
        case 0x21:  return crisp::input::CtKeyButton::kPageUp;     // VK_PRIOR
        case 0x22:  return crisp::input::CtKeyButton::kPageDown;   // VK_NEXT
        case 0x0D:  return crisp::input::CtKeyButton::kEnter;      // VK_RETURN
        case 0x08:  return crisp::input::CtKeyButton::kBackspace;  // VK_BACK
        case 0x2E:  return crisp::input::CtKeyButton::kDelete;     // VK_DELETE
        case 0x2D:  return crisp::input::CtKeyButton::kInsert;     // VK_INSERT
        case 0x09:  return crisp::input::CtKeyButton::kTab;        // VK_TAB
        case 0x1B:  return crisp::input::CtKeyButton::kEscape;     // VK_ESCAPE
        case 0x20:  return crisp::input::CtKeyButton::kSpace;      // VK_SPACE
        case 0x14:  return crisp::input::CtKeyButton::kCapsLock;   // VK_CAPITAL
        default:    return crisp::input::CtKeyButton::kUnknown;
        }
    }
    case CtKeySourceKind::kQtKey: {
        switch (platformKey) {
        case 0x01000030: return crisp::input::CtKeyButton::kF1;       // Qt::Key_F1
        case 0x01000031: return crisp::input::CtKeyButton::kF2;       // Qt::Key_F2
        case 0x01000032: return crisp::input::CtKeyButton::kF3;       // Qt::Key_F3
        case 0x01000033: return crisp::input::CtKeyButton::kF4;       // Qt::Key_F4
        case 0x01000034: return crisp::input::CtKeyButton::kF5;       // Qt::Key_F5
        case 0x01000035: return crisp::input::CtKeyButton::kF6;       // Qt::Key_F6
        case 0x01000036: return crisp::input::CtKeyButton::kF7;       // Qt::Key_F7
        case 0x01000037: return crisp::input::CtKeyButton::kF8;       // Qt::Key_F8
        case 0x01000038: return crisp::input::CtKeyButton::kF9;       // Qt::Key_F9
        case 0x01000039: return crisp::input::CtKeyButton::kF10;      // Qt::Key_F10
        case 0x0100003a: return crisp::input::CtKeyButton::kF11;      // Qt::Key_F11
        case 0x0100003b: return crisp::input::CtKeyButton::kF12;      // Qt::Key_F12
        case 0x01000013: return crisp::input::CtKeyButton::kArrowUp;    // Qt::Key_Up
        case 0x01000015: return crisp::input::CtKeyButton::kArrowDown;  // Qt::Key_Down
        case 0x01000012: return crisp::input::CtKeyButton::kArrowLeft;  // Qt::Key_Left
        case 0x01000014: return crisp::input::CtKeyButton::kArrowRight; // Qt::Key_Right
        case 0x01000010: return crisp::input::CtKeyButton::kHome;       // Qt::Key_Home
        case 0x01000011: return crisp::input::CtKeyButton::kEnd;        // Qt::Key_End
        case 0x01000016: return crisp::input::CtKeyButton::kPageUp;     // Qt::Key_PageUp
        case 0x01000017: return crisp::input::CtKeyButton::kPageDown;   // Qt::Key_PageDown
        case 0x01000004: return crisp::input::CtKeyButton::kEnter;      // Qt::Key_Return
        case 0x01000003: return crisp::input::CtKeyButton::kBackspace;  // Qt::Key_Backspace
        case 0x01000007: return crisp::input::CtKeyButton::kDelete;     // Qt::Key_Delete
        case 0x01000006: return crisp::input::CtKeyButton::kInsert;     // Qt::Key_Insert
        case 0x01000001: return crisp::input::CtKeyButton::kTab;        // Qt::Key_Tab
        case 0x01000000: return crisp::input::CtKeyButton::kEscape;     // Qt::Key_Escape
        case 0x20:       return crisp::input::CtKeyButton::kSpace;      // Qt::Key_Space
        case 0x01000024: return crisp::input::CtKeyButton::kCapsLock;   // Qt::Key_CapsLock
        default:         return crisp::input::CtKeyButton::kUnknown;
        }
    }
    }
    return crisp::input::CtKeyButton::kUnknown;
}

bool isDoubleClick(CtDblClickState& state,
                   std::chrono::steady_clock::time_point now,
                   int x, int y,
                   std::chrono::milliseconds maxGap,
                   int maxDistance) noexcept {
    const bool hit = state.m_hasLast &&
                     (now - state.m_lastPress) <= maxGap &&
                     std::abs(x - state.m_lastX) <= maxDistance &&
                     std::abs(y - state.m_lastY) <= maxDistance;
    // Record this press as the new first press either way (a double click
    // can only be decided against the PREVIOUS press).
    state.m_lastPress = now;
    state.m_lastX = x;
    state.m_lastY = y;
    state.m_hasLast = true;
    return hit;
}

} // namespace crisp::ui::platform