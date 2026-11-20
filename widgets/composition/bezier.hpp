/**
 * \file bezier.hpp
 * \brief A path of Bézier nodes, open or closed, and every edit made to it, knowing nothing of what it outlines.
 *
 * The nodes are ShapeNode, in fractions of a box the caller gives (a balloon's body, a text block), so the box
 * resized scales the path. The caller also gives its Rules. **Every edit goes through checked()**, which refuses
 * the result when it breaks them: a limit a caller needs one day is a field of Rules checked there, the
 * operations untouched.
 */

#ifndef BEZIER_HPP
#define BEZIER_HPP

#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>

#include "propertygroups.hpp"   // ShapeNode

namespace Bezier {

//! What a caller's path must stay.
struct Rules
{
    int  minNodes = 2;        //!< Never fewer nodes than this.
    bool closed   = false;    //!< The last node joins the first; an open path has no segment after its last.
};

//! @p after when it keeps @p rules, else @p before: the one gate every edit goes through.
[[nodiscard]] QList<ShapeNode> checked(const QList<ShapeNode>& before, const QList<ShapeNode>& after,
                                       const Rules& rules);

//! A node, or a node plus a handle (@p p), as a point of @p box.
[[nodiscard]] QPointF inBox(const QPointF& p, const QRectF& box);
//! inBox()'s inverse.
[[nodiscard]] QPointF relativeTo(const QPointF& p, const QRectF& box);
//! A handle (an offset in fractions of @p box) in @p box's pixels, where lengths and angles mean what they look.
[[nodiscard]] QPointF toPixels(const QPointF& offset, const QRectF& box);
//! toPixels()'s inverse.
[[nodiscard]] QPointF fromPixels(const QPointF& offset, const QRectF& box);

//! How many segments @p nodes have: one after each node, but after an open path's last.
[[nodiscard]] int segments(const QList<ShapeNode>& nodes, const Rules& rules);

/**
 * \brief The nodes joined by cubic segments in @p box, each leaving a node along its out-handle and arriving
 *        along the next one's in-handle; straight where neither has one. Non-zero fill; closed if the rules say.
 */
[[nodiscard]] QPainterPath path(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box);

/**
 * \brief path() as points in @p box: each node, and a curved segment's @p perSegment − 1 points between, evenly in
 *        its parameter. A straight segment is its two ends. Closed, it comes back to the first node.
 */
[[nodiscard]] QPolygonF polyline(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, int perSegment);

//! The point halfway along segment @p segment (de Casteljau), in fractions: where its "+" is.
[[nodiscard]] QPointF segmentMiddle(const QList<ShapeNode>& nodes, int segment);

//! @p nodes with a node splitting segment @p segment in half, so the path keeps its shape.
[[nodiscard]] QList<ShapeNode> withNodeAdded(const QList<ShapeNode>& nodes, const Rules& rules, int segment);
//! @p nodes without node @p index, unless the rules forbid it.
[[nodiscard]] QList<ShapeNode> withNodeRemoved(const QList<ShapeNode>& nodes, const Rules& rules, int index);
//! @p nodes with nodes @p indices moved @p by, in @p box's pixels, their handles along.
[[nodiscard]] QList<ShapeNode> withNodesMoved(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box,
                                              const QList<int>& indices, const QPointF& by);
/**
 * \brief @p nodes with node @p index given the handles it lacks on a straight side, a third of the way to the
 *        neighbour on that side and on the line to it, so the path stays as it was.
 */
[[nodiscard]] QList<ShapeNode> withHandlesGrown(const QList<ShapeNode>& nodes, const Rules& rules, int index);
/**
 * \brief @p nodes with node @p index made a @p type node, its handles in @p box brought into line: a Smooth or
 *        Symmetric node's handles on one line through it, the line they lean along (or its neighbours' when it
 *        has none), a missing one drawn a third of the way to its neighbour.
 */
[[nodiscard]] QList<ShapeNode> withNodeType(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box,
                                            int index, ShapeNode::Type type);

//! What a press on the path's edit handles takes.
enum class Part {
    Node,        //!< Node `index`.
    AddNode,     //!< The middle of segment `index`: a press there adds a node.
    InHandle,    //!< Node `index`'s handle its segment from the previous node arrives along.
    OutHandle,   //!< ...and the one its segment to the next node leaves along.
};

//! One edit handle, in @p box's coordinates; `origin` is a handle's node.
struct Grip
{
    Part    part;
    int     index;
    QPointF at;
    QPointF origin;
};

/**
 * \brief The picked node's handles, then every node, then a "+" halfway along each segment: the order a press is
 *        answered in, so a handle wins over the node it leaves. A handle the node lacks is offered where it would
 *        grow from, and becomes its own when dragged. An open path's ends offer no handle outside the path.
 */
[[nodiscard]] QList<Grip> grips(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, int picked);

//! @p nodes with node @p index's @p part dragged to @p to, in @p box's coordinates. AddNode moves nothing.
[[nodiscard]] QList<ShapeNode> dragged(const QList<ShapeNode>& nodes, const Rules& rules, const QRectF& box, Part part,
                                       int index, const QPointF& to);

/**
 * \brief Which type @p node reads as from its handles in @p box: Smooth when they point opposite ways along one
 *        line, Symmetric when they are as long as each other too, else Corner.
 */
[[nodiscard]] ShapeNode::Type typeOf(const ShapeNode& node, const QRectF& box);

}  // namespace Bezier

#endif // BEZIER_HPP
