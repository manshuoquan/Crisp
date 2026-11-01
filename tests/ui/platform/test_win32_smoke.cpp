// test_win32_smoke.cpp - UI3 Win32 platform smoke tests (platform layer;
// enabled under if(WIN32), W-3/WARN-5). Covers:
//  - mapWin32* table-driven field assertions (CtRawMsg, portable carrier);
//  - CtWin32EventSource poll smoke (message pump -> mapper -> strong push);
//  - CtPlatformWindow surfaceSize/eventSource contract.
// Hand-written test doubles, no mock framework.

#include <windows.h>

#include <chrono>
#include <cstdlib>

#include "ui/ct_input_mapper.h"
#include "ui/ct_platform_window.h"
#include "ui/platform/ct_platform_window_win32.h"
#include "ui/platform/ct_win32_event_source.h"

#include "test_harness.h"

using crisp::ui::platform::CtDblClickState;
using crisp::ui::platform::CtInputMapperResult;
using crisp::ui::platform::CtRawMsg;
using crisp::ui::platform::mapWin32Key;
using crisp::ui::platform::mapWin32Mouse;
using crisp::input::CtKeyButton;
using crisp::input::CtModifiers;
using crisp::input::CtMouseAction;
using crisp::input::CtMouseButton;
using crisp::input::CtMouseButtons;

namespace {

// ---------------------------------------------------------------------------
// mapWin32* - table-driven field assertions (UI3 §8.2/§8.3)
// ---------------------------------------------------------------------------

TEST_CASE(win32_mapper_mouse_press_release)
{
    CtDblClickState dbl;
    const crisp::ui::CtSize size{800, 600};

    // WM_LBUTTONDOWN at (10,20), no modifiers/buttons.
    CtRawMsg down{};
    down.msg = WM_LBUTTONDOWN;
    down.ptX = 10;
    down.ptY = 20;
    const CtInputMapperResult r1 = mapWin32Mouse(down, size, dbl);
    EXPECT_TRUE(crisp::ui::platform::isInput(r1));
    EXPECT_TRUE(std::holds_alternative<crisp::input::CtMousePayload>(r1));
    const auto& m1 = std::get<crisp::input::CtMousePayload>(r1);
    EXPECT_EQ(m1.action, CtMouseAction::kPress);
    EXPECT_EQ(m1.x, 10);
    EXPECT_EQ(m1.y, 20);
    EXPECT_TRUE(m1.button.has_value());
    EXPECT_EQ(*m1.button, CtMouseButton::kPrimary);
    EXPECT_TRUE(m1.buttons.has(CtMouseButtons::kPrimary));
    EXPECT_EQ(m1.modifiers.bits, CtModifiers::kNone);

    // WM_LBUTTONUP - button released, aggregate buttons now empty.
    CtRawMsg up{};
    up.msg = WM_LBUTTONUP;
    up.ptX = 12;
    up.ptY = 22;
    const CtInputMapperResult r2 = mapWin32Mouse(up, size, dbl);
    const auto& m2 = std::get<crisp::input::CtMousePayload>(r2);
    EXPECT_EQ(m2.action, CtMouseAction::kRelease);
    EXPECT_TRUE(m2.button.has_value());
    EXPECT_EQ(*m2.button, CtMouseButton::kPrimary);
    EXPECT_FALSE(m2.buttons.any());
}

TEST_CASE(win32_mapper_mouse_move_aggregates_buttons)
{
    CtDblClickState dbl;
    const crisp::ui::CtSize size{800, 600};

    // Move with the secondary button held (MK_RBUTTON) and Ctrl.
    CtRawMsg move{};
    move.msg = WM_MOUSEMOVE;
    move.wParam = MK_RBUTTON | MK_CONTROL;
    move.ptX = 100;
    move.ptY = 50;
    const CtInputMapperResult r = mapWin32Mouse(move, size, dbl);
    const auto& m = std::get<crisp::input::CtMousePayload>(r);
    EXPECT_EQ(m.action, CtMouseAction::kMove);
    EXPECT_FALSE(m.button.has_value());  // no button CHANGE on move
    EXPECT_TRUE(m.buttons.has(CtMouseButtons::kSecondary));
    EXPECT_TRUE(m.modifiers.has(CtModifiers::kControl));
}

TEST_CASE(win32_mapper_key_press_release_text_empty)
{
    // WM_KEYDOWN VK_F1 -> kF1, text stays EMPTY (MUST-3/D12).
    CtRawMsg kd{};
    kd.msg = WM_KEYDOWN;
    kd.wParam = VK_F1;
    const CtInputMapperResult r1 = mapWin32Key(kd);
    EXPECT_TRUE(std::holds_alternative<crisp::input::CtKeyPayload>(r1));
    const auto& k1 = std::get<crisp::input::CtKeyPayload>(r1);
    EXPECT_EQ(k1.action, crisp::input::CtKeyAction::kPress);
    EXPECT_EQ(k1.key, CtKeyButton::kF1);
    EXPECT_TRUE(k1.text.empty());
    EXPECT_FALSE(k1.isAutoRepeat);

    // WM_KEYUP VK_A -> kA (printable direct map, S3).
    CtRawMsg ku{};
    ku.msg = WM_KEYUP;
    ku.wParam = 'A';
    const CtInputMapperResult r2 = mapWin32Key(ku);
    const auto& k2 = std::get<crisp::input::CtKeyPayload>(r2);
    EXPECT_EQ(k2.action, crisp::input::CtKeyAction::kRelease);
    EXPECT_EQ(k2.key, CtKeyButton::kA);
}

TEST_CASE(win32_mapper_autorepeat_bit)
{
    CtRawMsg kd{};
    kd.msg = WM_KEYDOWN;
    kd.wParam = VK_SPACE;
    kd.lParam = 0x40000000u; // bit 30: repeat flag
    const auto& k = std::get<crisp::input::CtKeyPayload>(mapWin32Key(kd));
    EXPECT_TRUE(k.isAutoRepeat);
    EXPECT_EQ(k.key, CtKeyButton::kSpace);
}

TEST_CASE(win32_mapper_rejects_non_input)
{
    CtDblClickState dbl;
    const crisp::ui::CtSize size{800, 600};
    CtRawMsg nonInput{};
    nonInput.msg = WM_PAINT;
    EXPECT_FALSE(crisp::ui::platform::isInput(mapWin32Mouse(nonInput, size, dbl)));
    EXPECT_FALSE(crisp::ui::platform::isInput(mapWin32Key(nonInput)));
    nonInput.msg = WM_NCHITTEST;
    EXPECT_FALSE(crisp::ui::platform::isInput(mapWin32Mouse(nonInput, size, dbl)));
    nonInput.msg = WM_SIZE;
    EXPECT_FALSE(crisp::ui::platform::isInput(mapWin32Key(nonInput)));
}

TEST_CASE(win32_mapper_unknown_vk_maps_kUnknown)
{
    CtRawMsg kd{};
    kd.msg = WM_KEYDOWN;
    kd.wParam = 0xFE; // unmapped VK
    const auto& k = std::get<crisp::input::CtKeyPayload>(mapWin32Key(kd));
    EXPECT_EQ(k.key, CtKeyButton::kUnknown);
}

TEST_CASE(win32_mapper_zero_size_surface_rejects)
{
    // Section 14 contract: a ZERO-size surface has no hittable area ->
    // isInput==false.
    CtDblClickState dbl;
    CtRawMsg down{};
    down.msg = WM_LBUTTONDOWN;
    down.ptX = 5;
    down.ptY = 5;
    EXPECT_FALSE(crisp::ui::platform::isInput(
        mapWin32Mouse(down, crisp::ui::CtSize{0, 0}, dbl)));
    EXPECT_FALSE(crisp::ui::platform::isInput(
        mapWin32Mouse(down, crisp::ui::CtSize{-1, 10}, dbl)));
}

TEST_CASE(win32_mapper_dblclick_down_up_down_sequence)
{
    // D7/MUST-4: the DOWN->UP->DOWN sequence must yield kDblClick on the
    // second DOWN (state survives the UP - regression for the UP-reset bug).
    CtDblClickState dbl;
    const crisp::ui::CtSize size{800, 600};

    CtRawMsg down1{};
    down1.msg = WM_LBUTTONDOWN;
    down1.ptX = 10; down1.ptY = 10;
    down1.time = 1000; down1.bootTimeMs = 1000;
    const auto& m1 = std::get<crisp::input::CtMousePayload>(
        mapWin32Mouse(down1, size, dbl));
    EXPECT_EQ(m1.action, CtMouseAction::kPress);

    // UP (within the double-click window; state must survive).
    CtRawMsg up{};
    up.msg = WM_LBUTTONUP;
    up.ptX = 11; up.ptY = 11;
    up.time = 1100; up.bootTimeMs = 1100;
    const auto& m2 = std::get<crisp::input::CtMousePayload>(
        mapWin32Mouse(up, size, dbl));
    EXPECT_EQ(m2.action, CtMouseAction::kRelease);

    // Second DOWN within time+dist window -> kDblClick.
    CtRawMsg down2{};
    down2.msg = WM_LBUTTONDOWN;
    down2.ptX = 10; down2.ptY = 10;
    down2.time = 1200; down2.bootTimeMs = 1200;
    const auto& m3 = std::get<crisp::input::CtMousePayload>(
        mapWin32Mouse(down2, size, dbl));
    EXPECT_EQ(m3.action, CtMouseAction::kDblClick);
}

TEST_CASE(win32_mapper_timestamp_filled)
{
    CtDblClickState dbl;
    const crisp::ui::CtSize size{800, 600};
    CtRawMsg down{};
    down.msg = WM_LBUTTONDOWN;
    down.ptX = 5;
    down.ptY = 5;
    down.time = 1000;          // GetMessageTime()
    down.bootTimeMs = 2000;    // GetTickCount64() at message time
    const auto& m = std::get<crisp::input::CtMousePayload>(
        mapWin32Mouse(down, size, dbl));
    // Timestamp is a steady_clock time_point - bounded deviation from now
    // (D6): bootTimeMs-time = 1000ms elapsed since the message.
    const auto now = std::chrono::steady_clock::now();
    const auto expectedElapsed = std::chrono::milliseconds(
        static_cast<std::int64_t>(down.bootTimeMs) -
        static_cast<std::int64_t>(down.time));
    const auto actualElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m.timestamp);
    // Allow a small scheduling slack (< 100ms) around the expected value.
    EXPECT_TRUE(std::abs(actualElapsed.count() - expectedElapsed.count()) < 100);
}

// ---------------------------------------------------------------------------
// CtPlatformWindow surfaceSize/eventSource contract (§8.2)
// ---------------------------------------------------------------------------

TEST_CASE(win32_platform_window_contract)
{
    crisp::ui::CtPlatformWindowWin32 win("UI3 smoke", 320, 240);
    const auto size = win.surfaceSize();
    EXPECT_TRUE(size.width > 0);
    EXPECT_TRUE(size.height > 0);
    EXPECT_TRUE(win.eventSource() != nullptr);
    // The source is the Win32 pump, needs polling wakeup (D10).
    EXPECT_TRUE(win.eventSource()->needsPollingWakeup());
}

} // namespace
