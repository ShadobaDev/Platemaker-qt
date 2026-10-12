#include "objects/resize.hpp"

#include <QtMath>

namespace StripEdit {

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
            // Whichever axis the pointer has pulled further decides — so the box follows the hand
            // whichever way the drag leans, rather than ignoring one direction outright.
            const qreal sx = w / start.width();
            const qreal sy = h / start.height();
            const qreal s  = qAbs(sx - 1.0) >= qAbs(sy - 1.0) ? sx : sy;
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
