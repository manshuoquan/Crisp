// Unit tests: crisp::ui geometry primitives (CtRect / CtRegion, UI1 §2.1/§2.2).
//
// Hand-written test doubles, no mock framework (project rule).

#include "ui/ct_geometry.h"

#include "test_harness.h"

using crisp::ui::CtRect;
using crisp::ui::CtRegion;

namespace {

TEST_CASE(rect_half_open_bounds_are_exclusive)
{
    const CtRect r{10, 20, 100, 50};
    EXPECT_EQ(r.right(), 110);
    EXPECT_EQ(r.bottom(), 70);
    // Right/bottom edges are EXCLUSIVE (half-open [x, right())).
    EXPECT_TRUE(r.contains(10, 20));
    EXPECT_TRUE(r.contains(109, 69));
    EXPECT_FALSE(r.contains(110, 70));
    EXPECT_FALSE(r.contains(9, 20));
    EXPECT_FALSE(r.contains(10, 19));
}

TEST_CASE(rect_isEmpty_normalizes_non_positive_size)
{
    const CtRect zero{5, 5, 0, 10};
    const CtRect neg{5, 5, 10, -3};
    const CtRect one{5, 5, 1, 1};
    EXPECT_TRUE(CtRect{}.isEmpty());
    EXPECT_TRUE(zero.isEmpty());
    EXPECT_TRUE(neg.isEmpty());
    EXPECT_FALSE(one.isEmpty());
}

TEST_CASE(rect_intersects_half_open_math)
{
    const CtRect a{0, 0, 10, 10};
    EXPECT_TRUE(a.intersects(CtRect{9, 9, 5, 5}));  // touches at (9,9)
    EXPECT_FALSE(a.intersects(CtRect{10, 0, 5, 5})); // right edge exclusive
    EXPECT_FALSE(a.intersects(CtRect{0, 10, 5, 5})); // bottom edge exclusive
    EXPECT_FALSE(a.intersects(CtRect{}));            // empty never intersects
    // Empty rect with coordinates INSIDE the other must still not intersect
    // (width/height <= 0 short-circuit), incl. negative-coordinate operand.
    const CtRect emptyInside{5, 5, 0, 10};
    const CtRect emptyNeg{-5, 0, 10, 10};
    const CtRect otherEmpty{0, 0, 0, 10};
    EXPECT_FALSE(emptyInside.intersects(a));
    EXPECT_FALSE(emptyNeg.intersects(otherEmpty));
}

TEST_CASE(rect_intersect_returns_empty_when_disjoint)
{
    const CtRect a{0, 0, 10, 10};
    const CtRect b{20, 20, 5, 5};
    const CtRect inter = a.intersect(b);
    EXPECT_TRUE(inter.isEmpty());
    // Exact overlap returns the operand itself.
    const CtRect same = a.intersect(CtRect{2, 2, 4, 4});
    EXPECT_EQ(same.x, 2);
    EXPECT_EQ(same.y, 2);
    EXPECT_EQ(same.width, 4);
    EXPECT_EQ(same.height, 4);
}

TEST_CASE(rect_united_returns_bounding_box)
{
    const CtRect a{0, 0, 10, 10};
    const CtRect b{20, 30, 5, 5};
    const CtRect u = a.united(b);
    EXPECT_EQ(u.x, 0);
    EXPECT_EQ(u.y, 0);
    EXPECT_EQ(u.width, 25);  // right edge 25
    EXPECT_EQ(u.height, 35); // bottom edge 35
    // Empty operands pass the other through.
    EXPECT_EQ(CtRect{}.united(b).x, 20);
    EXPECT_EQ(a.united(CtRect{}).x, 0);
}

TEST_CASE(region_unions_into_single_bounding_rect)
{
    CtRegion region;
    EXPECT_TRUE(region.isEmpty());
    region.add(CtRect{0, 0, 10, 10});
    region.add(CtRect{20, 30, 5, 5});
    EXPECT_FALSE(region.isEmpty());
    EXPECT_EQ(region.bounds.x, 0);
    EXPECT_EQ(region.bounds.width, 25);
    EXPECT_EQ(region.bounds.height, 35);
    // Empty rect is a no-op on the region.
    const CtRect before = region.bounds;
    region.add(CtRect{});
    EXPECT_EQ(region.bounds.x, before.x);
    EXPECT_EQ(region.bounds.width, before.width);
    // Negative-size (empty) rect is also a no-op: bounds stay canonical.
    region.add(CtRect{5, 5, -3, -3});
    EXPECT_EQ(region.bounds.x, before.x);
    EXPECT_EQ(region.bounds.width, before.width);
    // clear() resets.
    region.clear();
    EXPECT_TRUE(region.isEmpty());
}

} // namespace (anonymous)
