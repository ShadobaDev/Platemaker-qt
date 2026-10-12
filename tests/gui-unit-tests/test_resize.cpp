// The resize maths behind the strip editor's eight grips: what moves, what stays, and when a drag is
// refused. A pure function, so it is tested without a scene.

#include <gtest/gtest.h>

#include "objects/resize.hpp"

using StripEdit::Grip;
using StripEdit::ResizeRule;
using StripEdit::resizedRect;

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

TEST(Resize, KeepAspectFollowsTheAxisPulledFurther)
{
    ResizeRule rule;
    rule.keepAspect = true;
    // Width pulled +50 % against height +12.5 %: width decides, height follows.
    EXPECT_EQ(resize(Grip::BottomRight, {40, 5}, rule), QRectF(100, 200, 120, 60));
    // Height pulled +100 % against width +12.5 %: height decides.
    EXPECT_EQ(resize(Grip::BottomRight, {10, 40}, rule), QRectF(100, 200, 160, 80));
    // The top-left corner still anchors the bottom-right one.
    EXPECT_EQ(resize(Grip::TopLeft, {-40, 0}, rule), QRectF(60, 180, 120, 60));
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
    const QRectF r = resize(Grip::BottomRight, {20, 0}, rule);
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
