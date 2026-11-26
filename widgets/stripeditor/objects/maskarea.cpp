#include "objects/maskarea.hpp"

#include <QLineF>
#include <QPainterPathStroker>
#include <QRectF>

#include <cmath>

namespace StripEdit {

namespace {

constexpr qreal k_snapStepDeg = 45.0;   //!< A polygon's edge with Shift: horizontal, vertical or diagonal.
constexpr int   k_fewestCorners = 3;    //!< Fewer points close round nothing.

//! The box from @p a to @p b, as @p box says.
QRectF boxOf(const QPointF& a, const QPointF& b, BoxDraw box)
{
    QPointF reach = b - a;
    if (box.square) {
        const qreal side = qMax(std::abs(reach.x()), std::abs(reach.y()));
        reach = {std::copysign(side, reach.x()), std::copysign(side, reach.y())};
    }
    return box.fromCentre ? QRectF(a - reach, a + reach).normalized() : QRectF(a, a + reach).normalized();
}

} // namespace

QPainterPath maskArea(MaskStroke stroke, const QPolygonF& points, qreal size, BoxDraw box)
{
    QPainterPath out;
    if (points.isEmpty())
        return out;
    switch (stroke) {
    case MaskStroke::Brush: {
        if (points.size() == 1) {
            out.addEllipse(points.first(), size / 2.0, size / 2.0);
            return out;
        }
        QPainterPath track;
        track.addPolygon(points);
        QPainterPathStroker stroker;
        stroker.setWidth(size);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(track);
    }
    case MaskStroke::Rectangle:
    case MaskStroke::Ellipse: {
        const QRectF r = boxOf(points.first(), points.last(), box);
        if (r.isEmpty())
            return out;
        if (stroke == MaskStroke::Rectangle)
            out.addRect(r);
        else
            out.addEllipse(r);
        return out;
    }
    case MaskStroke::Freehand:
    case MaskStroke::Polygon:
        if (points.size() < k_fewestCorners)
            return out;
        out.addPolygon(points);
        out.closeSubpath();
        out.setFillRule(Qt::WindingFill);   // a track crossing itself still covers what it went round
        return out;
    }
    return out;
}

QPointF snappedTo45(const QPointF& from, const QPointF& to)
{
    QLineF edge(from, to);
    edge.setAngle(std::round(edge.angle() / k_snapStepDeg) * k_snapStepDeg);
    return edge.p2();
}

}  // namespace StripEdit
