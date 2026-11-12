// The silhouette: a shape's outline with its tails united into it, what the whole record covers, and
// which part of it a point lands on. The shapes themselves are in shapes.cpp.

#include "geometry.hpp"
#include "outline.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QPainterPathStroker>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <optional>
#include <vector>

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
//! The least an ellipse of a Circles tail is along it, in balloon pixels: gaps wider than the way leave this.
constexpr qreal k_minCircle     = 1.0;
//! Either side of a point on a tail's middle, as a share of it, for the way it runs there.
constexpr qreal k_spineProbe    = 0.001;
//! The narrowest a tail's Round or Cut end is drawn, in balloon pixels, either side; narrower, it is a point.
constexpr qreal k_minTipEnd     = 0.25;
//! A half turn, in degrees, as QPainterPath::arcTo() takes it.
constexpr qreal k_halfTurn      = 180.0;
/**
 *  The longest a broken tail's sharp turn is mitred, in its half-widths there: past it (a turn back on
 *  itself) the turn is cut off square instead of spiking.
 */
constexpr qreal k_breakMiter    = 4.0;

/**
 * \brief A Circles tail's ellipses along @p spine, the tail's middle from where the chain starts to the tip
 *        (TailLook): shrinking towards the tip, `pitch` apart, the last ending at it.
 *
 * Every ellipse is the same path turned and moved, so they all wind one way, and the winding rule unites
 * any that overlap (a negative pitch, a chain sunk into the balloon) rather than cutting holes.
 */
QPainterPath circlesAlong(const QPainterPath& spine, const Tail& t)
{
    const qreal length = spine.length();
    const int   count  = qMax(1, t.look.circles.count);
    std::vector<qreal> share(static_cast<size_t>(count));
    qreal              total = 0.0;
    for (int i = 0; i < count; ++i) {
        share[size_t(i)] = count > 1 ? 1.0 - (1.0 - TailLook::k_lastCircle) * i / (count - 1) : 1.0;
        total += share[size_t(i)];
    }
    const qreal scale = qMax(length - (count - 1) * t.look.circles.pitch, count * k_minCircle) / total;

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    qreal at = 0.0;
    for (int i = 0; i < count; ++i) {
        const qreal   along  = share[size_t(i)] * scale;
        const qreal   across = t.baseWidth * share[size_t(i)];
        const qreal   pct    = spine.percentAtLength(qBound(0.0, at + along / 2.0, length));
        const QPointF way    = spine.pointAtPercent(qMin(1.0, pct + k_spineProbe))
                             - spine.pointAtPercent(qMax(0.0, pct - k_spineProbe));
        QTransform    place;
        place.translate(spine.pointAtPercent(pct).x(), spine.pointAtPercent(pct).y());
        place.rotateRadians(std::atan2(way.y(), way.x()));
        QPainterPath ellipse;
        ellipse.addEllipse(QPointF(), along / 2.0, across / 2.0);
        out.addPath(place.map(ellipse));
        at += along + t.look.circles.pitch;
    }
    return out;
}

/**
 * \brief Where a tail leaves its bubble: the line from the bubble's centre to the tip, and how far along it
 *        the outline is crossed.
 */
struct TailRay
{
    QPointF centre;      //!< The bubble's centre, where the line starts.
    QPointF ray;         //!< The unit direction from the centre to the tip.
    QPointF perp;        //!< `ray` turned a quarter, the direction across the tail.
    qreal   len  = 0.0;  //!< The distance from the centre to the tip.
    qreal   edge = 0.0;  //!< The distance from the centre to where the line leaves the outline.
};

/**
 * \brief Where the line from @p body's centre to @p t's tip leaves @p outline, or nothing when no tail can
 *        be drawn: a tip on the centre, a tail too thin to see, a tip inside the outline, or a line that
 *        never enters it.
 *
 * Shape-agnostic by construction. Rather than knowing where a given outline's edge is, it walks the
 * line from the tip towards the balloon's centre until a sample lands inside, then binary-searches that
 * step for the crossing with QPainterPath::contains(). Walking **from the tip** finds the crossing
 * nearest it, which is the edge the tail is seen to leave even where a concave outline crosses the line
 * more than once; it is what lets a tail leave any edge instead of only the bottom one.
 */
std::optional<TailRay> tailRay(const QPainterPath& outline, const QRectF& body, const Tail& t)
{
    TailRay r;
    r.centre = body.center();
    r.ray    = t.tip - r.centre;
    r.len    = std::hypot(r.ray.x(), r.ray.y());
    if (r.len < 1.0 || t.baseWidth < 1.0)
        return std::nullopt;             // tip on the centre, or a tail too thin to see
    if (outline.contains(t.tip))
        return std::nullopt;             // the tip sits inside the balloon: nothing would show
    r.ray /= r.len;

    // The crossing as a fraction of the distance from the centre: the first sample inside, walking in
    // from the tip, and the one before it bracket it.
    qreal outside = 1.0;
    qreal inside  = -1.0;
    for (int i = 1; i <= k_tailSamples && inside < 0.0; ++i) {
        const qreal f = 1.0 - qreal(i) / k_tailSamples;
        if (outline.contains(r.centre + r.ray * (r.len * f)))
            inside = f;
        else
            outside = f;
    }
    if (inside < 0.0)
        return std::nullopt;             // the line never enters the balloon: nothing to leave from
    for (int i = 0; i < k_edgeSteps; ++i) {
        const qreal mid = (inside + outside) / 2.0;
        if (outline.contains(r.centre + r.ray * (r.len * mid)))
            inside = mid;
        else
            outside = mid;
    }

    r.edge = r.len * inside;
    r.perp = QPointF(-r.ray.y(), r.ray.x());
    return r;
}

/**
 * \brief A tail's middle: one quad from `base` to `tip`, its control halfway between them moved sideways by
 *        `sway`, the bend. What every look of a tail is drawn along.
 */
struct TailSpine
{
    QPointF base;   //!< Where the middle starts, inside the bubble.
    QPointF tip;    //!< Where it ends.
    QPointF sway;   //!< How far the control is moved off the straight line, across the tail.
    QPointF perp;   //!< The unit direction across the tail.
};

//! A tail's outline, and its end where that is not a point (a Round or Cut tip), as the tail reports it.
struct TailShape
{
    QPainterPath             path;
    std::optional<EndPoints> end;
};

//! The end of a ribbon @p half wide either side about @p centre, square to @p across, reaching to @p tip.
EndPoints endAt(const QPointF& centre, const QPointF& across, const QPointF& tip, qreal half, bool round)
{
    OutlineEnd end;
    end.radius = half;
    end.round  = round;
    return {centre + across * half, tip, centre - across * half, end};
}

//! The spine as a path, from its base to its tip.
QPainterPath spinePath(const TailSpine& s)
{
    QPainterPath spine(s.base);
    spine.quadTo((s.base + s.tip) / 2.0 + s.sway, s.tip);
    return spine;
}

/**
 * \brief A broken tail (TailLook::breaks): a ribbon about the line through the spine's base, its breaks and
 *        its tip, @p half either side at the base and narrowing to @p end either side at the tip
 *        (0 a point; a half circle there when @p round), with sharp turns where @p look breaks it.
 *
 * Each run is a trapezoid from its start's half-width to its end's; each turn is filled on its outside by
 * a mitre (bevelled past `k_breakMiter`); a round end is a circle on the last run. They are united into
 * one outline, so a line drawn round it meets no edge inside.
 */
TailShape brokenRibbon(const TailSpine& s, qreal half, qreal end, bool round, const TailLook& look)
{
    const int     breaks  = qBound(0, look.breaks.count, BreakParams::k_maxBreaks);
    const QPointF control = (s.base + s.tip) / 2.0 + s.sway;
    const qreal   jog     = QLineF(s.base, s.tip).length() * qBound(0, look.breaks.jog, BreakParams::k_maxJog) / 100.0
                          * (look.breaks.clockwise ? 1.0 : -1.0);

    // The runs' corners: on the bent course, each break moved across it, to the right first (s.perp) when
    // the first turn is clockwise.
    QPolygonF line{s.base};
    for (int i = 1; i <= breaks; ++i) {
        const qreal   t  = qreal(i) / (breaks + 1);
        const QPointF on = s.base * ((1.0 - t) * (1.0 - t)) + control * (2.0 * (1.0 - t) * t) + s.tip * (t * t);
        line << on + s.perp * (i % 2 ? jog : -jog);
    }
    line << s.tip;
    // A round end's far point is the tip: its circle's centre stands back from it along the last run.
    if (round) {
        const QLineF last(line[line.size() - 2], line.last());
        if (last.length() <= end)
            return {};   // a last run shorter than the end's radius has nowhere to put it
        line.last() = last.pointAt(1.0 - end / last.length());
    }

    std::vector<qreal> at{0.0};   // how far along the line each corner is
    for (int i = 1; i < line.size(); ++i)
        at.push_back(at.back() + QLineF(line[i - 1], line[i]).length());
    if (at.back() <= 0.0)
        return {};
    const auto width = [&](int i) { return half + (end - half) * at[size_t(i)] / at.back(); };
    const auto across = [&](int i) {   // the left of run i, from corner i to i + 1
        const QPointF d = line[i + 1] - line[i];
        const qreal   n = std::hypot(d.x(), d.y());
        return n > 0.0 ? QPointF(-d.y(), d.x()) / n : s.perp;
    };

    QPainterPath parts;
    parts.setFillRule(Qt::WindingFill);
    const auto add = [&parts](const QPolygonF& ring) {
        // Every piece wound one way, so the winding rule unites rather than cuts them.
        QPolygonF r = ring;
        qreal     area = 0.0;
        for (int i = 0; i < r.size(); ++i)
            area += r[i].x() * r[(i + 1) % r.size()].y() - r[(i + 1) % r.size()].x() * r[i].y();
        if (area < 0.0)
            std::reverse(r.begin(), r.end());
        parts.addPolygon(r);
        parts.closeSubpath();
    };
    for (int i = 0; i + 1 < line.size(); ++i) {
        const QPointF n  = across(i);
        const qreal   w0 = width(i);
        const qreal   w1 = width(i + 1);
        add(QPolygonF{line[i] + n * w0, line[i + 1] + n * w1, line[i + 1] - n * w1, line[i] - n * w0});
    }
    for (int i = 1; i + 1 < line.size(); ++i) {
        const QPointF n0 = across(i - 1);
        const QPointF n1 = across(i);
        const qreal   w  = width(i);
        QPointF       bis = n0 + n1;
        const qreal   bl  = std::hypot(bis.x(), bis.y());
        for (qreal side : {1.0, -1.0}) {
            const QPointF a = line[i] + n0 * (side * w);
            const QPointF b = line[i] + n1 * (side * w);
            const qreal   cosHalf = bl / 2.0;   // the cosine of half the turn
            if (bl > 0.0 && 1.0 / cosHalf <= k_breakMiter)
                add(QPolygonF{line[i], a, line[i] + bis / bl * (side * w / cosHalf), b});
            else
                add(QPolygonF{line[i], a, b});
        }
    }
    if (round) {
        QPainterPath cap;
        cap.addEllipse(line.last(), end, end);
        add(cap.toFillPolygon());
    }
    TailShape out{parts.simplified(), std::nullopt};
    if (end > 0.0)
        out.end = endAt(line.last(), across(int(line.size()) - 2), s.tip, end, round);
    return out;
}

/**
 * \brief A Plain tail: a ribbon about @p s, @p half either side at the base, narrowing to its end as
 *        @p look's tip says. Its two sides are quads swayed as the spine is, so the tail curves as one
 *        ribbon rather than pinching.
 *
 * A Sharp tail's sides meet at the tip. A Round or Cut one's stop `tipWidth` per cent of the base apart,
 * square to the way the spine runs where it ends: a cut end is the straight line between them, through
 * the tip; a round end is a half circle between them whose far point is the tip.
 */
TailShape ribbonAbout(const TailSpine& s, qreal half, const TailLook& look)
{
    const QPointF b1  = s.base + s.perp * half;
    const QPointF b2  = s.base - s.perp * half;
    const qreal   end = half * qBound(0, look.end.width, 100) / 100.0;
    const bool    sharp = look.tip == TailLook::Tip::Sharp || end < k_minTipEnd;
    if (look.breaks.count > 0)
        return brokenRibbon(s, half, sharp ? 0.0 : end, !sharp && look.tip == TailLook::Tip::Round, look);

    QPainterPath p;
    p.moveTo(b1);
    if (sharp) {
        p.quadTo((b1 + s.tip) / 2.0 + s.sway, s.tip);
        p.quadTo((b2 + s.tip) / 2.0 + s.sway, b2);
        p.closeSubpath();
        return {p, std::nullopt};
    }

    // The way the spine runs where it ends: from its control to its tip, the quad's last tangent.
    QPointF     way = s.tip - ((s.base + s.tip) / 2.0 + s.sway);
    const qreal run = std::hypot(way.x(), way.y());
    if (run <= 0.0)
        return {};   // a spine folded back onto its tip has no way to end square to
    way /= run;
    const QPointF across(-way.y(), way.x());   // on the side of s.perp, as the spine is not turned past it
    const bool    round  = look.tip == TailLook::Tip::Round;
    const QPointF centre = round ? s.tip - way * end : s.tip;
    const QPointF e1     = centre + across * end;
    const QPointF e2     = centre - across * end;

    p.quadTo((b1 + e1) / 2.0 + s.sway, e1);
    if (round) {
        // arcTo()'s angles run anticlockwise on screen, from e1 round through the tip to e2.
        const qreal from   = qRadiansToDegrees(std::atan2(-across.y(), across.x()));
        const qreal toward = qRadiansToDegrees(std::atan2(-way.y(), way.x()));
        const qreal turn   = std::remainder(toward - from, 2.0 * k_halfTurn);
        p.arcTo(QRectF(centre - QPointF(end, end), QSizeF(2.0 * end, 2.0 * end)), from,
                turn > 0.0 ? k_halfTurn : -k_halfTurn);
    } else {
        p.lineTo(e2);
    }
    p.quadTo((b2 + e2) / 2.0 + s.sway, b2);
    p.closeSubpath();
    return {p, endAt(centre, across, s.tip, end, round)};
}

/**
 * \brief One tail, from where the line to its tip leaves the silhouette (tailRay()) out to that tip.
 *
 * A Plain tail's base is pulled back *inside* the silhouette so the union is watertight — everything
 * inside disappears into it anyway, and what shows is the taper from the point it emerges. Its bend is a
 * sideways sway expressed as a fraction of the tail's own length, so it looks the same at any size. It
 * ends as its tip says (ribbonAbout()).
 *
 * A Circles tail is a chain of ellipses on that same spine instead (circlesAlong()), from the crossing
 * moved by its offset to the tip.
 *
 * \param outline The bare silhouette, without any tail already merged into it: each tail must be aimed
 *                at the balloon, not at a neighbour's outline.
 */
TailShape tailPath(const QPainterPath& outline, const QRectF& body, const Tail& t)
{
    const std::optional<TailRay> r = tailRay(outline, body, t);
    if (!r)
        return {};

    if (t.look.style == TailLook::Style::Circles) {
        // The chain lies on the plain tail's middle, bend and all, from the crossing (moved in by the
        // offset, or out by a negative one) to the tip.
        const qreal from = r->edge - t.look.circles.offset;
        if (r->len - from < 1.0)
            return {};   // a gap past the tip: nothing left to draw
        const TailSpine s{r->centre + r->ray * from, t.tip, r->perp * (t.bend * (r->len - from)), r->perp};
        return {circlesAlong(spinePath(s), t), std::nullopt};
    }

    const TailSpine s{r->centre + r->ray * qMax(0.0, r->edge - t.baseWidth * k_tailBaseInset), t.tip,
                      r->perp * (t.bend * (r->len - r->edge)), r->perp};
    return ribbonAbout(s, t.baseWidth / 2.0, t.look);
}

} // namespace

QList<QPainterPath> silhouetteParts(const ObjectRecord& a)
{
    const QRectF       body    = balloonRect(a);
    const QPainterPath outline = outlineOf(a, body);
    if (outline.isEmpty())
        return {};
    // Each tail is aimed at the *bare* outline, not at the accumulating union: otherwise the second
    // tail would ray-cast against the first one and emerge from its flank.
    QList<QPainterPath> parts{outline};
    for (const Tail& t : a.tails.items) {
        const QPainterPath tp = tailPath(outline, body, t).path;
        if (!tp.isEmpty())
            parts.append(tp);
    }
    return parts;
}

OutlineModel outlineModel(const ObjectRecord& a, const QPainterPath& silhouette)
{
    OutlineModel m;
    m.joined      = silhouette;
    m.frame       = QRectF(0, 0, a.box.width(), a.box.height());
    m.coreStretch = shapeInfo(a.shape.kind).flashStretch;

    const QRectF       body    = balloonRect(a);
    const QPainterPath outline = outlineOf(a, body);
    if (outline.isEmpty())
        return m;
    QList<QPainterPath>                   parts{outline};
    QList<std::optional<EndPoints>>       partEnds{std::nullopt};
    QList<EndPoints>                      ends;
    for (const Tail& t : a.tails.items) {
        TailShape tail = tailPath(outline, body, t);
        if (tail.path.isEmpty())
            continue;
        parts.append(tail.path);
        partEnds.append(tail.end);
        if (tail.end)
            ends.append(*tail.end);
    }
    m.rings = ringsOf(silhouette, {}, -1, ends);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        QList<EndPoints> own;
        if (partEnds[i])
            own.append(*partEnds[i]);
        m.parts.append({parts[i], ringsOf(parts[i], parts, i, own)});
    }
    return m;
}

QPainterPath silhouette(const ObjectRecord& a)
{
    const QList<QPainterPath> parts = silhouetteParts(a);
    if (parts.isEmpty())
        return {};
    QPainterPath path = parts.first();
    for (qsizetype i = 1; i < parts.size(); ++i)
        path = path.united(parts[i]);
    return path;
}

QList<bool> tailsDrawn(const ObjectRecord& a)
{
    const QRectF       body    = balloonRect(a);
    const QPainterPath outline = outlineOf(a, body);
    QList<bool>        drawn;
    drawn.reserve(a.tails.items.size());
    for (const Tail& t : a.tails.items)
        drawn.append(!outline.isEmpty() && !tailPath(outline, body, t).path.isEmpty());
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
        // The letters' outline is theirs too.
        if (const qreal reach = slack + qMax(a.text.outlineWidth, 0.0); reach > 0.0) {
            QPainterPathStroker widen;
            widen.setWidth(reach * 2);
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
    if (!text.isEmpty()) {
        // The outline is drawn outside the letters, and their shadow reaches past it.
        const qreal around = qMax(a.text.outlineWidth, 0.0) + shadowReach(a.text.shadow);
        r = r.united(text.boundingRect().adjusted(-around, -around, around, around));
    }

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
