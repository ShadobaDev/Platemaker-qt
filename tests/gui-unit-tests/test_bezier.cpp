// The Bézier engine on an open path, which no caller had before the engine (the closed one is the Curve shape's,
// tested through it in test_property_groups.cpp), and the fitting of a closed outline with as few nodes as keep it.

#include <gtest/gtest.h>

#include "bezier.hpp"

#include <QPainterPathStroker>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

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

namespace {

//! A closed outline's rules: three nodes at least.
constexpr Bezier::Rules k_closed{3, true};
//! How far a fit may stray, in pixels: half a pixel, as the brush asks at a zoom of one.
constexpr qreal k_fitTolerance = 0.5;
//! What the fit may stray past it between the points it is measured at.
constexpr qreal k_fitSlack = 0.1;
//! Points per segment the fitted path is sampled at.
constexpr int k_samples = 32;

//! The distance from @p p to the closed polygon @p poly's edges.
qreal distanceTo(const QPolygonF& poly, const QPointF& p)
{
    qreal best = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i < poly.size(); ++i) {
        const QPointF a = poly[i], b = poly[(i + 1) % poly.size()], ab = b - a;
        const qreal   l2 = QPointF::dotProduct(ab, ab);
        const qreal   t  = l2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / l2, 0.0, 1.0) : 0.0;
        best             = std::min(best, QLineF(p, a + ab * t).length());
    }
    return best;
}

//! The fit of @p outline, and whether it and the outline are within the tolerance of each other both ways.
void expectFits(const QPolygonF& outline, const QList<ShapeNode>& nodes)
{
    ASSERT_GE(nodes.size(), 3);
    const QPolygonF drawn = Bezier::polyline(nodes, k_closed, k_box, k_samples);
    for (const QPointF& p : drawn)
        ASSERT_LE(distanceTo(outline, p), k_fitTolerance + k_fitSlack) << p.x() << ", " << p.y();
    for (const QPointF& p : outline)
        ASSERT_LE(distanceTo(drawn, p), k_fitTolerance + k_fitSlack) << p.x() << ", " << p.y();
}

} // namespace

/**
 * \brief A closed polygon is fitted with as few nodes as keep within the tolerance: a circle's smooth all round, a
 *        rectangle's one at each corner with straight sides, a brush stroke's outline far fewer than its points.
 */
TEST(Bezier, AnOutlineIsFittedWithFewNodesWithinTheTolerance)
{
    constexpr int   k_circlePoints = 720;
    constexpr qreal k_radius       = 45.0;
    const QPointF   centre         = k_box.center();
    QPolygonF       circle;
    for (int i = 0; i < k_circlePoints; ++i) {
        const qreal a = 2.0 * M_PI * i / k_circlePoints;
        circle.append(centre + QPointF(std::cos(a), std::sin(a)) * k_radius);
    }
    const QList<ShapeNode> round = Bezier::fitted(circle, k_box, k_fitTolerance);
    {
        SCOPED_TRACE("a circle");
        expectFits(circle, round);
    }
    EXPECT_LE(round.size(), 8);
    for (const ShapeNode& n : round)
        EXPECT_NE(n.type, ShapeNode::Type::Corner);

    // A rectangle, a point every pixel along its sides: a corner node at each corner, nothing between.
    const QRectF rect(40.0, 40.0, 200.0, 60.0);
    QPolygonF    square;
    const QPointF corners[] = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    for (int c = 0; c < 4; ++c) {
        const QPointF a = corners[c], b = corners[(c + 1) % 4];
        const int     steps = int(QLineF(a, b).length());
        for (int k = 0; k < steps; ++k)
            square.append(a + (b - a) * (qreal(k) / steps));
    }
    const QList<ShapeNode> boxed = Bezier::fitted(square, k_box, k_fitTolerance);
    {
        SCOPED_TRACE("a rectangle");
        expectFits(square, boxed);
    }
    ASSERT_EQ(boxed.size(), 4);
    for (const ShapeNode& n : boxed) {
        EXPECT_EQ(n.type, ShapeNode::Type::Corner);
        const QPointF at = Bezier::inBox(n.at, k_box);
        EXPECT_TRUE(std::any_of(std::begin(corners), std::end(corners),
                                [&](const QPointF& c) { return QLineF(at, c).length() <= k_tolerance; }));
    }

    // A round brush's stroke along a zigzag, as Qt outlines it.
    QPainterPath track;
    track.moveTo(50.0, 60.0);
    for (const QPointF& p : {QPointF(90.0, 100.0), QPointF(130.0, 50.0), QPointF(170.0, 95.0), QPointF(260.0, 70.0)})
        track.lineTo(p);
    QPainterPathStroker brush;
    brush.setWidth(18.0);
    brush.setCapStyle(Qt::RoundCap);
    brush.setJoinStyle(Qt::RoundJoin);
    const QList<QPolygonF> parts = brush.createStroke(track).simplified().toFillPolygons();
    ASSERT_EQ(parts.size(), 1);
    QPolygonF stroke = parts.first();
    stroke.removeLast();   // toFillPolygons() closes it by repeating the first point
    const QList<ShapeNode> painted = Bezier::fitted(stroke, k_box, k_fitTolerance);
    {
        SCOPED_TRACE("a stroke");
        expectFits(stroke, painted);
    }
    EXPECT_LT(painted.size(), stroke.size() / 3) << painted.size() << " nodes of " << stroke.size() << " points";

    EXPECT_TRUE(Bezier::fitted(QPolygonF({QPointF(0, 0), QPointF(1, 1)}), k_box, k_fitTolerance).isEmpty());
    EXPECT_TRUE(Bezier::fitted(circle, QRectF(), k_fitTolerance).isEmpty());
}
