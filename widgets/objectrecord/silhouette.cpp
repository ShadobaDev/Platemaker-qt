// The silhouette: a shape's outline with its tails united into it, what the whole record covers, and
// which part of it a point lands on. The shapes themselves are in shapes.cpp.

#include "geometry.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QPainterPathStroker>
#include <QtMath>

#include <cmath>

namespace Geometry {

namespace {

/**
 *  How far the tail's base is pulled back inside the silhouette, as a fraction of its width. Enough
 *  that the union is watertight, little enough that the taper still reads from where it emerges.
 */
constexpr qreal k_tailBaseInset = 0.6;
/**
 *  Samples on the way from a tail's tip to the centre, looking for the first one inside the outline. 64
 *  misses only a dent narrower than a sixty-fourth of the tail's reach.
 */
constexpr int   k_tailSamples   = 64;
/**
 *  Binary-search steps for the silhouette crossing, between the two samples that bracket it. 24
 *  halvings put it far under a pixel, and each step is one QPainterPath::contains().
 */
constexpr int   k_edgeSteps     = 24;

/**
 * @brief One tail, from where the line to its tip leaves the silhouette out to that tip.
 *
 * Shape-agnostic by construction. Rather than knowing where a given outline's edge is, it walks the
 * line from the tip towards the balloon's centre until a sample lands inside, then binary-searches that
 * step for the crossing with QPainterPath::contains(). Walking **from the tip** finds the crossing
 * nearest it, which is the edge the tail is seen to leave even where a concave outline crosses the line
 * more than once (PLAN-L B12); it is what lets a tail leave any edge instead of only the bottom one.
 *
 * The base is pulled back *inside* the silhouette so the union is watertight — everything inside
 * disappears into it anyway, and what shows is the taper from the point it emerges.
 *
 * @param outline The bare silhouette, without any tail already merged into it: each tail must be aimed
 *                at the balloon, not at a neighbour's outline.
 */
QPainterPath tailPath(const QPainterPath& outline, const QRectF& body, const Tail& t)
{
    const QPointF centre = body.center();
    QPointF       ray    = t.tip - centre;
    const qreal   len    = std::hypot(ray.x(), ray.y());
    if (len < 1.0 || t.baseWidth < 1.0)
        return {};                       // tip on the centre, or a tail too thin to see
    if (outline.contains(t.tip))
        return {};                       // the tip sits inside the balloon: nothing would show
    ray /= len;

    // The crossing as a fraction of the distance from the centre: the first sample inside, walking in
    // from the tip, and the one before it bracket it.
    qreal outside = 1.0;
    qreal inside  = -1.0;
    for (int i = 1; i <= k_tailSamples && inside < 0.0; ++i) {
        const qreal f = 1.0 - qreal(i) / k_tailSamples;
        if (outline.contains(centre + ray * (len * f)))
            inside = f;
        else
            outside = f;
    }
    if (inside < 0.0)
        return {};                       // the line never enters the balloon: nothing to leave from
    for (int i = 0; i < k_edgeSteps; ++i) {
        const qreal mid = (inside + outside) / 2.0;
        if (outline.contains(centre + ray * (len * mid)))
            inside = mid;
        else
            outside = mid;
    }

    const qreal edge = len * inside;

    const QPointF perp(-ray.y(), ray.x());
    const qreal   half = t.baseWidth / 2.0;
    const QPointF base = centre + ray * qMax(0.0, edge - t.baseWidth * k_tailBaseInset);
    const QPointF b1   = base + perp * half;
    const QPointF b2   = base - perp * half;

    // Bend is a sideways offset of both control points, so the tail curves as one ribbon rather than
    // pinching. Expressed as a fraction of the tail's own length, so it looks the same at any size.
    const QPointF sway = perp * (t.bend * (len - edge));

    QPainterPath p;
    p.moveTo(b1);
    p.quadTo((b1 + t.tip) / 2.0 + sway, t.tip);
    p.quadTo((b2 + t.tip) / 2.0 + sway, b2);
    p.closeSubpath();
    return p;
}

} // namespace

QPainterPath silhouette(const ObjectRecord& a)
{
    const QRectF body = balloonRect(a);
    QPainterPath path = outlineOf(a, body);
    if (path.isEmpty())
        return {};
    // Each tail is aimed at the *bare* outline, not at the accumulating union: otherwise the second
    // tail would ray-cast against the first one and emerge from its flank.
    const QPainterPath outline = path;
    for (const Tail& t : a.tails.items) {
        const QPainterPath tp = tailPath(outline, body, t);
        if (!tp.isEmpty())
            path = path.united(tp);
    }
    return path;
}

QList<bool> tailsDrawn(const ObjectRecord& a)
{
    const QRectF       body    = balloonRect(a);
    const QPainterPath outline = outlineOf(a, body);
    QList<bool>        drawn;
    drawn.reserve(a.tails.items.size());
    for (const Tail& t : a.tails.items)
        drawn.append(!outline.isEmpty() && !tailPath(outline, body, t).isEmpty());
    return drawn;
}

Part partAt(const ObjectRecord& a, const QPointF& local, qreal slack)
{
    // ponytail: geometry, not pixels. A Marker or Ink balloon is drawn through an SVG filter that
    // displaces its edges by a pixel or two, so at a high style amount a press right on the visible
    // edge can answer with the neighbouring part. Exactness would mean rasterising an id map through
    // the same filter on every press; the slack below already covers more than the filter moves.
    slack = qMax(slack, 0.0);

    // Topmost first. The lettering is drawn over the balloon, so a letter standing on the fill answers
    // "text" — which is what the eye says too.
    const QPainterPath text = textOutline(a);
    if (!text.isEmpty()) {
        if (text.contains(local))
            return Part::Text;
        if (slack > 0.0) {
            QPainterPathStroker widen;
            widen.setWidth(slack * 2);
            if (widen.createStroke(text).contains(local))
                return Part::Text;
        }
    }

    const QPainterPath shape = silhouette(a);
    if (shape.isEmpty())
        return Part::None;   // a shapeless record is its lettering and nothing else

    // A style's own strokes are the edge where it has them, so a press on one is a press on the outline.
    const QPainterPath strokes = ink(a, shape);
    if (!strokes.isEmpty() && strokes.contains(local))
        return Part::Outline;

    // The stroke is painted centred on the silhouette, so the band a click can land on is the path
    // stroked to the pen's width — plus the slack, because a hairline outline is unhittable without it.
    QPainterPathStroker band;
    band.setWidth(qMax(1.0, static_cast<qreal>(a.style.strokeWidth)) + slack * 2);
    if (band.createStroke(shape).contains(local))
        return Part::Outline;

    return shape.contains(local) ? Part::Fill : Part::None;
}

QRectF bounds(const ObjectRecord& a)
{
    return boundsOf(a, silhouette(a), textOutline(a));
}

QRectF boundsOf(const ObjectRecord& a, const QPainterPath& silhouette, const QPainterPath& text)
{
    // The balloon always counts, even when nothing is drawn in it — an empty shapeless record still
    // occupies the box the author dragged.
    QRectF r(0, 0, a.box.width(), a.box.height());

    if (!silhouette.isEmpty())
        r = r.united(silhouette.boundingRect());
    if (!text.isEmpty())
        r = r.united(text.boundingRect());

    // The stroke straddles the path, so half of it lies outside; one more pixel keeps antialiasing off
    // the edge of the buffer.
    const qreal pad = a.style.strokeWidth / 2.0 + 1.0 + styleMargin(a);
    r = r.adjusted(-pad, -pad, pad, pad);

    // Whole pixels, so the rasterised buffer and the SVG viewBox describe the same rectangle rather
    // than two roundings of it.
    const qreal left = qFloor(r.left());
    const qreal top  = qFloor(r.top());
    return QRectF(left, top, qCeil(r.right()) - left, qCeil(r.bottom()) - top);
}

}  // namespace Geometry
