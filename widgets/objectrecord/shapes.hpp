/**
 * \file shapes.hpp
 * \brief Every balloon shape, one table entry each: its names, whether it speaks, its outline and where
 *        its lettering fits.
 *
 * **Adding a shape is a value in `ShapeProperties::Kind` and an entry here** (shapes.cpp), with its path
 * function, its text-area function and its named constants beside it. Everything else asks the table:
 * the file names (shapeName), the picker and *Convert to ▸* (shapeTitle), the tail a placement sprouts
 * (shapeSpeaks), the silhouette (silhouette.cpp) and the text area (lettering.cpp).
 */

#ifndef SHAPES_HPP
#define SHAPES_HPP

#include <QPainterPath>
#include <QRectF>

#include "objectrecord.hpp"

namespace Geometry {

/**
 * @brief One shape: everything about it that is not the record's own state.
 *
 * Both functions take the balloon's rectangle (balloonRect()), so they know nothing about the record:
 * the stroke and the author's padding are applied by the caller, the same way for every shape.
 */
struct ShapeInfo
{
    ShapeProperties::Kind kind;   //!< Which shape this entry is; the table is checked against it.
    const char*           name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*           title;  //!< Shown on screen, translated through QObject::tr().
    /**
     * Someone is talking, so a placed one gets a tail. A caption, a banner or a scroll is narration —
     * it has no speaker to point at, and a tail there is one the author would have to turn off.
     */
    bool speaks;
    /**
     * The outline in @p body, **star-shaped around its centre**: a tail leaves the balloon along a ray
     * from the centre, so every such ray must cross the outline once. nullptr for `None`, which has none.
     */
    QPainterPath (*outline)(const QRectF& body);
    /**
     * The largest axis-aligned rectangle the outline actually contains, before the stroke and the
     * padding are taken off. This is the half of a shape that takes the thought: it is what stops a
     * wide balloon spilling its words out between a burst's spikes or past a diamond's slope.
     */
    QRectF (*safeArea)(const QRectF& body);
};

//! The entry for @p kind. Every kind up to `ShapeProperties::k_lastKind` has one.
[[nodiscard]] const ShapeInfo& shapeInfo(ShapeProperties::Kind kind);

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
