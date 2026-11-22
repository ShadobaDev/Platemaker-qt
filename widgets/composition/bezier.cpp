#include "bezier.hpp"

#include <cmath>

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
