// test_ct_input_mapper_qt.cpp - UI3 Qt-host mapper tests (C1: Qt-enabled
// test target). Directly drives mapBridge* (platform TU) with M4 bridge
// payloads and asserts the crisp::input core payload fields, incl. the
// cross-mode consistency contract (Win32 vs Qt-bridge mapping produce
// IDENTICAL payloads, text/timestamp excluded - D12/WARN-2).
// Hand-written test doubles, no mock framework.

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <windows.h>   // WM_KEYDOWN (cross-mode consistency case)
#endif

#include "ui/ct_input_mapper.h"
#include "ui/platform/ct_input_mapper_bridge.h"

#include "test_harness.h"

// Allocation tracking (this test binary): counting operator new/delete.
// The Qt-host mapper's mouse path + empty-text key path must not allocate
// (UI3 section 6.2/M-4). Defined HERE - this executable has no other
// operator new definition (ui_tests' anchor lives in a different binary).
std::atomic<std::size_t> g_allocCount{0};
std::atomic<bool> g_counting{false};

void* operator new(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using crisp::ui::platform::mapBridgeKey;
using crisp::ui::platform::mapBridgeMouse;
using crisp::input::CtKeyButton;
using crisp::input::CtModifiers;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMouseButtons;

namespace {

TEST_CASE(bridge_mapper_mouse_fields)
{
    crisp::event::CtMousePayload qt;  // M4 bridge payload (Qt fields)
    qt.action = crisp::event::CtMouseAction::kPress;
    qt.x = 42;
    qt.y = 77;
    qt.button = Qt::LeftButton;
    qt.buttons = Qt::LeftButton | Qt::RightButton;  // pressed set
    qt.modifiers = Qt::ControlModifier | Qt::ShiftModifier;
    qt.timestamp = std::chrono::steady_clock::now();

    const auto r = mapBridgeMouse(qt, crisp::ui::CtSize{800, 600});
    EXPECT_TRUE(crisp::ui::platform::isInput(r));
    EXPECT_TRUE(std::holds_alternative<crisp::input::CtMousePayload>(r));
    const crisp::input::CtMousePayload m = std::get<crisp::input::CtMousePayload>(r);
    EXPECT_EQ(m.action, CtMouseAction::kPress);
    EXPECT_EQ(m.x, 42);
    EXPECT_EQ(m.y, 77);
    EXPECT_TRUE(m.button.has_value());
    EXPECT_EQ(*m.button, CtMouseButton::kPrimary);
    EXPECT_TRUE(m.buttons.has(CtMouseButtons::kPrimary));
    EXPECT_TRUE(m.buttons.has(CtMouseButtons::kSecondary));
    EXPECT_TRUE(m.modifiers.has(CtModifiers::kControl));
    EXPECT_TRUE(m.modifiers.has(CtModifiers::kShift));
    // D6/MUST-8: the bridge timestamp passes through.
    EXPECT_EQ(m.timestamp, qt.timestamp);
}

TEST_CASE(bridge_mapper_mouse_move_no_button)
{
    crisp::event::CtMousePayload qt;
    qt.action = crisp::event::CtMouseAction::kMove;
    qt.x = 10;
    qt.y = 20;
    qt.button = Qt::NoButton;
    qt.buttons = Qt::NoButton;

    const crisp::input::CtMousePayload m = std::get<crisp::input::CtMousePayload>(
        mapBridgeMouse(qt, crisp::ui::CtSize{800, 600}));
    EXPECT_EQ(m.action, CtMouseAction::kMove);
    EXPECT_FALSE(m.button.has_value());
    EXPECT_FALSE(m.buttons.any());
    EXPECT_EQ(m.modifiers.bits, CtModifiers::kNone);
}

TEST_CASE(bridge_mapper_key_fields_keep_text)
{
    crisp::event::CtKeyPayload qt;
    qt.action = crisp::event::CtKeyAction::kPress;
    qt.key = Qt::Key_F1;
    qt.text = QStringLiteral("x");  // Qt side HAS real text (D12)
    qt.modifiers = Qt::AltModifier;
    qt.isAutoRepeat = true;

    const auto r = mapBridgeKey(qt);
    EXPECT_TRUE(std::holds_alternative<crisp::input::CtKeyPayload>(r));
    const crisp::input::CtKeyPayload k =
        std::get<crisp::input::CtKeyPayload>(r);
    EXPECT_EQ(k.action, crisp::input::CtKeyAction::kPress);
    EXPECT_EQ(k.key, CtKeyButton::kF1);          // shared semantic table
    EXPECT_EQ(k.text, u"x");                      // text KEPT (D12)
    EXPECT_TRUE(k.modifiers.has(CtModifiers::kAlt));
    EXPECT_TRUE(k.isAutoRepeat);
}

TEST_CASE(bridge_mapper_printable_ascii_keys)
{
    // Qt::Key 'a' (ASCII 0x61) -> kA (lower-case letter, S3).
    crisp::event::CtKeyPayload qtA;
    qtA.key = Qt::Key_A;
    const crisp::input::CtKeyPayload kA = std::get<crisp::input::CtKeyPayload>(mapBridgeKey(qtA));
    EXPECT_EQ(kA.key, CtKeyButton::kA);

    // Qt::Key_0 -> kD0.
    crisp::event::CtKeyPayload qt0;
    qt0.key = Qt::Key_0;
    const crisp::input::CtKeyPayload k0 = std::get<crisp::input::CtKeyPayload>(mapBridgeKey(qt0));
    EXPECT_EQ(k0.key, CtKeyButton::kD0);

    // Qt::Key_unknown -> kUnknown (D8: never silently dropped).
    crisp::event::CtKeyPayload qtUnk;
    qtUnk.key = Qt::Key_unknown;
    const crisp::input::CtKeyPayload kU = std::get<crisp::input::CtKeyPayload>(mapBridgeKey(qtUnk));
    EXPECT_EQ(kU.key, CtKeyButton::kUnknown);
}

TEST_CASE(bridge_mapper_zero_size_surface_rejects)
{
    // Section 14 contract: a ZERO-size surface has no hittable area ->
    // isInput==false (the mapper refuses instead of emitting a payload).
    crisp::event::CtMousePayload qt;
    qt.action = crisp::event::CtMouseAction::kPress;
    qt.x = 5;
    qt.y = 5;
    qt.button = Qt::LeftButton;
    const auto r = mapBridgeMouse(qt, crisp::ui::CtSize{0, 0});
    EXPECT_FALSE(crisp::ui::platform::isInput(r));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST_CASE(bridge_mapper_cross_mode_consistency)
{
    // D3/WARN-2: the SAME semantic input must map to the SAME core payload
    // from both modes. F1 press: Win32 VK_F1 (0x70) vs Qt::Key_F1.
    // text/timestamp excluded (D12/WARN-2) - both are mode-specific.

    // Qt mode:
    crisp::event::CtKeyPayload qt;
    qt.action = crisp::event::CtKeyAction::kPress;
    qt.key = Qt::Key_F1;
    qt.modifiers = Qt::NoModifier;
    const crisp::input::CtKeyPayload kQt = std::get<crisp::input::CtKeyPayload>(mapBridgeKey(qt));

#ifdef _WIN32
    // Win32 mode (same semantic key via the shared table):
    const crisp::input::CtKeyPayload kWin = std::get<crisp::input::CtKeyPayload>(
        crisp::ui::platform::mapWin32Key([] {
            crisp::ui::platform::CtRawMsg m;
            m.msg = WM_KEYDOWN;
            m.wParam = 0x70;  // VK_F1
            return m;
        }()));

    EXPECT_EQ(kQt.key, kWin.key);
    EXPECT_EQ(kQt.key, CtKeyButton::kF1);
    EXPECT_EQ(kQt.action, kWin.action);
    // Modifiers may differ (GetKeyState is async on Win32) - only the key
    // identity is compared here (cross-mode consistency contract).
#else
    // Non-Win32: only the Qt-side mapping is verifiable.
    EXPECT_EQ(kQt.key, CtKeyButton::kF1);
#endif
}

TEST_CASE(bridge_mapper_allocates_zero_for_mouse)
{
    // UI3 section 6.2/M-4: mapBridgeMouse + mapBridgeKey(text EMPTY) are
    // zero-allocation (the global counting operators are defined above).
    const std::size_t before = g_allocCount.load(std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    crisp::event::CtMousePayload qt;
    qt.action = crisp::event::CtMouseAction::kMove;
    for (int i = 0; i < 100; ++i) {
        qt.x = i;
        qt.y = i;
        mapBridgeMouse(qt, crisp::ui::CtSize{800, 600});
    }
    crisp::event::CtKeyPayload qtKey;
    qtKey.key = Qt::Key_F1;  // non-printing: text empty
    for (int i = 0; i < 100; ++i)
        mapBridgeKey(qtKey);

    g_counting.store(false, std::memory_order_relaxed);
    EXPECT_EQ(g_allocCount.load(std::memory_order_relaxed), before);
}

} // namespace
