// Unit tests: crisp::ui UI3 core input mapper (portable shared mapping
// table + double-click predicate). docs/ui/06-ui3-architecture.md §4.3/§8.2.
//
// The CORE TU (ct_input_mapper.cpp) implements ONLY the portable shared
// mapping table (mapKeyToCt + isDoubleClick) - no Win32/Qt dependency
// (MUST-7). mapWin32* lives in the platform TU; the platform smoke tests
// cover it (tests/ui/platform/). Hand-written test doubles, no mock.

#include <chrono>

#include "ui/ct_input_mapper.h"

#include "alloc_tracking.h" // shared g_allocCount/g_counting (defined in
                            // test_ct_widget_base.cpp, the UI1 anchor)
#include "test_harness.h"

using crisp::ui::platform::CtDblClickState;
using crisp::ui::platform::CtKeySourceKind;
using crisp::ui::platform::isDoubleClick;
using crisp::ui::platform::mapKeyToCt;
using crisp::input::CtKeyButton;

namespace {

// ---------------------------------------------------------------------------
// mapKeyToCt - shared semantic table (kWin32Vk / kQtKey rows)
// ---------------------------------------------------------------------------

TEST_CASE(mapper_shared_table_win32_vk)
{
    // Function row.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x70), CtKeyButton::kF1);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x7B), CtKeyButton::kF12);
    // Navigation cluster.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x26), CtKeyButton::kArrowUp);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x28), CtKeyButton::kArrowDown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x25), CtKeyButton::kArrowLeft);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x27), CtKeyButton::kArrowRight);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x24), CtKeyButton::kHome);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x23), CtKeyButton::kEnd);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x21), CtKeyButton::kPageUp);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x22), CtKeyButton::kPageDown);
    // Editing keys.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x0D), CtKeyButton::kEnter);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x08), CtKeyButton::kBackspace);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x2E), CtKeyButton::kDelete);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x2D), CtKeyButton::kInsert);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x09), CtKeyButton::kTab);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x1B), CtKeyButton::kEscape);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x20), CtKeyButton::kSpace);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x14), CtKeyButton::kCapsLock);
}

TEST_CASE(mapper_shared_table_qt_key)
{
    // Function row (Qt::Key values, qnamespace.h).
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000030), CtKeyButton::kF1);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x0100003b), CtKeyButton::kF12);
    // Navigation cluster.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000013), CtKeyButton::kArrowUp);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000015), CtKeyButton::kArrowDown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000012), CtKeyButton::kArrowLeft);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000014), CtKeyButton::kArrowRight);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000010), CtKeyButton::kHome);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000011), CtKeyButton::kEnd);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000016), CtKeyButton::kPageUp);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000017), CtKeyButton::kPageDown);
    // Editing keys.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000004), CtKeyButton::kEnter);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000003), CtKeyButton::kBackspace);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000007), CtKeyButton::kDelete);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000006), CtKeyButton::kInsert);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000001), CtKeyButton::kTab);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000000), CtKeyButton::kEscape);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x20), CtKeyButton::kSpace);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000024), CtKeyButton::kCapsLock);
}

TEST_CASE(mapper_shared_table_unknown_values_map_kUnknown)
{
    // Values outside the table never silently drop - kUnknown (D8).
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x00), CtKeyButton::kUnknown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0xFF), CtKeyButton::kUnknown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, 0x41 /* VK_A - printable,
               platform TU maps directly, NOT the shared table (S3) */),
              CtKeyButton::kUnknown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 0x01000000 + 0x1000),
              CtKeyButton::kUnknown);
    // Printable keys (S3: ASCII range) are NOT in the shared table - the
    // platform TUs map them directly. Assert the table returns kUnknown so
    // the platform TU can't accidentally rely on it.
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 'a'), CtKeyButton::kUnknown);
    EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, 'z'), CtKeyButton::kUnknown);
}

TEST_CASE(mapper_semantic_rows_shared_across_platforms)
{
    // The SAME semantic key maps to the same CtKeyButton from both kinds -
    // the widget layer sees identical payloads from either mode (D3).
    const struct { CtKeyButton expect; uint32_t vk; uint32_t qt; } rows[] = {
        {CtKeyButton::kF1,       0x70,           0x01000030},
        {CtKeyButton::kF12,      0x7B,           0x0100003b},
        {CtKeyButton::kArrowUp,  0x26,           0x01000013},
        {CtKeyButton::kEnter,    0x0D,           0x01000004},
        {CtKeyButton::kEscape,   0x1B,           0x01000000},
        {CtKeyButton::kSpace,    0x20,           0x20},
        {CtKeyButton::kTab,      0x09,           0x01000001},
    };
    for (const auto& r : rows) {
        EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kWin32Vk, r.vk), r.expect);
        EXPECT_EQ(mapKeyToCt(CtKeySourceKind::kQtKey, r.qt), r.expect);
    }
}

// ---------------------------------------------------------------------------
// isDoubleClick - portable time+distance window (W-7)
// ---------------------------------------------------------------------------

TEST_CASE(mapper_dblclick_detection_time_and_distance_windows)
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const std::chrono::milliseconds maxGap(500);
    const int maxDist = 4;

    CtDblClickState st;

    // First press: no previous press -> not a double click.
    EXPECT_FALSE(isDoubleClick(st, t0, 10, 10, maxGap, maxDist));
    // Second press within time+dist window -> double click.
    EXPECT_TRUE(isDoubleClick(st, t0 + std::chrono::milliseconds(100),
                              11, 9, maxGap, maxDist));
    // Third press within window (of the second) -> double click again.
    EXPECT_TRUE(isDoubleClick(st, t0 + std::chrono::milliseconds(200),
                              10, 10, maxGap, maxDist));
}

TEST_CASE(mapper_dblclick_outside_time_window_is_single)
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const std::chrono::milliseconds maxGap(500);
    const int maxDist = 4;

    CtDblClickState st;
    EXPECT_FALSE(isDoubleClick(st, t0, 10, 10, maxGap, maxDist));
    // Too slow -> single click; the state resets to this press.
    EXPECT_FALSE(isDoubleClick(st, t0 + std::chrono::milliseconds(600),
                               10, 10, maxGap, maxDist));
    // And a quick follow-up now IS a double click against the new press.
    EXPECT_TRUE(isDoubleClick(st, t0 + std::chrono::milliseconds(700),
                              10, 10, maxGap, maxDist));
}

TEST_CASE(mapper_dblclick_outside_distance_window_is_single)
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const std::chrono::milliseconds maxGap(500);
    const int maxDist = 4;

    CtDblClickState st;
    EXPECT_FALSE(isDoubleClick(st, t0, 10, 10, maxGap, maxDist));
    // Within time, but outside the distance window -> single click.
    EXPECT_FALSE(isDoubleClick(st, t0 + std::chrono::milliseconds(100),
                               15, 10, maxGap, maxDist));
}

TEST_CASE(mapper_dblclick_state_reset)
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const std::chrono::milliseconds maxGap(500);
    const int maxDist = 4;

    CtDblClickState st;
    EXPECT_FALSE(isDoubleClick(st, t0, 10, 10, maxGap, maxDist));
    st.reset(); // e.g. window destroyed / non-press event (MUST-4)
    EXPECT_FALSE(st.m_hasLast);
    EXPECT_FALSE(isDoubleClick(st, t0 + std::chrono::milliseconds(50),
                               10, 10, maxGap, maxDist));
}

// ---------------------------------------------------------------------------
// Zero-allocation (UI3 §6.2, M-4): core shared-table mapping is allocation-free
// ---------------------------------------------------------------------------

TEST_CASE(mapper_shared_table_allocates_zero)
{
    // The shared table + dbl-click predicate are pure value logic - no
    // heap allocation on any path. Arm the global counter (defined in
    // test_ct_widget_base.cpp, the UI1 allocation-tracking anchor).
    const std::size_t before = g_allocCount.load(std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (uint32_t vk = 0; vk <= 0xFF; ++vk)
        mapKeyToCt(CtKeySourceKind::kWin32Vk, vk);
    for (uint32_t k = 0; k <= 0x010000FF; k += 0x11)
        mapKeyToCt(CtKeySourceKind::kQtKey, k);

    CtDblClickState st;
    const auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        isDoubleClick(st, now + std::chrono::milliseconds(i),
                      10 + (i % 3), 10, std::chrono::milliseconds(500), 4);

    g_counting.store(false, std::memory_order_relaxed);
    EXPECT_EQ(g_allocCount.load(std::memory_order_relaxed), before);
}

} // namespace
