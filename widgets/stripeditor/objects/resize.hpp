#ifndef STRIPEDIT_RESIZE_HPP
#define STRIPEDIT_RESIZE_HPP

#include <QPointF>
#include <QRectF>

#include <optional>

namespace StripEdit {

/**
 * @brief What a press or a hover landed on. `Body` falls through to the object's own move handling.
 *
 * Declared here rather than inside Object so the resize maths below can be tested without a scene —
 * Object spells it `Object::Grip` all the same.
 */
enum class Grip { None, Body, TopLeft, TopRight, BottomLeft, BottomRight, Handle, Top, Bottom, Left, Right };

//! True for the four corner grips.
[[nodiscard]] constexpr bool isCorner(Grip g)
{
    return g == Grip::TopLeft || g == Grip::TopRight || g == Grip::BottomLeft || g == Grip::BottomRight;
}

//! True for the four edge grips.
[[nodiscard]] constexpr bool isEdge(Grip g)
{
    return g == Grip::Top || g == Grip::Bottom || g == Grip::Left || g == Grip::Right;
}

/**
 * @brief How a resize is constrained — the object's own rule and the modifiers held during the drag.
 */
struct ResizeRule
{
    bool  keepAspect = false;   //!< Shift, or an object whose size is one number (artwork).
    bool  fromCentre = false;   //!< Ctrl: the opposite side moves the other way, the centre stays.
    qreal minSize    = 0.0;     //!< Neither side may end up shorter than this.
    qreal snap       = 0.0;     //!< Sizes are whole multiples of this (one item unit); 0 = no snapping.
};

//! Grip size in *screen* pixels — the selection chrome's, and a tail tip's hit area.
inline constexpr qreal k_gripScreenPx = 9.0;

/**
 * @brief The rectangle a grip drag arrives at — the GIMP model.
 *
 * Dragging a corner moves that corner and an edge moves that edge; the opposite corner or edge is the
 * anchor and does not move. With `fromCentre` the anchor is the centre instead, and the opposite side
 * moves by the same amount the other way. With `keepAspect` the start's aspect ratio is kept: a corner
 * follows whichever axis the pointer has changed more, an edge drags the other axis along with it,
 * centred on its midline.
 *
 * Worked from the rectangle at press plus the total delta, never per mouse-move, so a fast drag cannot
 * accumulate rounding error.
 *
 * @param start The box at press, in scene coordinates.
 * @param grip  The grip being dragged — a corner or an edge; anything else returns @p start.
 * @param delta Pointer travel since the press, in scene coordinates.
 * @param rule  Aspect, centre, the minimum size and the snap. The size is snapped *before* the anchor
 *              is applied, so the anchored side stays exactly where it was.
 * @return The new box, or nothing when it would be smaller than `rule.minSize` on either axis — the
 *         caller keeps the last good one. Refusing rather than clamping keeps the box from "sticking"
 *         and then jumping on the way back; it also means a box is never turned inside out.
 */
[[nodiscard]] std::optional<QRectF> resizedRect(const QRectF& start, Grip grip, const QPointF& delta,
                                                const ResizeRule& rule);

}  // namespace StripEdit

#endif // STRIPEDIT_RESIZE_HPP
