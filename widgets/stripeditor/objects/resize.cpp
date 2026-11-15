#include "objects/resize.hpp"

#include <QtMath>

namespace StripEdit {

namespace {

//! Smallest box the author can resize to — below this the grips overlap and it stops being draggable.
constexpr int k_minBox = 40;

} // namespace

ResizeRule resizeRule(qreal itemScale, bool byScale, bool shift, bool ctrl)
{
    const qreal k = itemScale > 0.0 ? itemScale : 1.0;
    ResizeRule  rule;
    rule.keepAspect = byScale || shift;
    rule.fromCentre = ctrl;
    rule.minSize    = byScale ? k_minBox : k_minBox * k;
    rule.snap       = byScale ? 0.0 : k;
    return rule;
}

std::optional<QRectF> resizedRect(const QRectF& start, Grip grip, const QPointF& delta,
                                  const ResizeRule& rule)
{
    if (!isCorner(grip) && !isEdge(grip))
        return start;

    // Which sides the grip carries. A corner carries two, an edge one; the rest stay where they were.
    const bool left   = grip == Grip::TopLeft || grip == Grip::BottomLeft || grip == Grip::Left;
    const bool right  = grip == Grip::TopRight || grip == Grip::BottomRight || grip == Grip::Right;
    const bool top    = grip == Grip::TopLeft || grip == Grip::TopRight || grip == Grip::Top;
    const bool bottom = grip == Grip::BottomLeft || grip == Grip::BottomRight || grip == Grip::Bottom;

    // From the centre, the opposite side mirrors the dragged one, so the size changes twice as fast.
    const qreal k = rule.fromCentre ? 2.0 : 1.0;
    qreal w = start.width()  + k * (right ? delta.x() : left ? -delta.x() : 0.0);
    qreal h = start.height() + k * (bottom ? delta.y() : top ? -delta.y() : 0.0);

    if (rule.keepAspect && start.width() > 0.0 && start.height() > 0.0) {
        const qreal aspect = start.height() / start.width();
        if (isCorner(grip)) {
            // The corner goes to the point of the box's diagonal nearest the pointer: both axes count,
            // each by the box's extent along it, so the size follows the hand continuously. Picking the
            // axis pulled further instead jumped whenever the pick changed — on a tall picture, a pixel
            // sideways outweighed several down, and the box flipped from growing to shrinking.
            const qreal s = (w * start.width() + h * start.height())
                          / (start.width() * start.width() + start.height() * start.height());
            w = start.width() * s;
            h = start.height() * s;
        } else if (left || right) {
            h = w * aspect;
        } else {
            w = h / aspect;
        }
    }

    if (rule.snap > 0.0) {
        w = qRound(w / rule.snap) * rule.snap;
        h = qRound(h / rule.snap) * rule.snap;
    }

    if (w < rule.minSize || h < rule.minSize)
        return std::nullopt;

    // The anchor: the centre, or the side opposite the dragged one. An axis the grip does not carry —
    // the other axis of an edge, which only changes under keepAspect — stays centred on its midline.
    const QPointF c = start.center();
    const qreal x = rule.fromCentre ? c.x() - w / 2.0
                  : left            ? start.right() - w
                  : right           ? start.left()
                                    : c.x() - w / 2.0;
    const qreal y = rule.fromCentre ? c.y() - h / 2.0
                  : top             ? start.bottom() - h
                  : bottom          ? start.top()
                                    : c.y() - h / 2.0;
    return QRectF(x, y, w, h);
}

}  // namespace StripEdit
