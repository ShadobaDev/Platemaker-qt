#ifndef STRIPEDIT_MASKAREA_HPP
#define STRIPEDIT_MASKAREA_HPP

#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>

namespace StripEdit {

//! How a mask tool draws the area it adds to the selected mask, or subtracts from it (MaskGesture).
enum class MaskStroke {
    Brush,       //!< The pointer's track, a round brush's size across.
    Rectangle,   //!< The box from the press to the pointer.
    Ellipse,     //!< The ellipse in that box.
    Freehand,    //!< The pointer's track, closed back to its start.
    Polygon,     //!< A corner per click, closed back to the first.
};

//! How the box of a Rectangle or an Ellipse is taken from its two points.
struct BoxDraw
{
    bool square     = false;   //!< Its sides equal: a square, a circle (Shift).
    bool fromCentre = false;   //!< The first point is its centre, not a corner (Alt).
};

/**
 * \brief The area @p stroke covers along @p points, in scene coordinates: what a mask tool adds or subtracts.
 *
 * Brush: the track stroked @p size across, round caps; one point is a dab. Rectangle, Ellipse: the box from the
 * first point to the last, drawn as @p box says. Freehand, Polygon: the points closed. Empty when the points give
 * no area (none, or a box or a polygon with nothing inside).
 */
[[nodiscard]] QPainterPath maskArea(MaskStroke stroke, const QPolygonF& points, qreal size, BoxDraw box = {});

//! @p to turned about @p from onto the nearest multiple of 45°, its distance kept: a polygon's edge with Shift.
[[nodiscard]] QPointF snappedTo45(const QPointF& from, const QPointF& to);

}  // namespace StripEdit

#endif // STRIPEDIT_MASKAREA_HPP
