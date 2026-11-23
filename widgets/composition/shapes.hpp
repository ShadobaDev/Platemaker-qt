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

#include "composition.hpp"
#include "handles.hpp"
#include "names.hpp"

namespace Geometry {

//! The fewest lobes a Cloud has (three already close it), and the most, past what reads as a cloud.
constexpr int k_minLobes    = 3;
constexpr int k_maxLobes    = 40;
//! A lobe's radius, smallest and largest, in % of the shorter half-axis: at 100 % they meet in the middle.
constexpr int k_minLobeSize = 5;
constexpr int k_maxLobeSize = 100;

//! A shape's handles (shapeHandles()), told apart by Handle::part.
enum class ShapePart {
    Radius,      //!< The corner radius: a dot inside the first corner's arc.
    Vertex,      //!< A Polygon's or a Curve's node `index`.
    AddVertex,   //!< The middle of the edge after node `index`: a press there adds a node.
    InHandle,    //!< A Curve's node `index`: the handle its segment from the previous node arrives along.
    OutHandle,   //!< ...and the one its segment to the next node leaves along.
    Depth,       //!< The edge's depth: an arrow at its first valley, any kind with an edge.
};

/**
 * \brief One kind: everything about it that is not the composition's own state.
 *
 * Both functions take the shape group and the balloon's rectangle (balloonRect()), and nothing else of
 * the composition: the stroke and the author's padding are applied by the caller, the same way for every
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
    //! The sections of ShapeProperties it reads (ShapeProperties::sections()), whose rows the Shape panel shows.
    Fields::SectionNames sections;
    /**
     * The outline in @p body, any shape, concave included: a tail finds where it leaves the balloon by
     * walking from its tip towards the centre (silhouette.cpp). nullptr for `None`, which has none.
     */
    QPainterPath (*outline)(const ShapeProperties& shape, const QRectF& body);
    /**
     * How far a flash's core is stretched on this kind while the balloon leaves it on *Auto*, in %
     * (`StyleProperties::flashCoreStretch`). Round shapes and those of five sides or more point from the
     * centre (0); long straight sides want the line (100), so their strokes stand square to them. A Box
     * is a speech balloon as often as a caption box, so it takes the middle.
     */
    int flashStretch;
    /**
     * The handles edit mode offers on an outline in @p body; nullptr for a kind with none, which is not
     * offered edit mode. @p clearance keeps the radius dot that far in from its corner's arc, so it never
     * sits on the corner's own handle. @p picked is the node picked in edit mode, -1 for none: a Curve
     * shows that one's two handles.
     */
    QList<Handle> (*handles)(const ShapeProperties& shape, const QRectF& body, qreal clearance,
                                  int picked) = nullptr;
    /**
     * @p shape with @p handle dragged to @p to, both in the balloon's coordinates. A node may land past
     * the body (outside 0..1); growing the box to it is the caller's.
     */
    ShapeProperties (*dragHandle)(const ShapeProperties& shape, const QRectF& body, const Handle& handle,
                                  const QPointF& to, qreal clearance) = nullptr;
};

//! The entry for @p kind. Every kind up to `ShapeProperties::k_lastKind` has one.
[[nodiscard]] const ShapeInfo& shapeInfo(ShapeProperties::Kind kind);

/**
 * \brief One edge modulation: its names, and the outline with its teeth pushed in.
 *
 * Adding one is a value in `ShapeProperties::Edge` and an entry here (shapes.cpp): its function walks the
 * bare outline (EdgeWalk) and moves it in by at most `teeth.depth`, so the text area's inset (edgeInset())
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
 * \brief Every handle edit mode offers on @p shape in @p body: its kind's own (ShapeInfo::handles), then
 *        its edge's depth. What the controls ask, so an Ellipse with an edge is edited too.
 */
[[nodiscard]] QList<Handle> shapeHandles(const ShapeProperties& shape, const QRectF& body,
                                              qreal clearance, int picked);

//! @p shape with @p handle dragged to @p to: shapeHandles()' inverse, the kind's or the edge's.
[[nodiscard]] ShapeProperties dragShapeHandle(const ShapeProperties& shape, const QRectF& body,
                                              const Handle& handle, const QPointF& to, qreal clearance);

//! Whether @p shape offers edit mode at all: a kind with handles, or an edge.
[[nodiscard]] bool hasShapeHandles(const ShapeProperties& shape);
//! Whether @p mask offers edit mode: a region's shape with handles, or a fade's.
[[nodiscard]] bool hasShapeHandles(const Mask& mask);

//! @p shape with @p from's edge: what a picked tile keeps, since a template does not set the edge.
[[nodiscard]] ShapeProperties withEdgeOf(ShapeProperties shape, const ShapeProperties& from);

/**
 * \brief A ready-made outline: a value of the shape group, as the picker offers it.
 *
 * A composition never stores which template it came from, only the shape it has. A tile is current when the
 * composition's shape equals its template (templateOf()). An outline edited off every template is still its
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

//! The fewest and the most points regularNodes() makes a shape of.
constexpr int k_minRegularPoints = 3;
constexpr int k_maxRegularPoints = 64;

/**
 * \brief A Polygon's nodes for a regular polygon or a star, stretched to fill the unit box, so the balloon's
 *        box frames it.
 *
 * @p points corners round the centre, the first on top, turned @p turn degrees clockwise. With @p inner
 * under 1, a node between each two at @p inner of their radius: a star. @p irregularity (0..1) shortens each
 * node by up to half of it and moves it along by up to a quarter of its share of the round, dealt from
 * @p seed: long spikes and short stubs, unevenly spaced. Rounded to the decimals a node is saved with.
 */
[[nodiscard]] QList<ShapeNode> regularNodes(int points, qreal inner, qreal turn, qreal irregularity, quint32 seed);

//! Whether @p shape speaks: its template's answer, or its kind's when it matches none.
[[nodiscard]] bool shapeSpeaks(const ShapeProperties& shape);

//! The largest corner radius a balloon of @p size shows: half the shorter side, which makes the ends round.
[[nodiscard]] qreal cornerRadiusLimit(const QSizeF& size);

/**
 * @p shape with a node added in the middle of the edge after node @p edge: the "+". A Curve's segment is
 * split there (de Casteljau), so the outline keeps its shape.
 */
[[nodiscard]] ShapeProperties withVertexAdded(const ShapeProperties& shape, int edge);

/**
 * \brief @p shape with node @p index given the handles it lacks on a straight side, a third of the way to
 *        the neighbour on that side and on the line to it, so the outline stays as it was: *Add handles*.
 *        Unchanged when it lacks none there.
 */
[[nodiscard]] ShapeProperties withHandlesGrown(const ShapeProperties& shape, int index);

/**
 * \brief How far a Polygon's or a Curve's outline reaches, as fractions of @p body: (0, 0, 1, 1) when it
 *        fills it edge to edge. What the box is fitted to when a node is dragged.
 *
 * A Polygon by its nodes, since its rounded corners stay inside them; a Curve by the outline drawn, since a
 * curve may bulge past its nodes. Empty for any other kind, and for a body with no area.
 */
[[nodiscard]] QRectF outlineExtent(const ShapeProperties& shape, const QRectF& body);

//! @p shape with nodes @p indices moved @p by, in @p body's pixels, with their handles; the others stay.
[[nodiscard]] ShapeProperties withNodesMoved(const ShapeProperties& shape, const QRectF& body,
                                             const QList<int>& indices, const QPointF& by);

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
 * \brief The Curve that draws @p outline (its first subpath) in @p body: *Convert to Curve*.
 *
 * Each line and cubic of the path becomes a segment, so the outline is the same; a node whose handles
 * lie on one line is Smooth, and Symmetric when they are as long as each other too.
 */
[[nodiscard]] ShapeProperties curveOf(const QPainterPath& outline, const QRectF& body);

/**
 * \brief The balloon's own rectangle inside the box — the box, less room for the stroke.
 *
 * The box *is* the balloon. It used to double as the composition's whole extent, with a fixed 18 % of its
 * height reserved at the bottom for the tail to live in — which is what limited a tail to the bottom
 * edge and to inside the box. The drawn extent is computed instead (Geometry::bounds()).
 */
[[nodiscard]] QRectF balloonRect(const Balloon& b, const Body& body);

//! The point @p anchor (a fraction of @p rect, Tail::anchor) stands for in @p rect.
[[nodiscard]] QPointF anchorAt(const QRectF& rect, const QPointF& anchor);

//! What @p b's fill is laid over (a fade, its handles): every body's balloonRect() as one.
[[nodiscard]] QRectF fillBox(const Balloon& b);

//! @p body's own outline in @p rect (balloonRect()) — the shape alone, no tails, its edge dealt by @p seed.
[[nodiscard]] QPainterPath outlineOf(const Body& body, const QRectF& rect, quint32 seed);

//! Where @p mask hides: its regions' outlines (outlineOf()) in the composition's frame, their edge dealt by @p seed.
[[nodiscard]] QPainterPath maskOutline(const Mask& mask, quint32 seed);
//! The box round @p mask's regions, in the composition's frame: what its fade is laid on, what its grips resize.
[[nodiscard]] QRectF maskBox(const Mask& mask);

//! @p mask's fade in the composition's frame (its fractions laid on maskBox()); none without one.
[[nodiscard]] std::optional<QLineF> maskFade(const Mask& mask);
/**
 * \brief Whether @p c's mask hides it at @p at, in the composition's frame, as the eye sees it: at least half
 *        hidden there by the smooth fade (a pattern's marks not counted, so between two dots counts too), and @p c
 *        draws something there (partAt(), or its picture). What a click on the strip picks the mask by.
 */
[[nodiscard]] bool maskHidesAt(const Composition& c, const QPointF& at);

}  // namespace Geometry

#endif // SHAPES_HPP
