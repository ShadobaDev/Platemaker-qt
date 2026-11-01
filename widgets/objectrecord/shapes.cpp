#include "shapes.hpp"
#include "edgewalk.hpp"

#include <QByteArray>
#include <QObject>
#include <QRandomGenerator>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace Geometry {

namespace {

// ---------------------------------------------------------------------------
// The shapes' own numbers, beside the shapes that use them
// ---------------------------------------------------------------------------

/**
 *  Points on a "shout" burst — 12 spikes reads as a shout without turning into a sunburst.
 */
constexpr int   k_burstSpikes    = 12;
/**
 *  Inner radius of the burst, as a fraction of the outer one.
 */
constexpr qreal k_burstInner     = 0.74;
/**
 *  1/sqrt(2) — the half-axis fraction at which a rectangle inscribes an ellipse.
 */
constexpr qreal k_invSqrt2       = 0.70710678118654752;
/**
 *  Below this tan(θ/2) a polygon's corner folds back on itself, and no arc fits in it.
 */
constexpr qreal k_hairpinCorner  = 1e-6;
/**
 *  Fraction of the width the trapezoid's top edge loses on each side.
 */
constexpr qreal k_trapezoidSlant = 0.16;
/**
 *  A lobe never smaller than this, in balloon pixels, so a tiny balloon still has an outline.
 */
constexpr qreal k_minLobePx      = 2.0;
/**
 *  A lobe's arc longer than this, in turns, is one its neighbours cover (the angle wrapped round): it is
 *  left out.
 */
constexpr qreal k_coveredArc     = 0.95;
/**
 *  How far in from the edge a Cloud's inner ellipse is, in lobe radii: a little short of the lobes'
 *  centres, so it is covered wherever two lobes meet and fills the gap wherever they do not.
 */
constexpr qreal k_innerReach     = 0.95;
//! The inner ellipse's smallest radius, in balloon pixels.
constexpr qreal k_minInnerPx     = 1.0;
//! Halvings that find where a lobe crosses the inner ellipse: well under a thousandth of a pixel.
constexpr int   k_crossingSteps  = 40;
/**
 *  Depth of a banner's end notch, as a fraction of its width.
 */
constexpr qreal k_bannerNotch    = 0.12;
/**
 *  Width of a scroll's rolled end, as a fraction of the balloon's width.
 */
constexpr qreal k_scrollRoll     = 0.10;
/**
 *  How far a scroll's long edges bow inward, as a fraction of its height: where a quadratic's middle
 *  handle sits, so the edge itself dips half of it.
 */
constexpr qreal k_scrollBow      = 0.06;
/**
 *  A quarter of an ellipse as one cubic: each handle is this fraction of its radius long.
 */
constexpr qreal k_quarterArc     = 0.5522847498307936;
/**
 *  How far from one line, and from one length, a node's two handles may be and still read as Smooth or
 *  Symmetric when an outline is converted to a Curve: as a fraction (a sine, a ratio of lengths).
 */
constexpr qreal k_smoothTolerance = 1e-3;
/**
 *  A handle drawn for a node that has none, when it is made Smooth: this fraction of the way to its
 *  neighbour, as a straight segment's handles would sit.
 */
constexpr qreal k_newHandle      = 1.0 / 3.0;
/**
 *  Samples across each side of the grid a free outline's text area is found on (largestInscribedRect()).
 *  64 puts the rectangle within a sixty-fourth of the side of the true largest, well inside the padding.
 */
constexpr int   k_areaSamples    = 64;
/**
 *  Halvings that push each side of that rectangle back out to the outline: a cell to a thousandth.
 */
constexpr int   k_growSteps      = 10;
/**
 *  A caption box's corner radius, in balloon pixels. The speech balloon's is the struct's default
 *  (ShapeProperties::cornerRadius): 0.28 of the default 160 px height, as it was before it was a number.
 */
constexpr qreal k_captionCorner  = 4.0;

//! The largest axis-aligned rectangle inside the ellipse inscribed in \p r.
QRectF inscribedInEllipse(const ShapeProperties&, const QRectF& r)
{
    const qreal kx = r.width()  * (1.0 - k_invSqrt2) / 2.0;
    const qreal ky = r.height() * (1.0 - k_invSqrt2) / 2.0;
    return r.adjusted(kx, ky, -kx, -ky);
}

//! The whole rectangle: the lettering may use all of it.
QRectF wholeArea(const ShapeProperties&, const QRectF& body)
{
    return body;
}

// ---------------------------------------------------------------------------
// Box, Ellipse
// ---------------------------------------------------------------------------

//! The corner radius a Box in @p r actually gets: never negative, never past half the shorter side.
qreal boxRadius(const ShapeProperties& shape, const QRectF& r)
{
    return qBound(0.0, shape.cornerRadius, cornerRadiusLimit(r.size()));
}

QPainterPath boxPath(const ShapeProperties& shape, const QRectF& r)
{
    const qreal radius = boxRadius(shape, r);
    QPainterPath p;
    p.addRoundedRect(r, radius, radius);
    return p;
}

QRectF boxArea(const ShapeProperties& shape, const QRectF& body)
{
    // In from every side by where a corner's arc crosses its diagonal: the rectangle's corners then sit
    // on the arcs, so no letter in a corner pokes past the outline.
    const qreal in = boxRadius(shape, body) * (1.0 - k_invSqrt2);
    return body.adjusted(in, in, -in, -in);
}

QPainterPath ellipsePath(const ShapeProperties&, const QRectF& r)
{
    QPainterPath p;
    p.addEllipse(r);
    return p;
}

// ---------------------------------------------------------------------------
// Polygon
// ---------------------------------------------------------------------------

//! A Polygon's or a Curve's nodes: its own, or the rectangle's corners when it has fewer than three.
const QList<ShapeNode>& freeNodes(const ShapeProperties& shape)
{
    static const QList<ShapeNode> k_rectangle{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    return shape.nodes.size() >= 3 ? shape.nodes : k_rectangle;
}

//! A point relative to @p r (a node, or a node plus a handle) in @p r's own coordinates.
QPointF inBody(const QPointF& p, const QRectF& r)
{
    return {r.left() + p.x() * r.width(), r.top() + p.y() * r.height()};
}

//! inBody()'s inverse: a point in @p r's coordinates, relative to it.
QPointF relativeTo(const QPointF& p, const QRectF& r)
{
    return {(p.x() - r.left()) / r.width(), (p.y() - r.top()) / r.height()};
}

//! A handle (an offset, relative to @p r) in @p r's pixels, where lengths and angles mean what they look.
QPointF toPixels(const QPointF& offset, const QRectF& r)
{
    return {offset.x() * r.width(), offset.y() * r.height()};
}

//! toPixels()'s inverse.
QPointF fromPixels(const QPointF& offset, const QRectF& r)
{
    return {offset.x() / r.width(), offset.y() / r.height()};
}

qreal length(const QPointF& v)
{
    return std::hypot(v.x(), v.y());
}

//! A Polygon's vertices in @p r.
QList<QPointF> polygonPoints(const ShapeProperties& shape, const QRectF& r)
{
    const QList<ShapeNode>& nodes = freeNodes(shape);
    QList<QPointF>          out;
    out.reserve(nodes.size());
    for (const ShapeNode& n : nodes)
        out.append(inBody(n.at, r));
    return out;
}

/**
 * @brief The nodes joined by straight edges, each corner rounded by an arc of `cornerRadius`.
 *
 * A corner's arc is tangent to both its edges, and never reaches past the middle of either, so two
 * neighbouring corners cannot overlap; a sharp corner therefore rounds less than a wide one. The arc is
 * one cubic. Self-intersecting nodes are allowed, and filled by the non-zero rule.
 */
QPainterPath polygonPath(const ShapeProperties& shape, const QRectF& r)
{
    const QList<QPointF> pts    = polygonPoints(shape, r);
    const qreal          radius = qMax(0.0, shape.cornerRadius);
    const int            n      = int(pts.size());

    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    for (int i = 0; i < n; ++i) {
        const QPointF v  = pts[i];
        const QPointF a  = pts[(i + n - 1) % n] - v;   // towards the previous vertex
        const QPointF b  = pts[(i + 1) % n] - v;       // towards the next one
        const qreal   la = std::hypot(a.x(), a.y());
        const qreal   lb = std::hypot(b.x(), b.y());

        // How far the arc's ends sit from the vertex: r / tan(θ/2) for a corner of angle θ, no further
        // than half the shorter edge. Nothing to round at a zero-length edge or a hairpin.
        qreal tanHalf = 0.0;
        qreal reach   = 0.0;
        if (radius > 0.0 && la > 0.0 && lb > 0.0) {
            const qreal cosine = qBound(-1.0, (a.x() * b.x() + a.y() * b.y()) / (la * lb), 1.0);
            tanHalf            = std::tan(std::acos(cosine) / 2.0);
            if (tanHalf > k_hairpinCorner)
                reach = qMin(radius / tanHalf, qMin(la, lb) / 2.0);
        }
        if (reach <= 0.0) {
            if (i == 0) p.moveTo(v); else p.lineTo(v);
            continue;
        }

        const QPointF ua   = a / la;
        const QPointF ub   = b / lb;
        const QPointF from = v + ua * reach;
        const QPointF to   = v + ub * reach;
        // A circular arc of sweep φ as one cubic: handles 4/3·tan(φ/4)·r long, along the edges.
        const qreal arcRadius = reach * tanHalf;
        const qreal sweep     = M_PI - 2.0 * std::atan(tanHalf);
        const qreal handle    = 4.0 / 3.0 * std::tan(sweep / 4.0) * arcRadius;
        if (i == 0) p.moveTo(from); else p.lineTo(from);
        p.cubicTo(from - ua * handle, to - ub * handle, to);
    }
    p.closeSubpath();
    return p;
}

/**
 * @brief The largest axis-aligned rectangle inside @p outline, found on a grid of samples over @p body.
 *
 * For an outline with no formula of its own: each row of samples is filled between the outline's
 * crossings (by its own fill rule), and the largest rectangle of inside samples wins, the one nearest the
 * middle among equals; then each side is pushed out to the outline. It stays inside but for a notch
 * narrower than a cell. The grid is relative to the body, so an outline that scales with the box gives a
 * rectangle that scales with it too, which is what the box fit's search relies on. It returns an area
 * and places nothing.
 *
 * ponytail: a grid, not the exact polygon answer, so a rectangle of another proportion that is a little
 * larger can be missed by under a cell; the exact one is a polygon algorithm, if that ever shows.
 */
QRectF largestInscribedRect(const QPainterPath& outline, const QRectF& body)
{
    constexpr int n  = k_areaSamples;
    const qreal   sx = body.width() / n;
    const qreal   sy = body.height() / n;
    const auto    at = [&](int col, int row) {
        return QPointF(body.left() + (col + 0.5) * sx, body.top() + (row + 0.5) * sy);
    };

    // Which samples are inside: each row against the outline's edges.
    const QList<QPolygonF> rings   = outline.toSubpathPolygons();
    const bool             winding = outline.fillRule() == Qt::WindingFill;
    std::array<std::array<bool, n>, n> inside{};
    std::vector<std::pair<qreal, int>> crossings;
    for (int row = 0; row < n; ++row) {
        const qreal y = at(0, row).y();
        crossings.clear();
        for (const QPolygonF& ring : rings) {
            for (qsizetype k = 0; k < ring.size(); ++k) {
                const QPointF e0 = ring[k];
                const QPointF e1 = ring[(k + 1) % ring.size()];
                if ((e0.y() <= y) == (e1.y() <= y))
                    continue;
                const qreal x = e0.x() + (y - e0.y()) * (e1.x() - e0.x()) / (e1.y() - e0.y());
                crossings.push_back({x, e1.y() > e0.y() ? 1 : -1});
            }
        }
        std::sort(crossings.begin(), crossings.end());
        size_t next  = 0;
        int    count = 0;
        for (int col = 0; col < n; ++col) {
            const qreal x = at(col, row).x();
            for (; next < crossings.size() && crossings[next].first < x; ++next)
                count += winding ? crossings[next].second : 1;
            inside[row][col] = winding ? count != 0 : (count % 2) != 0;
        }
    }

    // The largest rectangle of inside samples: each row is the floor of a histogram of the columns'
    // runs of inside samples above it, and a histogram's largest rectangle is one pass with a stack.
    std::array<int, n> run{};
    int                bestArea = 0;
    qreal              bestOff  = 0.0;
    QRect              best;
    const qreal        middle   = (n - 1) / 2.0;
    std::vector<int>   stack;
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col)
            run[col] = inside[row][col] ? run[col] + 1 : 0;
        stack.clear();
        for (int col = 0; col <= n; ++col) {
            const int h = col < n ? run[col] : 0;
            while (!stack.empty() && run[stack.back()] >= h) {
                const int height = run[stack.back()];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const int area = height * (col - left);
                if (area == 0 || area < bestArea)
                    continue;
                // Among equals, the one whose middle is nearest the balloon's.
                const qreal cx  = (left + col - 1) / 2.0 - middle;
                const qreal cy  = (2 * row - height + 1) / 2.0 - middle;
                const qreal off = std::hypot(cx, cy);
                if (area > bestArea || off < bestOff) {
                    bestArea = area;
                    bestOff  = off;
                    best     = QRect(left, row - height + 1, col - left, height);
                }
            }
            stack.push_back(col);
        }
    }
    if (bestArea == 0)
        return QRectF(body.center(), QSizeF(0, 0));

    // The grid's rectangle runs between sample centres, so it stops up to a cell short of the outline
    // on every side. Each side is pushed back out, as far as the outline lets it, within that cell.
    // Asked of the even-odd copy: for a non-zero path, QPainterPath::contains(QRectF) answers a
    // rectangle that crosses the outline by sampling a few points, which let a side through a notch.
    QPainterPath strict = outline;
    strict.setFillRule(Qt::OddEvenFill);
    QRectF rect(at(best.left(), best.top()), at(best.right(), best.bottom()));
    const auto grow = [&](auto moveSide, qreal cell) {
        qreal fits = 0.0;
        qreal past = cell;
        for (int i = 0; i < k_growSteps; ++i) {
            const qreal mid   = (fits + past) / 2.0;
            QRectF      trial = rect;
            moveSide(trial, mid);
            (strict.contains(trial) ? fits : past) = mid;
        }
        moveSide(rect, fits);
    };
    grow([](QRectF& r, qreal d) { r.setLeft(r.left() - d); }, sx);
    grow([](QRectF& r, qreal d) { r.setRight(r.right() + d); }, sx);
    grow([](QRectF& r, qreal d) { r.setTop(r.top() - d); }, sy);
    grow([](QRectF& r, qreal d) { r.setBottom(r.bottom() + d); }, sy);
    return rect;
}

QRectF polygonArea(const ShapeProperties& shape, const QRectF& body)
{
    return largestInscribedRect(polygonPath(shape, body), body);
}

// ---------------------------------------------------------------------------
// Curve
// ---------------------------------------------------------------------------

/**
 * @brief The nodes joined by cubic segments, each leaving a node along its out-handle and arriving along
 *        the next one's in-handle; straight where neither has one. Filled by the non-zero rule, as a
 *        Polygon is.
 */
QPainterPath curvePath(const ShapeProperties& shape, const QRectF& r)
{
    const QList<ShapeNode>& nodes = freeNodes(shape);
    const qsizetype         n     = nodes.size();
    QPainterPath            p;
    p.setFillRule(Qt::WindingFill);
    p.moveTo(inBody(nodes.first().at, r));
    for (qsizetype i = 0; i < n; ++i) {
        const ShapeNode& a = nodes[i];
        const ShapeNode& b = nodes[(i + 1) % n];
        if (a.out.isNull() && b.in.isNull())
            p.lineTo(inBody(b.at, r));
        else
            p.cubicTo(inBody(a.at + a.out, r), inBody(b.at + b.in, r), inBody(b.at, r));
    }
    p.closeSubpath();
    return p;
}

QRectF curveArea(const ShapeProperties& shape, const QRectF& body)
{
    return largestInscribedRect(curvePath(shape, body), body);
}

// ---------------------------------------------------------------------------
// Edit-mode handles
// ---------------------------------------------------------------------------

/**
 * @brief One corner, as far as its rounding is concerned: where the middle of its arc sits for a radius,
 *        and back. The same rule polygonPath() rounds by, and the Box's, whose corners are right angles.
 */
struct Corner
{
    QPointF vertex;
    QPointF bisector;          //!< Towards the arc, between the two edges.
    qreal   perRadius = 0.0;   //!< From the vertex to the arc's middle, per unit of radius: 1/sin(θ/2) − 1.
    qreal   maxRadius = 0.0;   //!< The radius whose arc reaches the middle of the shorter edge.

    [[nodiscard]] bool valid() const { return perRadius > k_hairpinCorner; }
};

Corner cornerAt(const QPointF& prev, const QPointF& vertex, const QPointF& next)
{
    Corner        c{vertex};
    const QPointF a  = prev - vertex;
    const QPointF b  = next - vertex;
    const qreal   la = std::hypot(a.x(), a.y());
    const qreal   lb = std::hypot(b.x(), b.y());
    if (la <= 0.0 || lb <= 0.0)
        return c;
    const QPointF sum = a / la + b / lb;
    const qreal   ls  = std::hypot(sum.x(), sum.y());
    const qreal   half = std::acos(qBound(-1.0, (a.x() * b.x() + a.y() * b.y()) / (la * lb), 1.0)) / 2.0;
    if (ls <= k_hairpinCorner || std::sin(half) <= k_hairpinCorner)
        return c;   // a straight run or a hairpin: no arc to put a dot on
    c.bisector  = sum / ls;
    c.perRadius = 1.0 / std::sin(half) - 1.0;
    c.maxRadius = qMin(la, lb) / 2.0 * std::tan(half);
    return c;
}

//! The radius handle of @p corner rounded by @p radius: in the middle of the arc drawn, @p clearance further in.
QPointF radiusDot(const Corner& corner, qreal radius, qreal clearance)
{
    return corner.vertex + corner.bisector * (qBound(0.0, radius, corner.maxRadius) * corner.perRadius + clearance);
}

//! The radius whose dot sits nearest @p to, in whole pixels, from 0 to @p limit.
qreal radiusAt(const Corner& corner, const QPointF& to, qreal clearance, qreal limit)
{
    const QPointF d     = to - corner.vertex;
    const qreal   along = d.x() * corner.bisector.x() + d.y() * corner.bisector.y() - clearance;
    return qBound(0.0, qreal(qRound(along / corner.perRadius)), limit);
}

//! The Box's first corner, the top-left one.
Corner boxCorner(const QRectF& body)
{
    return cornerAt(body.bottomLeft(), body.topLeft(), body.topRight());
}

QList<ShapeHandle> boxHandles(const ShapeProperties& shape, const QRectF& body, qreal clearance, int)
{
    const Corner c = boxCorner(body);
    return {{ShapeHandle::Role::Radius, 0, radiusDot(c, shape.cornerRadius, clearance), -c.bisector}};
}

ShapeProperties dragBoxHandle(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                              const QPointF& to, qreal clearance)
{
    ShapeProperties s = shape;
    if (handle.role == ShapeHandle::Role::Radius)
        s.cornerRadius = radiusAt(boxCorner(body), to, clearance, cornerRadiusLimit(body.size()));
    return s;
}

//! The Polygon's first corner, at node 0, whose dot sets the radius every corner shares.
Corner polygonCorner(const QList<QPointF>& pts)
{
    return cornerAt(pts.last(), pts.first(), pts[1]);
}

/**
 * Every node, then the radius dot, then a "+" in the middle of each edge: the order a press is answered
 * in, so a node wins over a "+" on a short edge.
 */
QList<ShapeHandle> polygonHandles(const ShapeProperties& shape, const QRectF& body, qreal clearance, int)
{
    const QList<QPointF> pts = polygonPoints(shape, body);
    const int            n   = int(pts.size());
    QList<ShapeHandle>   out;
    for (int i = 0; i < n; ++i)
        out.append({ShapeHandle::Role::Vertex, i, pts[i]});
    if (const Corner c = polygonCorner(pts); c.valid())
        out.append({ShapeHandle::Role::Radius, 0, radiusDot(c, shape.cornerRadius, clearance), -c.bisector});
    for (int i = 0; i < n; ++i)
        out.append({ShapeHandle::Role::AddVertex, i, (pts[i] + pts[(i + 1) % n]) / 2.0});
    return out;
}

ShapeProperties dragPolygonHandle(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                                  const QPointF& to, qreal clearance)
{
    ShapeProperties s = shape;
    switch (handle.role) {
    case ShapeHandle::Role::Vertex:
        s.nodes = freeNodes(shape);   // a Polygon drawing the box's rectangle gets nodes of its own
        if (handle.index >= 0 && handle.index < s.nodes.size() && !body.isEmpty())
            s.nodes[handle.index].at = QPointF((to.x() - body.left()) / body.width(),
                                               (to.y() - body.top()) / body.height());
        break;
    case ShapeHandle::Role::Radius:
        if (const Corner c = polygonCorner(polygonPoints(shape, body)); c.valid())
            s.cornerRadius = radiusAt(c, to, clearance, cornerRadiusLimit(body.size()));
        break;
    case ShapeHandle::Role::AddVertex:
        break;   // a press adds the node (withVertexAdded()); the drag is that node's
    case ShapeHandle::Role::InHandle:
    case ShapeHandle::Role::OutHandle:
        break;   // a Curve's
    case ShapeHandle::Role::Depth:
        break;   // the edge's (dragShapeHandle())
    case ShapeHandle::Role::FadeFrom:
    case ShapeHandle::Role::FadeTo:
    case ShapeHandle::Role::FadeCentre:
        break;   // the fill's (dragFadeHandle())
    }
    return s;
}

/**
 * @brief A cubic segment from @p a to @p b cut in half (de Casteljau), relative to the body as the nodes
 *        are: the halves' new handles and the point between them. A straight one is cut at its middle,
 *        with no handles.
 */
struct Split
{
    QPointF aOut;            //!< @p a's out-handle, for the first half.
    QPointF mid;             //!< The point halfway along.
    QPointF midIn, midOut;   //!< Its handles.
    QPointF bIn;             //!< @p b's in-handle, for the second half.
};

Split splitSegment(const ShapeNode& a, const ShapeNode& b)
{
    const QPointF p0 = a.at, p1 = a.at + a.out, p2 = b.at + b.in, p3 = b.at;
    const QPointF p01 = (p0 + p1) / 2.0, p12 = (p1 + p2) / 2.0, p23 = (p2 + p3) / 2.0;
    const QPointF p012 = (p01 + p12) / 2.0, p123 = (p12 + p23) / 2.0;
    const QPointF mid  = (p012 + p123) / 2.0;
    if (a.out.isNull() && b.in.isNull())
        return {{}, mid, {}, {}, {}};
    return {p01 - p0, mid, p012 - mid, p123 - mid, p23 - p3};
}

/**
 * The picked node's two handles (the ones it has), then every node, then a "+" halfway along each
 * segment: the order a press is answered in, so a handle wins over the node it leaves.
 */
QList<ShapeHandle> curveHandles(const ShapeProperties& shape, const QRectF& body, qreal, int picked)
{
    const QList<ShapeNode>& nodes = freeNodes(shape);
    const int               n     = int(nodes.size());
    QList<ShapeHandle>      out;
    if (picked >= 0 && picked < n) {
        const ShapeNode& p  = nodes[picked];
        const QPointF    at = inBody(p.at, body);
        if (!p.in.isNull())
            out.append({ShapeHandle::Role::InHandle, picked, inBody(p.at + p.in, body), {}, at});
        if (!p.out.isNull())
            out.append({ShapeHandle::Role::OutHandle, picked, inBody(p.at + p.out, body), {}, at});
    }
    for (int i = 0; i < n; ++i)
        out.append({ShapeHandle::Role::Vertex, i, inBody(nodes[i].at, body)});
    for (int i = 0; i < n; ++i)
        out.append({ShapeHandle::Role::AddVertex, i, inBody(splitSegment(nodes[i], nodes[(i + 1) % n]).mid, body)});
    return out;
}

/**
 * @brief @p node's other handle put where its type says, after the one @p movedOut names was moved: on
 *        one line through the node, for a Smooth one as long as it was, for a Symmetric one as long as
 *        the moved one. In pixels, where "on one line" and "as long" are what the eye sees.
 */
void keepType(ShapeNode& node, bool movedOut, const QRectF& body)
{
    if (node.type == ShapeNode::Type::Corner)
        return;
    const QPointF moved = toPixels(movedOut ? node.out : node.in, body);
    QPointF&      other = movedOut ? node.in : node.out;
    const qreal   lm    = length(moved);
    if (lm <= 0.0)
        return;
    const qreal lo = node.type == ShapeNode::Type::Symmetric ? lm : length(toPixels(other, body));
    other          = fromPixels(-moved / lm * lo, body);
}

ShapeProperties dragCurveHandle(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                                const QPointF& to, qreal)
{
    ShapeProperties s = shape;
    s.nodes           = freeNodes(shape);   // a Curve drawing the box's rectangle gets nodes of its own
    if (handle.index < 0 || handle.index >= s.nodes.size() || body.isEmpty())
        return shape;
    ShapeNode&    node = s.nodes[handle.index];
    const QPointF rel  = relativeTo(to, body);
    switch (handle.role) {
    case ShapeHandle::Role::Vertex:
        node.at = rel;   // its handles are offsets, so they come along
        break;
    case ShapeHandle::Role::InHandle:
        node.in = rel - node.at;
        keepType(node, false, body);
        break;
    case ShapeHandle::Role::OutHandle:
        node.out = rel - node.at;
        keepType(node, true, body);
        break;
    case ShapeHandle::Role::Radius:
    case ShapeHandle::Role::AddVertex:
    case ShapeHandle::Role::Depth:
    case ShapeHandle::Role::FadeFrom:
    case ShapeHandle::Role::FadeTo:
    case ShapeHandle::Role::FadeCentre:
        break;   // no radius; a press on a "+" adds the node (withVertexAdded()); the edge's and the fill's are not here
    }
    return s;
}

/**
 * Which type @p node reads as, from its handles in @p body: Smooth when they point opposite ways along
 * one line, Symmetric when they are as long as each other too, else Corner.
 */
ShapeNode::Type typeOf(const ShapeNode& node, const QRectF& body)
{
    const QPointF in  = toPixels(node.in, body);
    const QPointF out = toPixels(node.out, body);
    const qreal   li  = length(in);
    const qreal   lo  = length(out);
    if (li <= 0.0 || lo <= 0.0)
        return ShapeNode::Type::Corner;
    const qreal sine   = (in.x() * out.y() - in.y() * out.x()) / (li * lo);
    const qreal cosine = (in.x() * out.x() + in.y() * out.y()) / (li * lo);
    if (std::abs(sine) > k_smoothTolerance || cosine >= 0.0)
        return ShapeNode::Type::Corner;
    return std::abs(li - lo) <= k_smoothTolerance * qMax(li, lo) ? ShapeNode::Type::Symmetric
                                                                  : ShapeNode::Type::Smooth;
}

// ---------------------------------------------------------------------------
// Cloud
// ---------------------------------------------------------------------------

/**
 * @brief Appends the arc of the ellipse round @p c with radii @p rx, @p ry, from parameter @p start on by
 *        @p sweep (radians, clockwise on screen), as cubics of at most a quarter turn; @p p is at its start.
 *
 * Not QPainterPath::arcTo(): that finds the arc's first point on its own Bézier approximation, a few
 * thousandths of a pixel off the curve, and joins it with a line — a sliver, and a node of its own once
 * the outline is converted to a Curve. A circle is the ellipse with equal radii.
 */
void addArc(QPainterPath& p, const QPointF& c, qreal rx, qreal ry, qreal start, qreal sweep)
{
    const int   pieces = qMax(1, qCeil(sweep / (M_PI / 2.0)));
    const qreal step   = sweep / pieces;
    const qreal handle = 4.0 / 3.0 * std::tan(step / 4.0);
    const auto  at     = [&](qreal t) { return c + QPointF(rx * qCos(t), ry * qSin(t)); };
    const auto  ahead  = [&](qreal t) { return QPointF(-rx * qSin(t), ry * qCos(t)) * handle; };
    for (int j = 0; j < pieces; ++j) {
        const qreal a0 = start + j * step;
        const qreal a1 = a0 + step;
        p.cubicTo(at(a0) + ahead(a0), at(a1) - ahead(a1), at(a1));
    }
}

//! A Cloud's lobe radius in @p r.
qreal lobeRadius(const ShapeProperties& shape, const QRectF& r)
{
    const int size = qBound(k_minLobeSize, shape.lobeSize, k_maxLobeSize);
    return qMax(k_minLobePx, qMin(r.width(), r.height()) / 2.0 * size / 100.0);
}

//! Lobe @p i of a Cloud's @p n's centre: on the ellipse a lobe's radius in from @p r's edge, the first on the right.
QPointF lobeCentre(int i, int n, qreal lobe, const QRectF& r)
{
    const qreal ang = (2.0 * M_PI * i) / n;
    return {r.center().x() + qCos(ang) * qMax(0.0, r.width() / 2.0 - lobe),
            r.center().y() + qSin(ang) * qMax(0.0, r.height() / 2.0 - lobe)};
}

/**
 * @brief The parameter between @p from and @p to where the inner ellipse @p at crosses the circle round
 *        @p c of radius @p radius, inside it at @p from and outside at @p to (or the other way round).
 */
qreal crossing(const std::function<QPointF(qreal)>& at, const QPointF& c, qreal radius, qreal from, qreal to)
{
    const auto inside = [&](qreal t) { return length(at(t) - c) < radius; };
    const bool startIn = inside(from);
    for (int k = 0; k < k_crossingSteps; ++k) {
        const qreal mid = (from + to) / 2.0;
        (inside(mid) == startIn ? from : to) = mid;
    }
    return (from + to) / 2.0;
}

/**
 * @brief A cloud: a ring of circles around an inner ellipse, the outline of their union.
 *
 * Built from arcs rather than by uniting circles, so the path is a few cubics a lobe (and *Convert to
 * Curve* gets a node per arc piece, not a polygon of hundreds), and nothing is computed by clipping.
 * Each lobe's outer arc runs from where it meets the lobe before to where it meets the one after: their
 * outer crossing, or, where two lobes do not meet outside the inner ellipse (the long sides of a wide
 * cloud), the ellipse itself carries the outline from one to the next. The inner ellipse is what keeps
 * the middle solid when the lobes are small.
 */
QPainterPath cloudPath(const ShapeProperties& shape, const QRectF& r)
{
    const int     n    = qBound(k_minLobes, shape.lobes, k_maxLobes);
    const qreal   lobe = lobeRadius(shape, r);
    const QPointF c    = r.center();
    const qreal   ex   = qMax(k_minInnerPx, r.width() / 2.0 - lobe * k_innerReach);
    const qreal   ey   = qMax(k_minInnerPx, r.height() / 2.0 - lobe * k_innerReach);
    const auto    onInner = [&](qreal t) { return c + QPointF(ex * qCos(t), ey * qSin(t)); };
    const auto    outsideInner = [&](const QPointF& q) {
        const qreal x = (q.x() - c.x()) / ex, y = (q.y() - c.y()) / ey;
        return x * x + y * y > 1.0;
    };
    QList<QPointF> centre;
    for (int i = 0; i < n; ++i)
        centre.append(lobeCentre(i, n, lobe, r));

    // Where each lobe is left for the next (exit) and the next one taken up (entry). Where they meet
    // outside the ellipse, the two are one point; else each is where its lobe crosses the ellipse, and
    // the ellipse runs between them (bridge, its parameters).
    QList<QPointF>                 exit(n), entry(n);
    QList<std::pair<qreal, qreal>> bridge(n, {0.0, 0.0});
    for (int i = 0; i < n; ++i) {
        const int     j = (i + 1) % n;
        const QPointF v = centre[j] - centre[i];
        const qreal   d = length(v);
        // Outwards from the chord: the ring runs clockwise on screen, so its outside is on the left of
        // the way it goes. Two lobes on one spot (they meet in the middle) take the direction between.
        const qreal   mid = (2.0 * M_PI * (i + 0.5)) / n;
        const QPointF out = d > k_hairpinCorner ? QPointF(v.y(), -v.x()) / d : QPointF(qCos(mid), qSin(mid));
        if (d <= 2.0 * lobe) {
            const QPointF meet = (centre[i] + centre[j]) / 2.0 + out * std::sqrt(lobe * lobe - d * d / 4.0);
            if (outsideInner(meet)) {
                exit[i] = entry[j] = meet;
                continue;
            }
        }
        const qreal a = (2.0 * M_PI * i) / n;
        const qreal b = a + 2.0 * M_PI / n;
        bridge[i]     = {crossing(onInner, centre[i], lobe, a, b), crossing(onInner, centre[j], lobe, a, b)};
        exit[i]       = onInner(bridge[i].first);
        entry[j]      = onInner(bridge[i].second);
    }

    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    p.moveTo(entry[0]);
    for (int i = 0; i < n; ++i) {
        const QPointF from = entry[i] - centre[i];
        const QPointF to   = exit[i] - centre[i];
        // Clockwise on screen, from the entry round the outside to the exit.
        const qreal start = std::atan2(from.y(), from.x());
        qreal       sweep = std::atan2(to.y(), to.x()) - start;
        while (sweep < 0.0)
            sweep += 2.0 * M_PI;
        if (sweep <= k_coveredArc * 2.0 * M_PI)
            addArc(p, centre[i], lobe, lobe, start, sweep);
        else
            p.lineTo(exit[i]);   // covered by its neighbours: straight on to where the next one starts
        if (const auto [t0, t1] = bridge[i]; t1 > t0)
            addArc(p, c, ex, ey, t0, t1 - t0);
    }
    p.closeSubpath();
    return p;
}

QRectF cloudArea(const ShapeProperties& shape, const QRectF& body)
{
    // The lobes eat into the ring, so the safe area is inside the ellipse through their centres.
    const qreal lobe = lobeRadius(shape, body);
    return inscribedInEllipse(shape, body.adjusted(lobe, lobe, -lobe, -lobe));
}

/**
 * The lobe size: an arrow at the first lobe's centre, along the way the lobe grows (outwards). The
 * centre is a lobe's radius in from the edge, so dragging it in grows the lobes.
 */
QList<ShapeHandle> cloudHandles(const ShapeProperties& shape, const QRectF& body, qreal, int)
{
    const qreal lobe = lobeRadius(shape, body);
    return {{ShapeHandle::Role::Radius, 0, lobeCentre(0, qBound(k_minLobes, shape.lobes, k_maxLobes), lobe, body),
             QPointF(1, 0)}};
}

ShapeProperties dragCloudHandle(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                                const QPointF& to, qreal)
{
    ShapeProperties s     = shape;
    const qreal     shortHalf = qMin(body.width(), body.height()) / 2.0;
    if (handle.role != ShapeHandle::Role::Radius || shortHalf <= 0.0)
        return s;
    const qreal lobe = body.right() - to.x();   // the first lobe's centre is a radius in from the right
    s.lobeSize       = qBound(k_minLobeSize, qRound(lobe / shortHalf * 100.0), k_maxLobeSize);
    return s;
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------
// Edges: the outline pushed in along it, in a pattern (PLAN-L M11)
// ---------------------------------------------------------------------------

//! The closest two teeth may be, in balloon pixels: below it the edge is a blur, and the count unbounded.
constexpr qreal   k_minEdgePitch = 2.0;
//! The fewest teeth round an edge: fewer reads as a polygon, not as a modulated edge.
constexpr int     k_minTeeth     = 3;
//! Mixed into the style seed for the edge's irregularity, so it draws from a stream of its own.
constexpr quint32 k_edgeStream   = 0xC2B2AE35u;
//! A corner whose fade would be shorter than this, in balloon pixels, does not fade: it is flat enough.
constexpr qreal   k_fadeFrom     = 0.5;
//! How much of the room to a sharp corner's middle a push may take: half, so the two sides never meet.
constexpr qreal   k_fadeShare    = 0.5;

//! Twice @p ring's signed area: positive when it runs clockwise on screen (y down).
qreal signedArea2(const QPolygonF& ring)
{
    qreal sum = 0.0;
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const QPointF& p = ring[i];
        const QPointF& q = ring[(i + 1) % ring.size()];
        sum += p.x() * q.y() - q.x() * p.y();
    }
    return sum;
}

//! The unit normal at @p p pointing into a ring that runs clockwise (@p clockwise) or the other way.
QPointF inwards(const EdgeWalk::Point& p, bool clockwise)
{
    const QPointF n(-p.tangent.y(), p.tangent.x());   // into a clockwise ring
    return clockwise ? n : -n;
}

//! How many teeth go round an edge of @p length, and how far apart: whole, so there is no seam.
std::pair<int, qreal> teethOf(qreal length, const ShapeProperties& shape)
{
    const int teeth = qMax(k_minTeeth, qRound(length / qMax(shape.edgePitch, k_minEdgePitch)));
    return {teeth, length / teeth};
}

/**
 * @brief @p walk's edge pushed in by @p depthAt(s) at each of @p stations (distances along it) and at each
 *        of its own corners, in order; never deeper than @p depth.
 *
 * The corners are what keep the shape: a point between two stations would otherwise be cut off by the
 * straight line between them (a star loses its spikes). A corner is pushed along the line between its
 * two edges, so it stays a corner; anywhere else, straight in.
 *
 * Near a sharp outward corner the push fades to nothing at the corner itself: the two edges pushed in
 * would otherwise cross where the shape is narrower than twice the depth, and a spike's tip would turn
 * into a loop. At a distance x from a corner of half-angle a the edges are x · tan(a) from its middle,
 * and either may go half of that deep there (`k_fadeShare`), so the two never meet. A smooth curve's corners are nearly flat, so they barely
 * fade at all.
 */
template <class DepthAt>
QPolygonF pushedIn(const EdgeWalk& walk, bool clockwise, const std::vector<qreal>& stations, qreal depth,
                   DepthAt depthAt)
{
    const std::vector<qreal>& corners = walk.corners();
    const qreal               length  = walk.length();
    const auto                before  = [&](qreal s) {
        // walk.at() at a corner answers for the edge after it; the one before is a hair back.
        return walk.at(s > k_hairpinCorner ? s - k_hairpinCorner : s - k_hairpinCorner + length);
    };

    // The outward corners sharp enough to fade near: where, and tan of half their angle.
    std::vector<std::pair<qreal, qreal>> sharp;
    for (size_t i = 0; i + 1 < corners.size(); ++i) {
        const QPointF a     = before(corners[i]).tangent;
        const QPointF b     = walk.at(corners[i]).tangent;
        const qreal   cross = a.x() * b.y() - a.y() * b.x();
        if ((cross > 0.0) != clockwise)
            continue;   // turning away from the inside: an inward corner, wide enough inside
        const qreal turn     = std::atan2(std::abs(cross), a.x() * b.x() + a.y() * b.y());
        const qreal tanHalf  = std::tan((M_PI - turn) / 2.0);
        if (depth / (k_fadeShare * qMax(tanHalf, k_hairpinCorner)) > k_fadeFrom)
            sharp.push_back({corners[i], tanHalf});
    }
    const auto fade = [&](qreal s) {
        qreal f = 1.0;
        for (const auto& [at, tanHalf] : sharp) {
            const qreal d = std::abs(s - at);
            const qreal x = qMin(d, length - d);   // round the edge, either way
            f             = qMin(f, k_fadeShare * x * tanHalf / depth);
        }
        return qBound(0.0, f, 1.0);
    };

    std::vector<std::pair<qreal, bool>> at;   // a distance along the edge, and whether it is a corner
    at.reserve(stations.size() + corners.size());
    for (qreal s : stations)
        at.push_back({s, false});
    for (size_t i = 0; i + 1 < corners.size(); ++i)   // the last is the first again
        at.push_back({corners[i], true});
    std::sort(at.begin(), at.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QPolygonF poly;
    poly.reserve(qsizetype(at.size()));
    for (const auto& [s, corner] : at) {
        const EdgeWalk::Point p = walk.at(s);
        QPointF               n = inwards(p, clockwise);
        if (corner) {
            const QPointF both = n + inwards(before(s), clockwise);
            if (const qreal l = std::hypot(both.x(), both.y()); l > k_hairpinCorner)
                n = both / l;
        }
        poly << p.at + n * qMin(depthAt(s), depth) * fade(s);
    }
    return poly;
}

/**
 * @brief A zigzag: straight teeth from the outline in to a valley `edgeDepth` deep, halfway between.
 *
 * The points stay on the outline and the valleys go in, so the balloon keeps its bounds. Irregularity
 * makes each valley shallower by up to its share and moves it along by up to a quarter of a tooth,
 * seeded, so it stays put while the balloon is edited. The outline's own corners ride on the teeth
 * (pushedIn()), so a polygon keeps its points.
 */
QPainterPath zigzagEdge(const QPainterPath& outline, const ShapeProperties& shape, quint32 seed)
{
    const qreal depth = qMax(shape.edgeDepth, 0.0);
    const qreal irr   = qBound(0, shape.edgeIrregularity, 100) / 100.0;
    if (depth <= 0.0)
        return outline;
    QRandomGenerator rng(seed ^ k_edgeStream);
    QPainterPath     out;
    out.setFillRule(Qt::WindingFill);
    for (const QPolygonF& ring : outline.toSubpathPolygons()) {
        const EdgeWalk walk(ring);
        if (walk.length() <= 0.0)
            continue;
        // Not a structured binding: a C++17 lambda cannot capture one.
        const std::pair<int, qreal> spacing = teethOf(walk.length(), shape);
        const int                   teeth   = spacing.first;
        const qreal                 step    = spacing.second;
        std::vector<qreal>          deep(static_cast<size_t>(teeth)), valley(static_cast<size_t>(teeth)), stations;
        stations.reserve(size_t(teeth) * 2);
        for (int k = 0; k < teeth; ++k) {
            // Both drawn before either is used, so a change to one does not re-deal the other.
            deep[size_t(k)]   = depth * (1.0 - irr * rng.generateDouble());
            valley[size_t(k)] = k * step + step / 2.0 + irr * (rng.generateDouble() - 0.5) * step / 2.0;
            stations.push_back(k * step);
            stations.push_back(valley[size_t(k)]);
        }
        // Straight from a point (0 deep) down to its tooth's valley and up to the next point.
        const auto depthAt = [&](qreal s) {
            const int   k    = qBound(0, int(s / step), teeth - 1);
            const qreal from = k * step;
            const qreal v    = valley[size_t(k)];
            const qreal to   = from + step;
            const qreal f    = s <= v ? (s - from) / (v - from) : (to - s) / (to - v);
            return deep[size_t(k)] * qBound(0.0, f, 1.0);
        };
        out.addPolygon(pushedIn(walk, signedArea2(ring) > 0.0, stations, depth, depthAt));
        out.closeSubpath();
    }
    return out;
}

//! In the enum's order; edgeInfo() checks every entry names its kind.
const std::array<EdgeInfo, int(ShapeProperties::k_lastEdge) + 1> k_edges{{
    {ShapeProperties::Edge::None,   "none",   QT_TRANSLATE_NOOP("QObject", "None"),   nullptr},
    {ShapeProperties::Edge::Zigzag, "zigzag", QT_TRANSLATE_NOOP("QObject", "Zigzag"), zigzagEdge},
}};

/**
 * @brief Where the edge's depth handle sits: on the bare outline at its first valley, and the way in
 *        there. Invalid with no edge, or no outline.
 */
struct DepthAnchor
{
    QPointF base;
    QPointF inward;
    bool    valid = false;
};

DepthAnchor depthAnchor(const ShapeProperties& shape, const QRectF& body)
{
    const auto outline = shapeInfo(shape.kind).outline;
    if (shape.edge == ShapeProperties::Edge::None || !outline || body.isEmpty())
        return {};
    const QList<QPolygonF> rings = outline(shape, body).toSubpathPolygons();
    if (rings.isEmpty())
        return {};
    const EdgeWalk walk(rings.first());
    if (walk.length() <= 0.0)
        return {};
    const qreal           step = teethOf(walk.length(), shape).second;
    const EdgeWalk::Point v    = walk.at(step / 2.0);
    return {v.at, inwards(v, signedArea2(rings.first()) > 0.0), true};
}

// ---------------------------------------------------------------------------

using Kind = ShapeProperties::Kind;

//! In the enum's order, so a kind is its own index; shapeInfo() checks every entry names its kind.
// The flash stretches were chosen by eye, shape by shape, on rendered sheets (PLAN-S A18); the Box's is
// a round number between its two templates' old values, to be checked by eye (PLAN-L B22).
const std::array<ShapeInfo, int(ShapeProperties::k_lastKind) + 1> k_shapes{{
    {Kind::None,      "none",      QT_TRANSLATE_NOOP("QObject", "Text only — no balloon"), false, false, nullptr,       wholeArea,          0},
    {Kind::Box,       "box",       QT_TRANSLATE_NOOP("QObject", "Box"),                    true,  true,  boxPath,       boxArea,            50,
     boxHandles, dragBoxHandle},
    {Kind::Ellipse,   "ellipse",   QT_TRANSLATE_NOOP("QObject", "Round balloon"),          true,  false, ellipsePath,   inscribedInEllipse, 0},
    {Kind::Polygon,   "polygon",   QT_TRANSLATE_NOOP("QObject", "Polygon"),                false, true,  polygonPath,   polygonArea,        0,
     polygonHandles, dragPolygonHandle},
    {Kind::Cloud,     "cloud",     QT_TRANSLATE_NOOP("QObject", "Cloud"),                  true,  false, cloudPath,     cloudArea,          0,
     cloudHandles, dragCloudHandle},
    {Kind::Curve,     "curve",     QT_TRANSLATE_NOOP("QObject", "Curve"),                  false, false, curvePath,     curveArea,          0,
     curveHandles, dragCurveHandle},
}};

//! A kind with every parameter at its default — the template of a kind that has no parameters.
ShapeProperties plain(Kind kind)
{
    ShapeProperties s;
    s.kind = kind;
    return s;
}

//! A Box with corner radius @p radius.
ShapeProperties box(qreal radius)
{
    ShapeProperties s = plain(Kind::Box);
    s.cornerRadius    = radius;
    return s;
}

//! @p p rounded to the decimals a node is saved with.
QPointF saved(const QPointF& p)
{
    const qreal scale = std::pow(10.0, ShapeProperties::k_nodeDecimals);
    return {qRound(p.x() * scale) / scale, qRound(p.y() * scale) / scale};
}

/**
 * @brief A sharp-cornered Polygon through @p nodes, rounded to the decimals a node is saved with, so a
 *        placed template reads back from its file as itself.
 */
ShapeProperties polygon(const QList<QPointF>& nodes)
{
    ShapeProperties s = plain(Kind::Polygon);
    s.cornerRadius    = 0.0;
    for (const QPointF& n : nodes)
        s.nodes.append(ShapeNode(saved(n), {}, {}));
    return s;
}

//! A Curve through @p nodes, rounded to the decimals a node is saved with, as polygon()'s are.
ShapeProperties curve(const QList<ShapeNode>& nodes)
{
    ShapeProperties s = plain(Kind::Curve);
    for (const ShapeNode& n : nodes)
        s.nodes.append(ShapeNode(saved(n.at), saved(n.in), saved(n.out), n.type));
    return s;
}

/**
 * @brief An unrolled scroll: bowed long edges, and a rolled end on each side that is half an ellipse.
 *
 * Each long edge is a quadratic, as a cubic: its handles two thirds of the way to the quadratic's one.
 * Each rolled end is two quarters of an ellipse, through the end's outermost point.
 */
QList<ShapeNode> scrollNodes()
{
    using Type       = ShapeNode::Type;
    const qreal roll = k_scrollRoll;
    const qreal arc  = k_quarterArc;
    const qreal run  = 2.0 / 3.0 * (0.5 - roll);   // a long edge's handles, along it...
    const qreal sag  = 2.0 / 3.0 * k_scrollBow;     // ...and in towards the middle
    return {
        {{roll, 0},     {-arc * roll, 0}, {run, sag}},
        {{1 - roll, 0}, {-run, sag},      {arc * roll, 0}},
        {{1, 0.5},      {0, -arc / 2},    {0, arc / 2},     Type::Symmetric},
        {{1 - roll, 1}, {arc * roll, 0},  {-run, -sag}},
        {{roll, 1},     {run, -sag},      {-arc * roll, 0}},
        {{0, 0.5},      {0, arc / 2},     {0, -arc / 2},    Type::Symmetric},
    };
}

//! A star of @p spikes points, its inner vertices at @p inner of the outer radius, the first one on top.
QList<QPointF> starNodes(int spikes, qreal inner)
{
    QList<QPointF> out;
    for (int i = 0; i < spikes * 2; ++i) {
        const qreal ang = (M_PI * i) / spikes - M_PI / 2.0;
        const qreal k   = (i % 2 == 0) ? 0.5 : 0.5 * inner;
        out.append(QPointF(0.5 + qCos(ang) * k, 0.5 + qSin(ang) * k));
    }
    return out;
}

} // namespace

const ShapeInfo& shapeInfo(ShapeProperties::Kind kind)
{
    const int i = int(kind);
    // A kind past the table is a file from a newer build, or a corrupt value: draw a Box, the fallback
    // the readers already use, rather than reading past the end.
    if (i < 0 || i >= int(k_shapes.size()))
        return k_shapes[int(Kind::Box)];
    Q_ASSERT(k_shapes[i].kind == kind);   // the table is out of the enum's order
    return k_shapes[i];
}

const QList<ShapeTemplate>& shapeTemplates()
{
    // The picker's order: the speaking shapes first, the most used of them leading.
    static const QList<ShapeTemplate> templates{
        {"speech",    QT_TRANSLATE_NOOP("QObject", "Speech balloon"),  plain(Kind::Box),       true},
        {"ellipse",   QT_TRANSLATE_NOOP("QObject", "Round balloon"),   plain(Kind::Ellipse),   true},
        {"thought",   QT_TRANSLATE_NOOP("QObject", "Thought balloon"), plain(Kind::Cloud),     true},
        {"shout",     QT_TRANSLATE_NOOP("QObject", "Shout"),
         polygon(starNodes(k_burstSpikes, k_burstInner)), true},
        {"caption",   QT_TRANSLATE_NOOP("QObject", "Caption box"),     box(k_captionCorner),   false},
        // A caption plate seen in perspective: full width at the bottom, narrowed at the top.
        {"trapezoid", QT_TRANSLATE_NOOP("QObject", "Caption plate"),
         polygon({{k_trapezoidSlant, 0}, {1 - k_trapezoidSlant, 0}, {1, 1}, {0, 1}}), false},
        {"diamond",   QT_TRANSLATE_NOOP("QObject", "Diamond"),
         polygon({{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}), false},
        // A ribbon with a V notched into each end.
        {"banner",    QT_TRANSLATE_NOOP("QObject", "Banner"),
         polygon({{0, 0}, {1, 0}, {1 - k_bannerNotch, 0.5}, {1, 1}, {0, 1}, {k_bannerNotch, 0.5}}), false},
        {"scroll",    QT_TRANSLATE_NOOP("QObject", "Scroll"),          curve(scrollNodes()),   false},
    };
    return templates;
}

const ShapeTemplate& shapeTemplate(const char* id)
{
    for (const ShapeTemplate& t : shapeTemplates())
        if (qstrcmp(t.id, id) == 0)
            return t;
    Q_ASSERT_X(false, "shapeTemplate", "no template has this id");
    return shapeTemplates().first();
}

const ShapeTemplate* templateOf(const ShapeProperties& shape)
{
    // The edge is laid over any outline and kept by a picked tile, so it does not tell templates apart.
    for (const ShapeTemplate& t : shapeTemplates())
        if (withEdgeOf(t.shape, shape) == shape)
            return &t;
    return nullptr;
}

const EdgeInfo& edgeInfo(ShapeProperties::Edge kind)
{
    const int i = int(kind);
    if (i < 0 || i >= int(k_edges.size()))
        return k_edges[int(ShapeProperties::Edge::None)];
    Q_ASSERT(k_edges[i].kind == kind);   // the table is out of the enum's order
    return k_edges[i];
}

qreal edgeInset(const ShapeProperties& shape)
{
    return edgeInfo(shape.edge).modulate ? qMax(shape.edgeDepth, 0.0) : 0.0;
}

ShapeProperties withEdgeOf(ShapeProperties shape, const ShapeProperties& from)
{
    shape.edge             = from.edge;
    shape.edgePitch        = from.edgePitch;
    shape.edgeDepth        = from.edgeDepth;
    shape.edgeIrregularity = from.edgeIrregularity;
    return shape;
}

bool hasShapeHandles(const ShapeProperties& shape)
{
    return shapeInfo(shape.kind).handles != nullptr || edgeInfo(shape.edge).modulate != nullptr;
}

QList<ShapeHandle> shapeHandles(const ShapeProperties& shape, const QRectF& body, qreal clearance, int picked)
{
    const ShapeInfo&   info = shapeInfo(shape.kind);
    QList<ShapeHandle> out  = info.handles ? info.handles(shape, body, clearance, picked) : QList<ShapeHandle>();
    if (const DepthAnchor d = depthAnchor(shape, body); d.valid)
        out.append({ShapeHandle::Role::Depth, 0, d.base + d.inward * qMax(shape.edgeDepth, 0.0), d.inward});
    return out;
}

ShapeProperties dragShapeHandle(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                                const QPointF& to, qreal clearance)
{
    if (handle.role != ShapeHandle::Role::Depth) {
        const ShapeInfo& info = shapeInfo(shape.kind);
        return info.dragHandle ? info.dragHandle(shape, body, handle, to, clearance) : shape;
    }
    ShapeProperties   s = shape;
    const DepthAnchor d = depthAnchor(shape, body);
    if (d.valid) {
        const QPointF along = to - d.base;
        const qreal   depth = along.x() * d.inward.x() + along.y() * d.inward.y();
        s.edgeDepth         = qBound(0.0, qreal(qRound(depth)), cornerRadiusLimit(body.size()));
    }
    return s;
}

bool shapeSpeaks(const ShapeProperties& shape)
{
    const ShapeTemplate* t = templateOf(shape);
    return t ? t->speaks : shapeInfo(shape.kind).speaks;
}

qreal cornerRadiusLimit(const QSizeF& size)
{
    return qMin(size.width(), size.height()) / 2.0;
}

ShapeProperties withVertexAdded(const ShapeProperties& shape, int edge)
{
    ShapeProperties s = shape;
    s.nodes           = freeNodes(shape);
    const int n       = int(s.nodes.size());
    if (edge < 0 || edge >= n)
        return shape;
    ShapeNode& a = s.nodes[edge];
    ShapeNode& b = s.nodes[(edge + 1) % n];
    if (shape.kind != ShapeProperties::Kind::Curve) {   // a Polygon's edge is straight, whatever its nodes hold
        s.nodes.insert(edge + 1, ShapeNode((a.at + b.at) / 2.0, {}, {}));
        return s;
    }
    const Split half = splitSegment(a, b);
    a.out            = half.aOut;
    b.in             = half.bIn;
    // A cut curve leaves its new node with its handles on one line; a straight one with none.
    const ShapeNode::Type type = half.midIn.isNull() ? ShapeNode::Type::Corner : ShapeNode::Type::Smooth;
    s.nodes.insert(edge + 1, ShapeNode(half.mid, half.midIn, half.midOut, type));
    return s;
}

ShapeProperties withVertexRemoved(const ShapeProperties& shape, int index)
{
    ShapeProperties s = shape;
    s.nodes           = freeNodes(shape);
    if (s.nodes.size() <= 3 || index < 0 || index >= s.nodes.size())
        return shape;
    s.nodes.removeAt(index);
    return s;
}

ShapeProperties withNodeType(const ShapeProperties& shape, const QRectF& body, int index, ShapeNode::Type type)
{
    ShapeProperties s = shape;
    s.nodes           = freeNodes(shape);
    const int n       = int(s.nodes.size());
    if (index < 0 || index >= n || body.isEmpty())
        return shape;
    ShapeNode& node = s.nodes[index];
    node.type       = type;
    if (type == ShapeNode::Type::Corner)
        return s;   // the handles stay where they are, now free of each other

    const QPointF prev = toPixels(node.at - s.nodes[(index + n - 1) % n].at, body);
    const QPointF next = toPixels(s.nodes[(index + 1) % n].at - node.at, body);
    const QPointF in   = toPixels(node.in, body);
    const QPointF out  = toPixels(node.out, body);
    // Along the line the handles lean along, out from the in-handle to the out-handle; with no handles,
    // along the neighbours'.
    QPointF dir = out - in;
    if (length(dir) <= 0.0)
        dir = prev + next;
    if (length(dir) <= 0.0)
        return s;
    dir /= length(dir);
    qreal li = length(in) > 0.0 ? length(in) : length(prev) * k_newHandle;
    qreal lo = length(out) > 0.0 ? length(out) : length(next) * k_newHandle;
    if (type == ShapeNode::Type::Symmetric)
        li = lo = (li + lo) / 2.0;
    node.in  = fromPixels(-dir * li, body);
    node.out = fromPixels(dir * lo, body);
    return s;
}

ShapeProperties curveOf(const QPainterPath& outline, const QRectF& body)
{
    ShapeProperties s;
    s.kind = ShapeProperties::Kind::Curve;
    if (body.isEmpty())
        return s;
    QList<ShapeNode>& nodes = s.nodes;
    const auto        point = [&](int i) { return relativeTo(outline.elementAt(i), body); };
    for (int i = 0; i < outline.elementCount(); ++i) {
        const QPainterPath::Element e = outline.elementAt(i);
        const QPointF               p = point(i);
        if (e.isMoveTo()) {
            if (!nodes.isEmpty())
                break;   // the first subpath only: the outlines converted have one
            nodes.append(ShapeNode(p, {}, {}));
        } else if (e.isLineTo()) {
            if (!Fields::same(p, nodes.last().at))
                nodes.append(ShapeNode(p, {}, {}));
        } else if (e.isCurveTo() && i + 2 < outline.elementCount()) {
            nodes.last().out = p - nodes.last().at;   // p is the first control point
            const QPointF end = point(i + 2);
            nodes.append(ShapeNode(end, point(i + 1) - end, {}));
            i += 2;
        }
    }
    // A closed path comes back to where it began: that last node is the first one.
    if (nodes.size() > 1 && Fields::same(nodes.last().at, nodes.first().at)) {
        nodes.first().in = nodes.last().in;
        nodes.removeLast();
    }
    for (ShapeNode& node : nodes)
        node.type = typeOf(node, body);
    return s;
}

QRectF balloonRect(const ObjectRecord& a)
{
    const qreal  sw = a.style.strokeWidth / 2.0;
    QRectF body(0, 0, a.box.width(), a.box.height());
    body.adjust(sw, sw, -sw, -sw);
    return body.normalized();
}

QPainterPath outlineOf(const ObjectRecord& a, const QRectF& body)
{
    if (!a.hasSilhouette() || body.isEmpty())
        return {};
    const auto outline = shapeInfo(a.shape.kind).outline;
    if (!outline)
        return {};
    // The edge on the bare outline, before any tail is united into it (PLAN-L B26).
    const auto modulate = edgeInfo(a.shape.edge).modulate;
    return modulate ? modulate(outline(a.shape, body), a.shape, a.styleSeed) : outline(a.shape, body);
}

}  // namespace Geometry
