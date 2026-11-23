#include "bezier.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Bezier {

namespace {

/**
 *  How far from one line, and from one length, a node's two handles may be and still read as Smooth or
 *  Symmetric: as a fraction (a sine, a ratio of lengths).
 */
constexpr qreal k_smoothTolerance = 1e-3;
/**
 *  A handle drawn for a node that has none: this fraction of the way to its neighbour, as a straight segment's
 *  handles would sit.
 */
constexpr qreal k_newHandle = 1.0 / 3.0;

qreal length(const QPointF& v)
{
    return std::hypot(v.x(), v.y());
}

//! The node after @p i, round the end of a closed path.
int next(const QList<ShapeNode>& nodes, int i)
{
    return (i + 1) % int(nodes.size());
}

//! The node before @p i, round the start of a closed path.
int previous(const QList<ShapeNode>& nodes, int i)
{
    const int n = int(nodes.size());
    return (i + n - 1) % n;
}

//! Whether node @p i has a neighbour before it (@p out false) or after it (@p out true) on the path.
bool hasSide(const QList<ShapeNode>& nodes, const Rules& rules, int i, bool out)
{
    return rules.closed || (out ? i + 1 < nodes.size() : i > 0);
}

/**
 * \brief A cubic segment from @p a to @p b cut in half (de Casteljau), in fractions as the nodes are: the halves'
 *        new handles and the point between them. A straight one is cut at its middle, with no handles.
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
 * Where node @p i's missing handle on one side grows from: a third of the way to the neighbour on that side
 * (@p out: the next one), on the line to it, so on a straight segment it bends nothing.
 */
QPointF grownHandle(const QList<ShapeNode>& nodes, int i, bool out)
{
    return (nodes[out ? next(nodes, i) : previous(nodes, i)].at - nodes[i].at) * k_newHandle;
}

/**
 * \brief @p node's other handle put where its type says, after the one @p movedOut names was moved: on one line
 *        through the node, for a Smooth one as long as it was, for a Symmetric one as long as the moved one. In
 *        pixels, where "on one line" and "as long" are what the eye sees.
 */
void keepType(ShapeNode& node, bool movedOut, const QRectF& box)
{
    if (node.type == ShapeNode::Type::Corner)
        return;
    const QPointF moved = toPixels(movedOut ? node.out : node.in, box);
    QPointF&      other = movedOut ? node.in : node.out;
    const qreal   lm    = length(moved);
    if (lm <= 0.0)
        return;
    const qreal lo = node.type == ShapeNode::Type::Symmetric ? lm : length(toPixels(other, box));
    other          = fromPixels(-moved / lm * lo, box);
}

}  // namespace

QList<ShapeNode> checked(const QList<ShapeNode>& before, const QList<ShapeNode>& after, const Rules& rules)
{
    return after.size() >= rules.minNodes ? after : before;
}

QPointF inBox(const QPointF& p, const QRectF& box)
{
    return {box.left() + p.x() * box.width(), box.top() + p.y() * box.height()};
}

QPointF relativeTo(const QPointF& p, const QRectF& box)
{
    return {(p.x() - box.left()) / box.width(), (p.y() - box.top()) / box.height()};
}

QPointF toPixels(const QPointF& offset, const QRectF& box)
{
    return {offset.x() * box.width(), offset.y() * box.height()};
}

QPointF fromPixels(const QPointF& offset, const QRectF& box)
{
    return {offset.x() / box.width(), offset.y() / box.height()};
}

int segments(const QList<ShapeNode>& nodes, const Rules& rules)
{
    const int n = int(nodes.size());
    return rules.closed ? n : qMax(n - 1, 0);
}

QPainterPath path(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box)
{
    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    if (nodes.isEmpty())
        return p;
    p.moveTo(inBox(nodes.first().at, box));
    for (int i = 0; i < segments(nodes, rules); ++i) {
        const ShapeNode& a = nodes[i];
        const ShapeNode& b = nodes[next(nodes, i)];
        if (a.out.isNull() && b.in.isNull())
            p.lineTo(inBox(b.at, box));
        else
            p.cubicTo(inBox(a.at + a.out, box), inBox(b.at + b.in, box), inBox(b.at, box));
    }
    if (rules.closed)
        p.closeSubpath();
    return p;
}

QPolygonF polyline(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, int perSegment)
{
    QPolygonF out;
    if (nodes.isEmpty())
        return out;
    out.append(inBox(nodes.first().at, box));
    for (int i = 0; i < segments(nodes, rules); ++i) {
        const ShapeNode& a  = nodes[i];
        const ShapeNode& b  = nodes[next(nodes, i)];
        const QPointF    p0 = inBox(a.at, box), p1 = inBox(a.at + a.out, box), p2 = inBox(b.at + b.in, box),
                         p3 = inBox(b.at, box);
        if (!(a.out.isNull() && b.in.isNull()))
            for (int k = 1; k < perSegment; ++k) {
                const qreal t = qreal(k) / perSegment, u = 1.0 - t;
                out.append(p0 * (u * u * u) + p1 * (3.0 * u * u * t) + p2 * (3.0 * u * t * t) + p3 * (t * t * t));
            }
        out.append(p3);
    }
    return out;
}

QPointF segmentMiddle(const QList<ShapeNode>& nodes, int segment)
{
    return splitSegment(nodes[segment], nodes[next(nodes, segment)]).mid;
}

QList<ShapeNode> withNodeAdded(const QList<ShapeNode>& nodes, const Rules& rules, int segment)
{
    if (segment < 0 || segment >= segments(nodes, rules))
        return nodes;
    QList<ShapeNode> out = nodes;
    ShapeNode&       a   = out[segment];
    ShapeNode&       b   = out[next(out, segment)];
    const Split      half = splitSegment(a, b);
    a.out                 = half.aOut;
    b.in                  = half.bIn;
    // A cut curve leaves its new node with its handles on one line; a straight one with none.
    const ShapeNode::Type type = half.midIn.isNull() ? ShapeNode::Type::Corner : ShapeNode::Type::Smooth;
    out.insert(segment + 1, ShapeNode(half.mid, half.midIn, half.midOut, type));
    return checked(nodes, out, rules);
}

QList<ShapeNode> withNodeRemoved(const QList<ShapeNode>& nodes, const Rules& rules, int index)
{
    if (index < 0 || index >= nodes.size())
        return nodes;
    QList<ShapeNode> out = nodes;
    out.removeAt(index);
    return checked(nodes, out, rules);
}

QList<ShapeNode> withNodesMoved(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box,
                                const QList<int>& indices, const QPointF& by)
{
    if (box.isEmpty())
        return nodes;
    QList<ShapeNode> out  = nodes;
    const QPointF    step = fromPixels(by, box);
    for (int i : indices)
        if (i >= 0 && i < out.size())
            out[i].at += step;   // a handle is an offset from its node, so it comes along
    return checked(nodes, out, rules);
}

QList<ShapeNode> withHandlesGrown(const QList<ShapeNode>& nodes, const Rules& rules, int index)
{
    if (index < 0 || index >= nodes.size())
        return nodes;
    QList<ShapeNode> out  = nodes;
    ShapeNode&       node = out[index];
    // Only a straight side: a handle grown on a segment its other end already bends would bend it more.
    if (hasSide(nodes, rules, index, false) && node.in.isNull() && nodes[previous(nodes, index)].out.isNull())
        node.in = grownHandle(nodes, index, false);
    if (hasSide(nodes, rules, index, true) && node.out.isNull() && nodes[next(nodes, index)].in.isNull())
        node.out = grownHandle(nodes, index, true);
    return checked(nodes, out, rules);
}

QList<ShapeNode> withNodeType(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, int index,
                              ShapeNode::Type type)
{
    if (index < 0 || index >= nodes.size() || box.isEmpty())
        return nodes;
    QList<ShapeNode> out  = nodes;
    ShapeNode&       node = out[index];
    node.type             = type;
    if (type == ShapeNode::Type::Corner)
        return checked(nodes, out, rules);   // the handles stay where they are, now free of each other

    // An open path's end has no neighbour on its outer side: none to lean along, none to measure by.
    const QPointF prev = hasSide(nodes, rules, index, false)
                       ? toPixels(node.at - nodes[previous(nodes, index)].at, box) : QPointF();
    const QPointF following = hasSide(nodes, rules, index, true)
                            ? toPixels(nodes[next(nodes, index)].at - node.at, box) : QPointF();
    const QPointF in  = toPixels(node.in, box);
    const QPointF outH = toPixels(node.out, box);
    // Along the line the handles lean along, out from the in-handle to the out-handle; with no handles, along the
    // neighbours'.
    QPointF dir = outH - in;
    if (length(dir) <= 0.0)
        dir = prev + following;
    if (length(dir) <= 0.0)
        return checked(nodes, out, rules);
    dir /= length(dir);
    qreal li = length(in) > 0.0 ? length(in) : length(prev) * k_newHandle;
    qreal lo = length(outH) > 0.0 ? length(outH) : length(following) * k_newHandle;
    if (type == ShapeNode::Type::Symmetric)
        li = lo = (li + lo) / 2.0;
    node.in  = fromPixels(-dir * li, box);
    node.out = fromPixels(dir * lo, box);
    return checked(nodes, out, rules);
}

QList<Grip> grips(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, int picked)
{
    const int   n = int(nodes.size());
    QList<Grip> out;
    if (picked >= 0 && picked < n) {
        const ShapeNode& p  = nodes[picked];
        const QPointF    at = inBox(p.at, box);
        if (hasSide(nodes, rules, picked, false)) {
            const QPointF in = p.in.isNull() ? grownHandle(nodes, picked, false) : p.in;
            out.append({Part::InHandle, picked, inBox(p.at + in, box), at});
        }
        if (hasSide(nodes, rules, picked, true)) {
            const QPointF outH = p.out.isNull() ? grownHandle(nodes, picked, true) : p.out;
            out.append({Part::OutHandle, picked, inBox(p.at + outH, box), at});
        }
    }
    for (int i = 0; i < n; ++i)
        out.append({Part::Node, i, inBox(nodes[i].at, box), {}});
    for (int i = 0; i < segments(nodes, rules); ++i)
        out.append({Part::AddNode, i, inBox(segmentMiddle(nodes, i), box), {}});
    return out;
}

QList<ShapeNode> dragged(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, Part part, int index,
                         const QPointF& to)
{
    if (index < 0 || index >= nodes.size() || box.isEmpty())
        return nodes;
    QList<ShapeNode> out  = nodes;
    ShapeNode&       node = out[index];
    const QPointF    rel  = relativeTo(to, box);
    switch (part) {
    case Part::Node:
        node.at = rel;   // its handles are offsets, so they come along
        break;
    case Part::InHandle:
        node.in = rel - node.at;
        keepType(node, false, box);
        break;
    case Part::OutHandle:
        node.out = rel - node.at;
        keepType(node, true, box);
        break;
    case Part::AddNode:
        break;   // a press adds the node (withNodeAdded()); the drag is that node's
    }
    return checked(nodes, out, rules);
}

namespace {

//! Points nearer than this (pixels) are one: a chord of no length leaves the fit's parameters undefined.
constexpr qreal k_fitSamePoint = 1e-6;
//! How sharply the outline must turn at a point for a corner to stay there, in radians (60 degrees).
constexpr qreal k_fitCornerTurn = M_PI / 3.0;
//! How far to each side of a point its turn is measured, in tolerances: past a flattened round's own small turns.
constexpr qreal k_fitCornerReach = 4.0;
//! Newton-Raphson passes that re-place the points' parameters before a segment is split (Schneider's).
constexpr int k_fitPasses = 4;
//! Within this many times the squared tolerance, re-placing the parameters is tried before splitting (Schneider's).
constexpr qreal k_fitPassError = 4.0;
//! A handle shorter than this fraction of its chord is no fit (Schneider's): see leastSquares().
constexpr qreal k_fitShortHandle = 1e-6;
//! The fewest nodes a closed outline keeps.
constexpr int k_fitFewestNodes = 3;

struct Cubic
{
    QPointF p0, p1, p2, p3;
};

qreal dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

QPointF unit(const QPointF& v)
{
    const qreal l = length(v);
    return l > 0.0 ? v / l : QPointF();
}

QPointF pointAt(const Cubic& c, qreal t)
{
    const qreal u = 1.0 - t;
    return c.p0 * (u * u * u) + c.p1 * (3.0 * u * u * t) + c.p2 * (3.0 * u * t * t) + c.p3 * (t * t * t);
}

//! The parameters of @p d's points from @p first to @p last by the length along them: 0 at the first, 1 at the last.
QList<qreal> chordParameters(const QList<QPointF>& d, int first, int last)
{
    QList<qreal> u{0.0};
    for (int i = first + 1; i <= last; ++i)
        u.append(u.last() + length(d[i] - d[i - 1]));
    const qreal whole = u.last();
    for (qreal& t : u)
        t /= whole;
    return u;
}

//! The cubic from d[first] along @p t1 to d[last] along @p t2 (pointing back) that best fits the points between.
Cubic leastSquares(const QList<QPointF>& d, int first, int last, const QList<qreal>& u, const QPointF& t1,
                   const QPointF& t2)
{
    const QPointF p0 = d[first], p3 = d[last];
    qreal         c00 = 0.0, c01 = 0.0, c11 = 0.0, x0 = 0.0, x1 = 0.0;
    for (int i = first; i <= last; ++i) {
        const qreal   t = u[i - first], s = 1.0 - t;
        const qreal   b0 = s * s * s, b1 = 3.0 * s * s * t, b2 = 3.0 * s * t * t, b3 = t * t * t;
        const QPointF a0 = t1 * b1, a1 = t2 * b2;
        c00 += dot(a0, a0);
        c01 += dot(a0, a1);
        c11 += dot(a1, a1);
        const QPointF rest = d[i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
        x0 += dot(a0, rest);
        x1 += dot(a1, rest);
    }
    const qreal det   = c00 * c11 - c01 * c01;
    const qreal chord = length(p3 - p0);
    qreal       left  = det != 0.0 ? (x0 * c11 - x1 * c01) / det : 0.0;
    qreal       right = det != 0.0 ? (c00 * x1 - c01 * x0) / det : 0.0;
    // Too short a handle, or two that cross over each other along the chord (few points between, the system near
    // singular: Paper.js's check), are no fit: each a third of the chord, as with no points between.
    const QPointF line = p3 - p0;
    if (left < k_fitShortHandle * chord || right < k_fitShortHandle * chord
        || dot(t1 * left, line) - dot(t2 * right, line) > chord * chord)
        left = right = chord / 3.0;
    return {p0, p0 + t1 * left, p3 + t2 * right, p3};
}

//! The largest squared distance of the points from @p c at their parameters, and the point where it is.
std::pair<qreal, int> worst(const QList<QPointF>& d, int first, int last, const Cubic& c, const QList<qreal>& u)
{
    qreal most  = 0.0;
    int   where = (first + last) / 2;
    for (int i = first + 1; i < last; ++i) {
        const QPointF off = pointAt(c, u[i - first]) - d[i];
        if (const qreal e = dot(off, off); e > most) {
            most  = e;
            where = i;
        }
    }
    return {most, where};
}

//! Each parameter moved by a Newton-Raphson step towards the curve's point nearest its point.
QList<qreal> replaced(const QList<QPointF>& d, int first, const Cubic& c, QList<qreal> u)
{
    const QPointF q1[] = {(c.p1 - c.p0) * 3.0, (c.p2 - c.p1) * 3.0, (c.p3 - c.p2) * 3.0};
    const QPointF q2[] = {(q1[1] - q1[0]) * 2.0, (q1[2] - q1[1]) * 2.0};
    for (int k = 0; k < u.size(); ++k) {
        const qreal   t = u[k], s = 1.0 - t;
        const QPointF off = pointAt(c, t) - d[first + k];
        const QPointF v1  = q1[0] * (s * s) + q1[1] * (2.0 * s * t) + q1[2] * (t * t);
        const QPointF v2  = q2[0] * s + q2[1] * t;
        const qreal   den = dot(v1, v1) + dot(off, v2);
        if (den != 0.0)
            u[k] = t - dot(off, v1) / den;
    }
    return u;
}

//! Cubics through d[first..last], leaving along @p t1 and arriving along @p t2 (pointing back), appended to @p out.
void fitSpan(const QList<QPointF>& d, int first, int last, const QPointF& t1, const QPointF& t2, qreal error,
             QList<Cubic>& out)
{
    if (last - first == 1) {
        const qreal third = length(d[last] - d[first]) / 3.0;
        out.append({d[first], d[first] + t1 * third, d[last] + t2 * third, d[last]});
        return;
    }
    QList<qreal> u    = chordParameters(d, first, last);
    Cubic        c    = leastSquares(d, first, last, u, t1, t2);
    auto [most, here] = worst(d, first, last, c, u);
    if (most < error) {
        out.append(c);
        return;
    }
    if (most < error * k_fitPassError)
        for (int pass = 0; pass < k_fitPasses; ++pass) {
            u                    = replaced(d, first, c, u);
            c                    = leastSquares(d, first, last, u, t1, t2);
            std::tie(most, here) = worst(d, first, last, c, u);
            if (most < error) {
                out.append(c);
                return;
            }
        }
    QPointF across = unit(d[here - 1] - d[here + 1]);
    if (across.isNull())
        across = unit(d[here - 1] - d[here]);
    fitSpan(d, first, here, t1, across, error, out);
    fitSpan(d, here, last, -across, t2, error, out);
}

//! The point of the closed @p pts at least @p reach along it from point @p i, going by @p step (1 or -1).
QPointF reached(const QList<QPointF>& pts, int i, int step, qreal reach)
{
    const int n    = int(pts.size());
    qreal     gone = 0.0;
    int       j    = i;
    for (int k = 1; k < n && gone < reach; ++k) {
        const int to = (j + step + n) % n;
        gone += length(pts[to] - pts[j]);
        j = to;
    }
    return pts[j];
}

//! How far the closed @p pts turns at point @p i, measured to the points @p reach away each side, in radians.
qreal turnAt(const QList<QPointF>& pts, int i, qreal reach)
{
    const QPointF a = pts[i] - reached(pts, i, -1, reach), b = reached(pts, i, 1, reach) - pts[i];
    return std::abs(std::atan2(a.x() * b.y() - a.y() * b.x(), dot(a, b)));
}

//! The points of the closed @p pts within @p reach along it each side of @p i, @p i not among them.
QList<int> around(const QList<QPointF>& pts, int i, qreal reach)
{
    const int  n = int(pts.size());
    QList<int> out;
    for (const int step : {-1, 1}) {
        qreal gone = 0.0;
        for (int k = 1, j = i; k < n; ++k) {
            const int to = (j + step + n) % n;
            gone += length(pts[to] - pts[j]);
            if (gone > reach)
                break;
            out.append(to);
            j = to;
        }
    }
    return out;
}

//! The length along the closed @p pts from point @p from forwards to point @p to.
qreal along(const QList<QPointF>& pts, int from, int to)
{
    const int n = int(pts.size());
    qreal     l = 0.0;
    for (int j = from;;) {
        const int k = (j + 1) % n;
        l += length(pts[k] - pts[j]);
        if (k == to)
            return l;
        j = k;
    }
}

} // namespace

QList<ShapeNode> fitted(const QPolygonF& outline, const QRectF& box, qreal tolerance)
{
    if (box.isEmpty() || tolerance <= 0.0)
        return {};
    // Its points, each apart from the one before, round to the first.
    QList<QPointF> pts;
    for (const QPointF& p : outline)
        if (pts.isEmpty() || length(p - pts.last()) > k_fitSamePoint)
            pts.append(p);
    while (pts.size() > 1 && length(pts.last() - pts.first()) <= k_fitSamePoint)
        pts.removeLast();
    const int n = int(pts.size());
    if (n < k_fitFewestNodes)
        return {};

    // The breaks, in order round it: corners where it turns sharply (the sharpest of those near each other)...
    const qreal  reach = k_fitCornerReach * tolerance;
    QList<qreal> turn(n);
    for (int i = 0; i < n; ++i)
        turn[i] = turnAt(pts, i, reach);
    QList<std::pair<int, bool>> breaks;   // a point, and whether it is a corner
    for (int i = 0; i < n; ++i) {
        if (turn[i] <= k_fitCornerTurn)
            continue;
        const QList<int> near = around(pts, i, reach);
        if (std::all_of(near.cbegin(), near.cend(),
                        [&](int j) { return j < i ? turn[j] < turn[i] : turn[j] <= turn[i]; }))
            breaks.append({i, true});
    }
    // ...and as many smooth ones as make three, each half way along the longest stretch between two.
    if (breaks.isEmpty())
        breaks.append({0, false});
    while (breaks.size() < k_fitFewestNodes) {
        int   longest = -1;
        qreal most    = 0.0;
        for (int k = 0; k < breaks.size(); ++k) {
            const int from = breaks[k].first, to = breaks[(k + 1) % breaks.size()].first;
            if ((from + 1) % n == to)
                continue;   // no point between
            if (const qreal l = along(pts, from, to); l > most) {
                most    = l;
                longest = k;
            }
        }
        if (longest < 0)
            return {};
        const int from = breaks[longest].first, to = breaks[(longest + 1) % breaks.size()].first;
        int       mid  = (from + 1) % n;
        for (qreal gone = length(pts[mid] - pts[from]); gone < most / 2.0 && (mid + 1) % n != to;
             mid        = (mid + 1) % n)
            gone += length(pts[(mid + 1) % n] - pts[mid]);
        breaks.insert(longest + 1, {mid, false});
    }

    // The points unrolled from the first break round to it again; the breaks as places in them.
    const int      start = breaks.first().first;
    QList<QPointF> d;
    for (int k = 0; k <= n; ++k)
        d.append(pts[(start + k) % n]);
    QList<int> place;
    for (const auto& b : std::as_const(breaks))
        place.append((b.first - start + n) % n);
    place.append(n);
    const auto before = [&](int i) { return d[i == 0 ? n - 1 : i - 1]; };
    const auto after  = [&](int i) { return d[i == n ? 1 : i + 1]; };

    // Each stretch between two breaks: leaving and arriving along the sides at a corner, across a smooth one.
    QList<Cubic> cubics;
    const qreal  error = tolerance * tolerance;
    for (int k = 0; k + 1 < place.size(); ++k) {
        const int     from = place[k], to = place[k + 1];
        const bool    sharpFrom = breaks[k].second, sharpTo = breaks[(k + 1) % breaks.size()].second;
        const QPointF t1 = unit(sharpFrom ? d[from + 1] - d[from] : after(from) - before(from));
        const QPointF t2 = unit(sharpTo ? d[to - 1] - d[to] : before(to) - after(to));
        fitSpan(d, from, to, t1, t2, error, cubics);
    }

    QList<ShapeNode> nodes;
    for (const Cubic& c : std::as_const(cubics))
        nodes.append(ShapeNode(relativeTo(c.p0, box), {}, fromPixels(c.p1 - c.p0, box)));
    for (int k = 0; k < cubics.size(); ++k)
        nodes[(k + 1) % nodes.size()].in = fromPixels(cubics[k].p2 - cubics[k].p3, box);
    for (ShapeNode& node : nodes)
        node.type = typeOf(node, box);
    return nodes;
}

ShapeNode::Type typeOf(const ShapeNode& node, const QRectF& box)
{
    const QPointF in  = toPixels(node.in, box);
    const QPointF out = toPixels(node.out, box);
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

}  // namespace Bezier
