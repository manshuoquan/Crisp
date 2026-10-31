// Unit tests: crisp::ui paint value types (CtColor/CtPoint/CtSize/
// CtFont/CtPaintCmd, UI2 §3.1-§3.4).
//
// Covers: CtColor value semantics / RGBA / opaque; CtFont identity +
// equality; the record-style CtPaintCmd (u16string_view refs — no copy).
//
// Hand-written test doubles, no mock framework (project rule).

#include <string>

#include "ui/ct_paint_types.h"
#include "ui/ct_paint_cmd.h"
#include "ui/ct_geometry.h"

#include "test_harness.h"

using crisp::ui::CtColor;
using crisp::ui::CtFont;
using crisp::ui::CtPaintCmd;
using crisp::ui::CtPaintCmdType;
using crisp::ui::CtPaintMetrics;
using crisp::ui::CtPixelFormat;
using crisp::ui::CtPoint;
using crisp::ui::CtRect;
using crisp::ui::CtSize;

namespace {

TEST_CASE(paint_color_value_semantics)
{
    // Default color: opaque black (r=g=b=0, a=255).
    const CtColor c;
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
    EXPECT_TRUE(c.isOpaque());

    // Named colors (UI2 minimal set).
    EXPECT_EQ(CtColor::black().r, 0);
    EXPECT_EQ(CtColor::black().a, 255);
    EXPECT_TRUE(CtColor::black().isOpaque());
    EXPECT_EQ(CtColor::white().r, 255);
    EXPECT_EQ(CtColor::white().g, 255);
    EXPECT_EQ(CtColor::white().b, 255);
    EXPECT_TRUE(CtColor::white().isOpaque());

    // RGBA fields are independently settable; non-255 alpha => not opaque.
    const CtColor semi{CtColor{1, 2, 3, 128}};
    EXPECT_EQ(semi.r, 1);
    EXPECT_EQ(semi.g, 2);
    EXPECT_EQ(semi.b, 3);
    EXPECT_EQ(semi.a, 128);
    EXPECT_FALSE(semi.isOpaque());

    // Value semantics: copy carries the values independently.
    CtColor copy = c;
    copy.r = 9;
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(copy.r, 9);
}

TEST_CASE(paint_font_identity_and_metrics_capability)
{
    // CtFont is an identity value: family + pixelSize.
    CtFont defaultFont;
    EXPECT_EQ(defaultFont.pixelSize, 14u);
    EXPECT_TRUE(defaultFont.family.empty());

    CtFont a;
    a.pixelSize = 12;
    a.family = u"Segoe UI";
    CtFont b;
    b.pixelSize = 12;
    b.family = u"Segoe UI";
    CtFont c;
    c.pixelSize = 12;
    c.family = u"Arial";
    CtFont d;
    d.pixelSize = 16;
    d.family = u"Segoe UI";

    EXPECT_TRUE(a == b);          // same family + pixelSize
    EXPECT_FALSE(a == c);         // different family
    EXPECT_FALSE(a == d);         // different pixelSize

    // CtPaintMetrics is the capability RESULT (backend-owned, D7).
    CtPaintMetrics m;
    m.width = 0;
    m.height = 0;
    EXPECT_EQ(m.width, 0);
    EXPECT_EQ(m.height, 0);
}

TEST_CASE(paint_cmd_records_u16string_views_without_copy)
{
    // The record-style command stores family + text as u16string_view
    // REFS into caller-owned storage (MUST-1): no copy, no ownership.
    const std::u16string text = u"Hello";
    const std::u16string family = u"Segoe UI";

    CtPaintCmd cmd;
    cmd.type = CtPaintCmdType::kDrawText;
    cmd.origin = CtPoint{5, 7};
    cmd.color = CtColor::black();
    cmd.fontPixelSize = 12;
    cmd.family = family;
    cmd.text = text;

    // The views alias the caller's strings (identity, not content copy).
    EXPECT_EQ(cmd.text.data(), text.data());
    EXPECT_EQ(cmd.text.size(), 5u);
    EXPECT_EQ(cmd.family.data(), family.data());
    EXPECT_EQ(cmd.family.size(), 8u);

    // kFillRect commands carry a device-coord rect (D3).
    CtPaintCmd fill;
    fill.type = CtPaintCmdType::kFillRect;
    fill.rect = CtRect{1, 2, 30, 40};
    EXPECT_EQ(fill.rect.x, 1);
    EXPECT_EQ(fill.rect.width, 30);
}

TEST_CASE(paint_point_size_and_format_are_value_types)
{
    // CtPoint / CtSize are plain value structs with sane defaults.
    const CtPoint p;
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
    const CtSize s;
    EXPECT_EQ(s.width, 0);
    EXPECT_EQ(s.height, 0);
    EXPECT_TRUE(s.isEmpty());
    EXPECT_FALSE((CtSize{10, 10}).isEmpty());

    // CtPixelFormat: exactly ONE format in UI2 (D5).
    EXPECT_EQ(static_cast<int>(CtPixelFormat::kRgba8888), 0);
}

} // namespace (anonymous)
