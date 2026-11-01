/**
 * \file shapes.hpp
 * \brief Every balloon shape, one table entry each: its names, its outline and where its lettering
 *        fits; and the templates the picker offers.
 *
 * **Adding a kind is a value in `ShapeProperties::Kind` and an entry here** (shapes.cpp), with its path
 * function, its text-area function and its named constants beside it. Everything else asks the table:
 * the file names (shapeName), the titles (shapeTitle), the silhouette (silhouette.cpp) and the text
 * area (lettering.cpp). **Adding a ready-made outline is a template**: a row of the second table, a
 * value of ShapeProperties with a title and whether it speaks. The picker's tiles are those rows.
 */

#ifndef SHAPES_HPP
#define SHAPES_HPP

#include <QPainterPath>
#include <QRectF>

#include "objectrecord.hpp"

namespace Geometry {

//! The fewest lobes a Cloud has (three already close it), and the most, past what reads as a cloud.
constexpr int k_minLobes    = 3;
constexpr int k_maxLobes    = 40;
//! A lobe's radius, smallest and largest, in % of the shorter half-axis: at 100 % they meet in the middle.
constexpr int k_minLobeSize = 5;
constexpr int k_maxLobeSize = 100;

/**
 * @brief A point a shape offers to be dragged in edit mode, in the balloon's own coordinates.
 */
struct ShapeHandle
{
    enum class Role {
        Radius,      //!< The corner radius: a dot inside the first corner's arc.
        Vertex,      //!< A Polygon's or a Curve's node `index`.
        AddVertex,   //!< The middle of the edge after node `index`: a press there adds a node.
        InHandle,    //!< A Curve's node `index`: the handle its segment from the previous node arrives along.
        OutHandle,   //!< ...and the one its segment to the next node leaves along.
        Depth,       //!< The edge's depth: an arrow at its first valley, any kind with an edge.
        FadeFrom,    //!< The fill's fade, side `index` (left, right, top, bottom): where it starts.
        FadeTo,      //!< ...and where the pattern is gone (fadeHandles(), styles.hpp).
    };
    Role    role  = Role::Vertex;
    int     index = 0;
    QPointF at;
    QPointF axis;     //!< For the radius, the unit direction into its corner; for the depth, inwards; for a
                      //!< fade's dot, outwards along its arm.
    QPointF origin;   //!< For a Curve's handle: its node, which the line to it is drawn from; for a fade's
                      //!< dot, the cross's centre.
};

/**
 * @brief One kind: everything about it that is not the record's own state.
 *
 * Both functions take the shape group and the balloon's rectangle (balloonRect()), and nothing else of
 * the record: the stroke and the author's padding are applied by the caller, the same way for every
 * shape.
 */
struct ShapeInfo
{
    ShapeProperties::Kind kind;   //!< Which shape this entry is; the table is checked against it.
    const char*           name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*           title;  //!< Shown on screen, translated through QObject::tr().
    /**
     * Whether an outline of this kind that matches no template speaks (shapeSpeaks()). A template says
     * it for itself: a speech balloon and a caption box are both Boxes.
     */
    bool speaks;
    //! Whether the kind reads `cornerRadius`. The Shape panel shows the row only then.
    bool rounded;
    /**
     * The outline in @p body, any shape, concave included: a tail finds where it leaves the balloon by
     * walking from its tip towards the centre (silhouette.cpp). nullptr for `None`, which has none.
     */
    QPainterPath (*outline)(const ShapeProperties& shape, const QRectF& body);
    /**
     * The largest axis-aligned rectangle the outline actually contains, before the stroke and the
     * padding are taken off. This is the half of a shape that takes the thought: it is what stops a
     * wide balloon spilling its words out between a burst's spikes or past a diamond's slope. A kind
     * with no formula for it (the Polygon, the Curve) computes it from its outline. An area, never a placement:
     * where the words sit in it is lettering.cpp's alone.
     */
    QRectF (*safeArea)(const ShapeProperties& shape, const QRectF& body);
    /**
     * How far a flash's core is stretched on this kind while the balloon leaves it on *Auto*, in %
     * (`StyleProperties::flashCoreStretch`). Round shapes and those of five sides or more point from the
     * centre (0); long straight sides want the line (100), so their strokes stand square to them. A Box
     * is a speech balloon as often as a caption box, so it takes the middle (PLAN-L B22).
     */
    int flashStretch;
    /**
     * The handles edit mode offers on an outline in @p body; nullptr for a kind with none, which is not
     * offered edit mode. @p clearance keeps the radius dot that far in from its corner's arc, so it never
     * sits on the corner's own handle. @p picked is the node picked in edit mode, -1 for none: a Curve
     * shows that one's two handles.
     */
    QList<ShapeHandle> (*handles)(const ShapeProperties& shape, const QRectF& body, qreal clearance,
                                  int picked) = nullptr;
    /**
     * @p shape with @p handle dragged to @p to, both in the balloon's coordinates. A node may land past
     * the body (outside 0..1); growing the box to it is the caller's (PLAN-L B10).
     */
    ShapeProperties (*dragHandle)(const ShapeProperties& shape, const QRectF& body, const ShapeHandle& handle,
                                  const QPointF& to, qreal clearance) = nullptr;
};

//! The entry for @p kind. Every kind up to `ShapeProperties::k_lastKind` has one.
[[nodiscard]] const ShapeInfo& shapeInfo(ShapeProperties::Kind kind);

/**
 * @brief One edge modulation (PLAN-L M11): its names, and the outline with its teeth pushed in.
 *
 * Adding one is a value in `ShapeProperties::Edge` and an entry here (shapes.cpp): its function walks the
 * bare outline (EdgeWalk) and moves it in by at most `edgeDepth`, so the text area's inset (edgeInset())
 * holds for every one.
 */
struct EdgeInfo
{
    ShapeProperties::Edge kind;
    const char*           name;    //!< Persisted. **Never changes** once saved.
    const char*           title;   //!< Shown on screen, translated through QObject::tr().
    //! @p outline with its edge modulated, seeded by @p seed; nullptr for None, which leaves it as it is.
    QPainterPath (*modulate)(const QPainterPath& outline, const ShapeProperties& shape, quint32 seed);
};

//! The entry for @p kind. Every kind up to `ShapeProperties::k_lastEdge` has one.
[[nodiscard]] const EdgeInfo& edgeInfo(ShapeProperties::Edge kind);

//! How far in from the outline @p shape's edge reaches: its depth, or 0 with no edge.
[[nodiscard]] qreal edgeInset(const ShapeProperties& shape);

/**
 * @brief Every handle edit mode offers on @p shape in @p body: its kind's own (ShapeInfo::handles), then
 *        its edge's depth. What the controls ask, so an Ellipse with an edge is edited too.
 */
[[nodiscard]] QList<ShapeHandle> shapeHandles(const ShapeProperties& shape, const QRectF& body,
                                              qreal clearance, int picked);

//! @p shape with @p handle dragged to @p to: shapeHandles()' inverse, the kind's or the edge's.
[[nodiscard]] ShapeProperties dragShapeHandle(const ShapeProperties& shape, const QRectF& body,
                                              const ShapeHandle& handle, const QPointF& to, qreal clearance);

//! Whether @p shape offers edit mode at all: a kind with handles, or an edge.
[[nodiscard]] bool hasShapeHandles(const ShapeProperties& shape);

//! @p shape with @p from's edge: what a picked tile keeps, since a template does not set the edge.
[[nodiscard]] ShapeProperties withEdgeOf(ShapeProperties shape, const ShapeProperties& from);

/**
 * @brief A ready-made outline: a value of the shape group, as the picker offers it.
 *
 * A record never stores which template it came from, only the shape it has. A tile is current when the
 * record's shape equals its template (templateOf()). An outline edited off every template is still its
 * kind, and matches none.
 */
struct ShapeTemplate
{
    const char*     id;      //!< For code and tests to name one by (shapeTemplate()). Not persisted.
    const char*     title;   //!< Shown on screen, translated through QObject::tr().
    ShapeProperties shape;
    //! Someone is talking, so a placed one gets a tail. A caption, a banner or a scroll is narration:
    //! it has no speaker to point at, and a tail there is one the author would have to turn off.
    bool            speaks;
};

//! Every template, in the picker's order.
[[nodiscard]] const QList<ShapeTemplate>& shapeTemplates();

//! The template called @p id. Asserts on an unknown one, and then returns the first.
[[nodiscard]] const ShapeTemplate& shapeTemplate(const char* id);

//! The template @p shape is, its edge aside, or nullptr when it matches none.
[[nodiscard]] const ShapeTemplate* templateOf(const ShapeProperties& shape);

//! Whether @p shape speaks: its template's answer, or its kind's when it matches none.
[[nodiscard]] bool shapeSpeaks(const ShapeProperties& shape);

//! The largest corner radius a balloon of @p size shows: half the shorter side, which makes the ends round.
[[nodiscard]] qreal cornerRadiusLimit(const QSizeF& size);

/**
 * @p shape with a node added in the middle of the edge after node @p edge: the "+". A Curve's segment is
 * split there (de Casteljau), so the outline keeps its shape.
 */
[[nodiscard]] ShapeProperties withVertexAdded(const ShapeProperties& shape, int edge);

//! @p shape without node @p index; unchanged when that would leave fewer than three.
[[nodiscard]] ShapeProperties withVertexRemoved(const ShapeProperties& shape, int index);

/**
 * @p shape with node @p index made a @p type node, its handles in @p body brought into line: a Smooth or
 * Symmetric node's handles are put on one line through it, the line they lean along (or its neighbours'
 * when it has none), and a missing one is drawn a third of the way to its neighbour.
 */
[[nodiscard]] ShapeProperties withNodeType(const ShapeProperties& shape, const QRectF& body, int index,
                                           ShapeNode::Type type);

/**
 * @brief The Curve that draws @p outline (its first subpath) in @p body: *Convert to Curve*.
 *
 * Each line and cubic of the path becomes a segment, so the outline is the same; a node whose handles
 * lie on one line is Smooth, and Symmetric when they are as long as each other too.
 */
[[nodiscard]] ShapeProperties curveOf(const QPainterPath& outline, const QRectF& body);

/**
 * @brief The balloon's own rectangle inside the box — the box, less room for the stroke.
 *
 * The box *is* the balloon. It used to double as the record's whole extent, with a fixed 18 % of its
 * height reserved at the bottom for the tail to live in — which is what limited a tail to the bottom
 * edge and to inside the box. The drawn extent is computed instead (Geometry::bounds()).
 */
[[nodiscard]] QRectF balloonRect(const ObjectRecord& a);

//! The balloon's own outline — the shape alone, no tails. Empty for a shapeless record.
[[nodiscard]] QPainterPath outlineOf(const ObjectRecord& a, const QRectF& body);

}  // namespace Geometry

#endif // SHAPES_HPP
