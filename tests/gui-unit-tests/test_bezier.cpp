// The Bézier engine on an open path, which no caller had before the engine: the closed one is the Curve shape's,
// tested through it in test_property_groups.cpp.

#include <gtest/gtest.h>

#include "bezier.hpp"

namespace {

//! An open path's rules: two nodes at least, no segment after the last.
constexpr Bezier::Rules k_open{2, false};
//! A box of 300 × 100 px: fractions and pixels differ, so a mix-up shows.
const QRectF k_box(10.0, 20.0, 300.0, 100.0);
//! How close two computed fractions or pixels must be to count as the same.
constexpr qreal k_tolerance = 1e-9;

//! Three nodes along the box's middle line, straight.
QList<ShapeNode> line()
{
    return {ShapeNode(0.0, 0.5), ShapeNode(0.5, 0.5), ShapeNode(1.0, 0.5)};
}

bool near(const QPointF& a, const QPointF& b)
{
    return std::abs(a.x() - b.x()) <= k_tolerance && std::abs(a.y() - b.y()) <= k_tolerance;
}

int count(const QList<Bezier::Grip>& grips, Bezier::Part part)
{
    return int(std::count_if(grips.cbegin(), grips.cend(), [part](const Bezier::Grip& g) { return g.part == part; }));
}

} // namespace

TEST(Bezier, AnOpenPathHasNoSegmentAfterItsLastNode)
{
    const QList<ShapeNode> nodes = line();
    EXPECT_EQ(Bezier::segments(nodes, k_open), 2);
    const QPainterPath p = Bezier::path(nodes, k_open, k_box);
    ASSERT_EQ(p.elementCount(), 3) << "a move and two lines, no way back";
    EXPECT_TRUE(near(p.currentPosition(), Bezier::inBox(nodes.last().at, k_box))) << "it ends at its last node";

    const QList<Bezier::Grip> grips = Bezier::grips(nodes, k_open, k_box, -1);
    EXPECT_EQ(count(grips, Bezier::Part::AddNode), 2);
    EXPECT_EQ(Bezier::withNodeAdded(nodes, k_open, 2), nodes) << "no segment 2 to split";
    EXPECT_EQ(Bezier::withNodeAdded(nodes, k_open, 1).size(), 4);
}

TEST(Bezier, AnOpenPathKeepsTwoNodesAtLeast)
{
    const QList<ShapeNode> two = Bezier::withNodeRemoved(line(), k_open, 1);
    ASSERT_EQ(two.size(), 2);
    EXPECT_EQ(Bezier::withNodeRemoved(two, k_open, 0), two) << "the rules refuse a path of one";
}

TEST(Bezier, AnOpenPathsEndsHaveNoHandleOutsideIt)
{
    const QList<ShapeNode> nodes = line();
    const int              last  = int(nodes.size()) - 1;

    const QList<Bezier::Grip> first = Bezier::grips(nodes, k_open, k_box, 0);
    EXPECT_EQ(count(first, Bezier::Part::InHandle), 0);
    EXPECT_EQ(count(first, Bezier::Part::OutHandle), 1);
    const QList<Bezier::Grip> end = Bezier::grips(nodes, k_open, k_box, last);
    EXPECT_EQ(count(end, Bezier::Part::InHandle), 1);
    EXPECT_EQ(count(end, Bezier::Part::OutHandle), 0);

    // Grown and made smooth, an end gets its inner handle only, a third of the way to its neighbour.
    const QList<ShapeNode> grown = Bezier::withHandlesGrown(nodes, k_open, 0);
    EXPECT_TRUE(grown.first().in.isNull());
    EXPECT_TRUE(near(grown.first().out, (nodes[1].at - nodes[0].at) / 3.0));
    const ShapeNode smooth = Bezier::withNodeType(nodes, k_open, k_box, last, ShapeNode::Type::Smooth).last();
    EXPECT_TRUE(near(smooth.in, (nodes[last - 1].at - nodes[last].at) / 3.0));
    EXPECT_TRUE(smooth.out.isNull()) << "nothing past the end to measure it by";
}

TEST(Bezier, AClosedPathWrapsRound)
{
    // The same nodes closed: a segment back to the first, and handles on both sides of every node.
    const Bezier::Rules    closed{3, true};
    const QList<ShapeNode> nodes = line();
    EXPECT_EQ(Bezier::segments(nodes, closed), 3);
    EXPECT_EQ(count(Bezier::grips(nodes, closed, k_box, 0), Bezier::Part::InHandle), 1);
    EXPECT_EQ(Bezier::withNodeRemoved(nodes, closed, 0), nodes) << "three is the closed rules' fewest";
}
