// Unit tests: crisp::ui widget trace (CtWidgetTrace, UI1 §2.7 / D3).
//
// Hand-written test doubles, no mock framework (project rule).

#include <chrono>
#include <vector>

#include "ui/ct_widget_trace.h"

#include "test_harness.h"

using crisp::ui::CtWidgetPhase;
using crisp::ui::CtWidgetRecord;
using crisp::ui::CtWidgetTrace;

namespace {

TEST_CASE(widget_trace_records_and_copy_out_in_order)
{
    CtWidgetTrace trace;
    EXPECT_EQ(trace.size(), 0u);

    CtWidgetRecord r1;
    r1.phase = CtWidgetPhase::kCreated;
    r1.objectId = 42;
    r1.when = std::chrono::steady_clock::now();
    CtWidgetRecord r2;
    r2.phase = CtWidgetPhase::kDamage;
    r2.objectId = 43; // distinct id: order assertion is meaningful
    r2.damageRectCount = 3;
    r2.when = std::chrono::steady_clock::now();

    EXPECT_TRUE(trace.record(r1));
    EXPECT_TRUE(trace.record(r2));
    EXPECT_EQ(trace.size(), 2u);

    CtWidgetRecord out[4];
    const std::size_t n = trace.copyOut(out, 4);
    EXPECT_EQ(n, 2u);
    // Order preserved (oldest first): r1 (42) then r2 (43).
    EXPECT_EQ(static_cast<int>(out[0].phase), static_cast<int>(CtWidgetPhase::kCreated));
    EXPECT_EQ(out[0].objectId, 42u);
    EXPECT_EQ(out[1].objectId, 43u);
    EXPECT_EQ(out[1].damageRectCount, 3u);
}

TEST_CASE(widget_trace_copy_out_respects_capacity)
{
    CtWidgetTrace trace;
    CtWidgetRecord r;
    r.phase = CtWidgetPhase::kDamage;
    r.objectId = 7;
    trace.record(r);

    CtWidgetRecord out[1];
    const std::size_t n = trace.copyOut(out, 1);
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(out[0].objectId, 7u);
}

TEST_CASE(widget_trace_drops_oldest_on_overflow)
{
    CtWidgetTrace trace;
    // Fill the ring to capacity.
    for (std::size_t i = 0; i < CtWidgetTrace::kCapacity; ++i) {
        CtWidgetRecord r;
        r.phase = CtWidgetPhase::kCreated;
        r.objectId = static_cast<std::uint64_t>(i);
        EXPECT_TRUE(trace.record(r));
    }
    EXPECT_EQ(trace.size(), CtWidgetTrace::kCapacity);

    // One more record: oldest dropped, new one retained.
    CtWidgetRecord r;
    r.phase = CtWidgetPhase::kCreated;
    r.objectId = 9999;
    EXPECT_FALSE(trace.record(r)); // overflow: dropped one
    EXPECT_EQ(trace.size(), CtWidgetTrace::kCapacity);

    std::vector<CtWidgetRecord> out(CtWidgetTrace::kCapacity);
    const std::size_t n = trace.copyOut(out.data(), out.size());
    EXPECT_EQ(n, CtWidgetTrace::kCapacity);
    // The first (oldest) entry is now 1 (0 was dropped).
    EXPECT_EQ(out.front().objectId, 1u);
    // The newest entry is the overflow record.
    EXPECT_EQ(out.back().objectId, 9999u);
}

} // namespace (anonymous)
