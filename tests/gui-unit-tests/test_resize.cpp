// The resize maths behind the strip editor's eight grips: what moves, what stays, and when a drag is
// refused. A pure function, so it is tested without a scene.

#include <gtest/gtest.h>

#include "objects/resize.hpp"

using StripEdit::Grip;
using StripEdit::ResizeRule;
using StripEdit::resizedRect;
using StripEdit::resizeRule;

namespace {

const QRectF k_start(100, 200, 80, 40);

QRectF resize(Grip g, QPointF d, ResizeRule rule = {})
{
    const auto r = resizedRect(k_start, g, d, rule);
    EXPECT_TRUE(r.has_value());
    return r.value_or(QRectF());
}

} // namespace

TEST(Resize, EachCornerAnchorsTheOppositeOne)
{
    EXPECT_EQ(resize(Grip::BottomRight, {10, 5}), QRectF(100, 200, 90, 45));
    EXPECT_EQ(resize(Grip::TopLeft,     {10, 5}), QRectF(110, 205, 70, 35));
    EXPECT_EQ(resize(Grip::TopRight,    {10, 5}), QRectF(100, 205, 90, 35));
    EXPECT_EQ(resize(Grip::BottomLeft,  {10, 5}), QRectF(110, 200, 70, 45));
}

TEST(Resize, EachEdgeMovesOnlyItselfAndIgnoresTheOtherAxis)
{
    EXPECT_EQ(resize(Grip::Right,  {10, 99}), QRectF(100, 200, 90, 40));
    EXPECT_EQ(resize(Grip::Left,   {10, 99}), QRectF(110, 200, 70, 40));
    EXPECT_EQ(resize(Grip::Bottom, {99, 5}),  QRectF(100, 200, 80, 45));
    EXPECT_EQ(resize(Grip::Top,    {99, 5}),  QRectF(100, 205, 80, 35));
}

TEST(Resize, FromTheCentreKeepsTheCentre)
{
    ResizeRule rule;
    rule.fromCentre = true;
    const QRectF corner = resize(Grip::BottomRight, {10, 5}, rule);
    EXPECT_EQ(corner, QRectF(90, 195, 100, 50));
    EXPECT_EQ(corner.center(), k_start.center());

    const QRectF edge = resize(Grip::Left, {-10, 0}, rule);
    EXPECT_EQ(edge, QRectF(90, 200, 100, 40));
}

TEST(Resize, KeepAspectTakesTheDiagonalPointNearestThePointer)
{
    ResizeRule rule;
    rule.keepAspect = true;
    // On the diagonal: exactly where the pointer is.
    EXPECT_EQ(resize(Grip::BottomRight, {40, 20}, rule), QRectF(100, 200, 120, 60));
    // Off it, by (40 − 20 · 2) / 5: the projection of (120, 40) on (80, 40) is 1.4 of the box.
    EXPECT_EQ(resize(Grip::BottomRight, {40, 0}, rule), QRectF(100, 200, 112, 56));
    // The top-left corner still anchors the bottom-right one.
    EXPECT_EQ(resize(Grip::TopLeft, {-40, -20}, rule), QRectF(60, 180, 120, 60));
}

TEST(Resize, KeepAspectFollowsThePointerWithoutJumps)
{
    // A tall picture (the reported one, at 533 %): a pixel sideways against several down used to flip the
    // box from growing by 0.8 % to shrinking by 1.4 %. Now each pixel of travel moves it by a fraction of one.
    constexpr qreal k_onePixel = 1.0;
    const QRectF    tall(371.453, 10238, 282.583, 1066.35);
    ResizeRule      rule;
    rule.keepAspect = true;
    QRectF last     = tall;
    for (int x = 0; x >= -4; --x) {
        for (int y = 0; y <= 9; ++y) {
            const auto r = resizedRect(tall, Grip::BottomRight, QPointF(x, y), rule);
            ASSERT_TRUE(r.has_value());
            if (y > 0)
                EXPECT_LE(qAbs(r->height() - last.height()), k_onePixel) << x << "," << y;
            last = *r;
        }
    }
}

TEST(Resize, KeepAspectOnAnEdgeCentresTheOtherAxis)
{
    ResizeRule rule;
    rule.keepAspect = true;
    EXPECT_EQ(resize(Grip::Right, {40, 0}, rule), QRectF(100, 190, 120, 60));
    EXPECT_EQ(resize(Grip::Bottom, {0, 40}, rule), QRectF(60, 200, 160, 80));
}

TEST(Resize, BothModifiersTogether)
{
    ResizeRule rule;
    rule.keepAspect = true;
    rule.fromCentre = true;
    const QRectF r = resize(Grip::BottomRight, {20, 10}, rule);
    EXPECT_EQ(r, QRectF(80, 190, 120, 60));
    EXPECT_EQ(r.center(), k_start.center());
}

TEST(Resize, RefusesBelowTheMinimumRatherThanTurningInsideOut)
{
    ResizeRule rule;
    rule.minSize = 40;
    EXPECT_TRUE(resizedRect(k_start, Grip::Bottom, {0, 0}, rule).has_value());   // exactly the minimum
    EXPECT_FALSE(resizedRect(k_start, Grip::Bottom, {0, -1}, rule).has_value());
    EXPECT_FALSE(resizedRect(k_start, Grip::Right, {-200, 0}, rule).has_value());   // past the anchor
}

TEST(Resize, APictureShrinksToTheFloorOnScreenWhateverItsScale)
{
    // A 53 × 200 picture drawn at 445 %: its floor is 40 scene pixels, not 40 of its own pixels times its scale
    // (which stopped it at 336 %).
    constexpr qreal k_scale     = 4.45;
    constexpr qreal k_tolerance = 1e-9;
    const QRectF    drawn(0, 0, 53 * k_scale, 200 * k_scale);
    const auto      shrunk = resizedRect(drawn, Grip::BottomRight, {-190, -720}, resizeRule(k_scale, true, false, false));
    ASSERT_TRUE(shrunk.has_value());
    EXPECT_LT(shrunk->width(), 40 * k_scale);
    EXPECT_NEAR(shrunk->height() / shrunk->width(), drawn.height() / drawn.width(), k_tolerance);
    EXPECT_FALSE(resizedRect(drawn, Grip::BottomRight, {-200, -760}, resizeRule(k_scale, true, false, false)));

    // A box in its own units keeps a floor of 40 of them, snapped to one.
    const ResizeRule box = resizeRule(2.0, false, false, false);
    EXPECT_NEAR(box.minSize, 80.0, k_tolerance);
    EXPECT_NEAR(box.snap, 2.0, k_tolerance);
    EXPECT_FALSE(box.keepAspect);
}

TEST(Resize, SnappingKeepsTheAnchoredSideExactlyInPlace)
{
    ResizeRule rule;
    rule.snap = 2.0;   // an item drawn at twice its size: one item unit is two scene units
    const QRectF r = resize(Grip::TopLeft, {-3.3, -0.7}, rule);
    EXPECT_EQ(r.size(), QSizeF(84, 40));
    EXPECT_EQ(r.bottomRight(), k_start.bottomRight());
}

TEST(Resize, AnythingButAGripLeavesTheBoxAlone)
{
    EXPECT_EQ(resize(Grip::Body,   {10, 10}), k_start);
    EXPECT_EQ(resize(Grip::Handle, {10, 10}), k_start);
}
