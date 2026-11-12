// The lines and the textures: one table entry each, the flash's strokes, and the room each needs past the
// geometry.

#include "styles.hpp"
#include "edgewalk.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

#include <QObject>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QTransform>
#include <QRandomGenerator>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace Geometry {

namespace {

/**
 *  The Ink look: ink bleeds in finer waves and less far than a marker wanders (the struct's defaults: a
 *  dried felt-tip's long slow waves, about 6 px off a straight line), then softens.
 */
constexpr qreal k_inkFrequency    = 0.07;
constexpr int   k_inkOctaves      = 2;
constexpr qreal k_inkDisplacement = 3.0;
constexpr qreal k_inkBlur         = 0.6;
/**
 *  How far a blur spreads, in deviations: past three, under half a percent of the ink is left.
 */
constexpr qreal k_blurReach       = 3.0;

// ---------------------------------------------------------------------------
// Flash: narrow triangles ringing the silhouette, outwards and inwards
// ---------------------------------------------------------------------------

/**
 *  The closest two strokes may be, in balloon pixels. Below a pixel the ring is a solid band anyway,
 *  and the count would grow without bound as the pitch reaches zero.
 */
constexpr qreal   k_minPitch    = 1.0;
/**
 *  Mixed into the seed for the inner strokes, so they draw from a stream of their own: changing the
 *  inner pitch does not re-deal the outer strokes. Any odd constant will do; this is 2^32 / phi.
 */
constexpr quint32 k_innerStream = 0x9E3779B9u;
//! A stroke closer than this to the core has no direction to point in, and is skipped.
constexpr qreal   k_minRay      = 1e-6;

//! One ring of strokes: the outer or the inner one.
struct Ring
{
    qreal pitch, length, base;
    int   pitchJitter, lengthJitter;   // %
    bool  inward;
};

//! A factor in [1 − p, 1 + p], for a jitter of p %.
qreal spread(QRandomGenerator& rng, int percent)
{
    const qreal p = qBound(0, percent, 100) / 100.0;
    return 1.0 + p * (2.0 * rng.generateDouble() - 1.0);
}

/**
 * \brief Adds one ring of strokes along every closed edge in @p edges.
 *
 * Strokes are spread evenly by arc length, not by angle, which would crowd them at the ends of a wide
 * oval: the edge's perimeter is divided into whole steps near the pitch, so there is no seam where the
 * walk meets its start. Each stroke points along the ray from the nearest point of @p core through its
 * place on the edge: away from it outside, towards it inside, where it is cut short at the core.
 */
void addRing(QPainterPath& out, const QList<OutlineRing>& edges, const QRectF& core, const Ring& r,
             QRandomGenerator& rng)
{
    const qreal pitch = qMax(r.pitch, k_minPitch);
    const qreal half  = qMax(r.base, 0.0) / 2.0;

    for (const OutlineRing& ring : edges) {
        const EdgeWalk& edge = ring.walk;
        const qreal perimeter = edge.length();
        const int   count = qMax(1, qRound(perimeter / pitch));
        const qreal step  = perimeter / count;
        for (int i = 0; i < count; ++i) {
            // Both numbers are drawn before anything can skip the stroke, so one skipped stroke does
            // not shift every stroke after it.
            const qreal shift  = 0.5 * (spread(rng, r.pitchJitter) - 1.0);   // ± half a step at 100 %
            qreal       length = r.length * spread(rng, r.lengthJitter);

            // Where along the edge.
            const qreal   s = std::fmod((i + shift) * step + perimeter, perimeter);
            const QPointF p = edge.at(s).at;

            // From the nearest point of the core; a point on or inside it falls back to the core's centre.
            const QPointF near(qBound(core.left(), p.x(), core.right()),
                               qBound(core.top(), p.y(), core.bottom()));
            QPointF ray = p - near;
            qreal   d   = std::hypot(ray.x(), ray.y());
            if (d < k_minRay) {
                ray = p - core.center();
                d   = std::hypot(ray.x(), ray.y());
            }
            if (d < k_minRay)
                continue;
            if (r.inward)
                length = qMin(length, d);
            if (length <= 0.0)
                continue;

            const QPointF u = ray / d;                         // along the ray, outwards
            const QPointF n(-u.y() * half, u.x() * half);      // across it, half the base
            // The inner triangle is the outer one turned half round its foot, so every stroke winds the
            // same way and the non-zero rule paints overlaps as one.
            const qreal sign = r.inward ? -1.0 : 1.0;
            out.moveTo(p + n * sign);
            out.lineTo(p - n * sign);
            out.lineTo(p + u * (length * sign));
            out.closeSubpath();
        }
    }
}

QPainterPath flashInk(const StyleProperties& s, const OutlineModel& outline, quint32 seed)
{
    const QSizeF  box    = outline.frame.size();
    const QPointF centre = outline.frame.center();

    // What the strokes point away from: the centre, stretched into a line along the box's longer axis.
    // At 100 % the line runs between the centres of the two circles that fit the box's ends, so every
    // stroke on a long straight side is square to it and only the ends fan out; a square box keeps a
    // point whatever the stretch. On Auto the shape says how far.
    const int    percent = s.flash.coreStretch == FlashParams::k_autoStretch ? outline.coreStretch
                                                                                 : s.flash.coreStretch;
    const qreal  stretch = qBound(0, percent, 100) / 100.0;
    const QSizeF half(stretch * qMax(0.0, box.width() - box.height()) / 2.0,
                      stretch * qMax(0.0, box.height() - box.width()) / 2.0);
    const QRectF core(centre.x() - half.width(), centre.y() - half.height(), 2 * half.width(),
                      2 * half.height());

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    QRandomGenerator outer(seed);
    QRandomGenerator inner(seed ^ k_innerStream);
    addRing(out, outline.rings, core,
            {s.flash.outerPitch, s.flash.outerLength, s.flash.outerBase,
             s.flash.outerPitchJitter, s.flash.outerLengthJitter, false},
            outer);
    addRing(out, outline.rings, core,
            {s.flash.innerPitch, s.flash.innerLength, s.flash.innerBase,
             s.flash.innerPitchJitter, s.flash.innerLengthJitter, true},
            inner);
    return out;
}

//! The longest outer stroke, plus half the widest base: a base turned across a corner reaches out too.
qreal flashReach(const StyleProperties& s)
{
    const qreal            jit   = qBound(0, s.flash.outerLengthJitter, 100) / 100.0;
    const qreal            reach = qMax(s.flash.outerLength, 0.0) * (1.0 + jit);
    return reach + qMax(qMax(s.flash.outerBase, s.flash.innerBase), 0.0) / 2.0;
}

// ---------------------------------------------------------------------------
// Brush: a few strokes along the edge, tapering at both ends, swelling and wandering
// ---------------------------------------------------------------------------

/**
 *  How long one swell or one wander is along a stroke, in balloon pixels: the noise's span. About a
 *  brush's hand-width, so a stroke changes a few times along a balloon's side.
 */
constexpr qreal   k_brushWave    = 60.0;
/**
 *  How far a stroke's ends are moved off an even share of the edge, in shares: so the joins do not sit
 *  where a pattern would put them.
 */
constexpr qreal   k_brushScatter = 0.15;
//! The ribbon's samples along the edge, in balloon pixels: finer than a curve's flattening shows.
constexpr qreal   k_brushStep    = 2.0;
/**
 *  Mixed into the seed for the joins' scatter, and per stroke for its swell and its wander, so each draws
 *  from a stream of its own. Odd constants (2^32 / phi, and a 32-bit mixer's multiplier).
 */
constexpr quint32 k_brushStream  = 0x9E3779B9u;
constexpr quint32 k_strokeStream = 0x85EBCA6Bu;

//! A stroke's width at @p u (0..1 along it): its full width but for the ends, which narrow to a point.
qreal taperAt(qreal u, qreal taper)
{
    if (taper <= 0.0)
        return 1.0;
    const qreal f = qBound(0.0, qMin(u, 1.0 - u) / taper, 1.0);
    return std::sin(f * M_PI / 2.0);   // eased into the full width, not a straight wedge
}

//! How a ribbon about the edge is drawn: its width, and how its ends narrow, its width swells and it wanders.
struct RibbonLook
{
    qreal half;    //!< Half its width, in balloon pixels.
    qreal taper;   //!< How much of it, at each end, narrows to its point, as a share of its length (taperAt()).
    qreal swell;   //!< How much its width varies along it, as a share of the width.
    qreal drift;   //!< How far it wanders off the edge, in balloon pixels.
};

/**
 * \brief A ribbon about @p edge from @p from to @p to, distances along it that may run past either end
 *        of the lap: as wide as @p look says, narrowing at its ends, its width and its line moved by
 *        smooth noise from @p seed. One closed polygon, its left side out and its right side back.
 */
QPolygonF ribbon(const EdgeWalk& edge, qreal from, qreal to, quint32 seed, const RibbonLook& look)
{
    const qreal length  = edge.length();
    const int   samples = qMax(2, qCeil((to - from) / k_brushStep));
    QPolygonF   left, right;
    for (int k = 0; k <= samples; ++k) {
        const qreal  u  = qreal(k) / samples;
        const qreal  at = from + (to - from) * u;
        const auto   p  = edge.at(at - std::floor(at / length) * length);
        const QPointF n(-p.tangent.y(), p.tangent.x());
        const qreal  w  = look.half * taperAt(u, look.taper) * (1.0 + look.swell * smoothNoise(seed, at / k_brushWave));
        const qreal  c  = look.drift * smoothNoise(seed ^ k_brushStream, at / k_brushWave);
        left << p.at + n * (c + w);
        right.prepend(p.at + n * (c - w));
    }
    return left + right;
}

/**
 * \brief The brush's strokes along every closed edge of @p outline.
 *
 * Each edge is shared out among `brushStrokes` strokes, their joins scattered off the even share and
 * each running `brushOverlap` past the next one's start, so they overlap and do not quite close. A stroke
 * is a ribbon about the edge, as wide as the outline, narrowing to a point over `brushTaper` of its
 * length at each end, its width varied by `brushSwell` and its middle moved off the edge by
 * `brushDrift`, both along smooth noise. Every ribbon winds the same way, so overlaps paint as one.
 */
QPainterPath brushInk(const StyleProperties& s, const OutlineModel& outline, quint32 seed)
{
    const qreal            half  = qMax(s.strokeWidth, 0) / 2.0;
    const int              count = qMax(1, s.brush.strokes);
    const qreal            over  = qBound(0, s.brush.overlap, 100) / 100.0;
    const qreal            taper = qBound(0, s.brush.taper, 50) / 100.0;
    const qreal            swell = qBound(0, s.brush.swell, 100) / 100.0;

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (half <= 0.0)
        return out;   // no outline, no brush: its width is the outline's
    const RibbonLook look{half, taper, swell, s.brush.drift};
    QRandomGenerator rng(seed ^ k_brushStream);
    quint32          stroke = 0;   // a number per stroke, for its own noise
    for (const OutlineRing& ring : outline.rings) {
        const EdgeWalk& edge   = ring.walk;
        const qreal     length = edge.length();
        const qreal     share  = length / count;
        std::vector<qreal> join(size_t(count) + 1);
        for (int j = 0; j < count; ++j)
            join[size_t(j)] = (j + k_brushScatter * (2.0 * rng.generateDouble() - 1.0)) * share;
        join[size_t(count)] = join[0] + length;   // the last stroke ends where the first begins, round

        for (int j = 0; j < count; ++j, ++stroke) {
            const qreal from = join[size_t(j)] - over * share / 2.0;
            const qreal to   = join[size_t(j) + 1] + over * share / 2.0;
            out.addPolygon(ribbon(edge, from, to, seed ^ ((stroke + 1) * k_strokeStream), look));
            out.closeSubpath();
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Dashed: the edge cut into dashes, a whole number to each lap
// ---------------------------------------------------------------------------

/**
 * \brief The dashes along every closed edge of @p outline: straight-ended ribbons as wide as the
 *        outline, starting where the edge does.
 *
 * The lap takes the whole number of dash-and-gap periods nearest its length, and the gap gives or takes
 * the rest, so no short dash or doubled gap marks where the lap closes. A gap of 0 is one whole line.
 */
QPainterPath dashedInk(const StyleProperties& s, const OutlineModel& outline, quint32)
{
    const RibbonLook       look{qMax(s.strokeWidth, 0) / 2.0, 0.0, 0.0, 0.0};   // straight: no taper, swell or drift
    const qreal            dash = qMax(s.dash.length, 0.0);
    const qreal            gap  = qMax(s.dash.gap, 0.0);

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (look.half <= 0.0 || dash <= 0.0)
        return out;
    for (const OutlineRing& ring : outline.rings) {
        const EdgeWalk& edge   = ring.walk;
        const qreal     length = edge.length();
        if (gap <= 0.0) {
            out.addPolygon(ribbon(edge, 0.0, length, 0, look));
            out.closeSubpath();
            continue;
        }
        const int   count  = qMax(1, qRound(length / (dash + gap)));
        const qreal period = length / count;
        for (int k = 0; k < count; ++k) {
            out.addPolygon(ribbon(edge, k * period, k * period + qMin(dash, period), 0, look));
            out.closeSubpath();
        }
    }
    return out;
}

//! Half the outline: a dash is as wide as it.
qreal dashedReach(const StyleProperties& s)
{
    return qMax(s.strokeWidth, 0) / 2.0;
}

// ---------------------------------------------------------------------------
// Multi-line: the outline, and lines inside it
// ---------------------------------------------------------------------------

//! Everything within @p width / 2 of @p silhouette's edge, either side, its corners round as the outline's are.
QPainterPath edgeBand(const QPainterPath& silhouette, qreal width)
{
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(silhouette);
}

/**
 * \brief The outline as Clean draws it, and `multiLines` - 1 lines inside it, each `multiGap` in from the
 *        one before and `multiWidth` wide: the part of the silhouette that far from its edge. Where a tail
 *        is too narrow to hold one, it stops. No outline width, no line, as for the brush.
 */
QPainterPath multiInk(const StyleProperties& s, const OutlineModel& outline, quint32)
{
    const QPainterPath&    silhouette = outline.joined;
    const qreal            width = qMax(s.strokeWidth, 0);
    const qreal            inner = qMax(s.multi.width, 0.0);
    const qreal            gap   = qMax(s.multi.gap, 0.0);
    if (width <= 0.0)
        return {};
    QPainterPath out  = edgeBand(silhouette, width);
    qreal        from = width / 2.0;   // how far in from the edge the last line drawn ends
    for (int k = 1; inner > 0.0 && k < s.multi.lines; ++k) {
        from += gap;
        out   = out.united(silhouette.intersected(edgeBand(silhouette, 2.0 * (from + inner)))
                             .subtracted(edgeBand(silhouette, 2.0 * from)));
        from += inner;
    }
    return out;
}

//! Half the outline: the other lines are inside it.
qreal multiReach(const StyleProperties& s)
{
    return qMax(s.strokeWidth, 0) / 2.0;
}

//! Half the outline at its widest swell, and as far as a stroke wanders.
qreal brushReach(const StyleProperties& s)
{
    return qMax(s.strokeWidth, 0) / 2.0 * (1.0 + qBound(0, s.brush.swell, 100) / 100.0)
         + std::abs(s.brush.drift);
}

// ---------------------------------------------------------------------------
// Drybrush: a thin whole core along the edge, and hairs either side of it that thin out away from it
// ---------------------------------------------------------------------------

//! How long one hair or one gap is along the line, in balloon pixels: the noise's span.
constexpr qreal   k_bristleWave   = 32.0;
//! A hair's half-width, in lanes: thin, with the dark between them.
constexpr qreal   k_bristleHair   = 0.4;
/**
 *  How much drier the outermost lane is than the one by the core, as a share of the way from the dryness
 *  to bone dry: the hairs thin out outwards but do not quite stop.
 */
constexpr qreal   k_bristleThinning = 0.75;
//! How far a hair wanders off its lane, in lanes.
constexpr qreal   k_bristleWander = 0.2;
/**
 *  How far under the dry level the noise falls before a hair is its full width, in noise units: so a
 *  hair narrows to its ends rather than stopping square.
 */
constexpr qreal   k_bristleSwell  = 0.35;
//! How long a hair narrows away over before the other side's core at a concave corner, in lanes.
constexpr qreal   k_rootTaper     = 2.0;
/**
 *  How far a hair runs on under another part of the silhouette, in half line widths: a balloon's into a
 *  tail's root, a tail's back into the balloon. At most; its own noise may end it sooner, and the last
 *  third of the way it narrows away.
 */
constexpr qreal   k_partRunOn     = 3.0;
/**
 *  How far from a concave corner its other side's core can stop a hair, in half line widths, along the
 *  edge or past the corner: further off, a curving edge is not that side's business.
 */
constexpr qreal   k_rootReach     = 4.0;
//! A convex corner narrower than this, in degrees, is a point: the hairs run into it.
constexpr qreal   k_pointAngle    = 45.0;
/**
 *  How far past the other side's hairs a hair runs on across a corner, in half line widths, so that they
 *  cross into a hatch; each hair's own run varies up to half as much again.
 */
constexpr qreal   k_hatchReach    = 0.4;
/**
 *  The core's miter limit, in its widths (QPainterPathStroker): long enough that a point down to about
 *  ten degrees is mitred to its tip, short enough that a sliver of an edge does not spike.
 */
constexpr qreal   k_coreMiter     = 6.0;
//! Mixed into the seed per lane (a 32-bit mixer's multiplier), like a brush's strokes.
constexpr quint32 k_bristleStream = 0xC2B2AE35u;

/**
 * \brief Smooth noise round a closed edge @p length long, about @p wave per span: a whole number of
 *        spans to the lap, repeating, so it closes on itself where the walk does.
 */
qreal loopedNoise(quint32 seed, qreal at, qreal length, qreal wave)
{
    const qint64 spans = qMax<qint64>(1, qRound64(length / wave));
    return smoothNoise(seed, at / length * spans, spans);
}

//! How many times @p rings wind round @p p, counted the shoelace's way.
int windingAt(const QList<QPolygonF>& rings, const QPointF& p)
{
    int w = 0;
    for (const QPolygonF& ring : rings)
        for (int i = 0; i < ring.size(); ++i) {
            const QPointF& a     = ring[i];
            const QPointF& b     = ring[(i + 1) % ring.size()];
            const qreal    cross = (b.x() - a.x()) * (p.y() - a.y()) - (p.x() - a.x()) * (b.y() - a.y());
            if (a.y() <= p.y() && b.y() > p.y() && cross > 0.0)
                ++w;
            else if (a.y() > p.y() && b.y() <= p.y() && cross < 0.0)
                --w;
        }
    return w;
}

/**
 * \brief Adds to @p out the whole outline along every closed edge of @p silhouette, @p core either side
 *        of it, mitred so a point stays a point: wound as a ribbon left side forward is (shoelace()), so
 *        the strokes laid over it add to it and do not cancel it.
 */
void addCore(QPainterPath& out, const QPainterPath& silhouette, qreal core)
{
    QPainterPathStroker pen;
    pen.setWidth(2.0 * core);
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setMiterLimit(k_coreMiter);
    for (const QPolygonF& ring : silhouette.toSubpathPolygons()) {
        if (ring.size() < 3)
            continue;
        QPainterPath outline;
        outline.addPolygon(ring);
        outline.closeSubpath();
        QList<QPolygonF> rings = pen.createStroke(outline).toSubpathPolygons();
        if (windingAt(rings, ring.first()) > 0)
            for (QPolygonF& r : rings)
                std::reverse(r.begin(), r.end());
        for (const QPolygonF& r : std::as_const(rings)) {
            out.addPolygon(r);
            out.closeSubpath();
        }
    }
}

/**
 * \brief How much of a hair @p offset off @p edge is left at @p at, where the edge bends: none where the bend's
 *        centre is nearer than the hair on its side, whole a @p lane before it. 1 on a straight or gentle
 *        stretch, and on a hair outside the bend.
 *
 * The bend is read over the hair's own distance either way, inside [@p from, @p to] (a stretch between
 * corners, so a corner is never read as a bend): its radius is that arc's length over its turn.
 */
qreal bendFade(const EdgeWalk& edge, qreal at, qreal offset, qreal lane, qreal from, qreal to)
{
    const qreal length = edge.length();
    const auto  wrap   = [length](qreal s) { return s - std::floor(s / length) * length; };
    const qreal reach  = std::abs(offset);
    const qreal a0     = qMax(from, at - reach);
    const qreal a1     = qMin(to, at + reach);
    const QPointF t1   = edge.at(wrap(a0)).tangent;
    const QPointF t2   = edge.at(wrap(a1)).tangent;
    const qreal turn   = std::atan2(t1.x() * t2.y() - t1.y() * t2.x(), QPointF::dotProduct(t1, t2));
    // A turn towards the normal (-t.y, t.x) bends round a centre on the side of a positive offset.
    if (turn * offset <= 0.0 || lane <= 0.0)
        return 1.0;
    const qreal radius = (a1 - a0) / std::abs(turn);
    return qBound(0.0, (radius - reach) / lane, 1.0);
}

/**
 * \brief The dry brush round @p outline.
 *
 * A **core**, `drybrushCore` of the line's width, is the plain outline, mitred so a point stays a
 * point: always whole, so the fill is never open to what is round it. Either side of it the rest of the
 * width is shared into lanes, `drybrushBristles` in all, one **hair** each: a thin ribbon along the
 * edge, wandering in its lane, there where its noise is under a dry level and narrowing away over it.
 * The level rises from `drybrushDryness` next to the core most of the way to bone dry at the line's
 * edge (`k_bristleThinning`), so the hairs thin out away from it.
 *
 * The core follows the joined silhouette; the hairs follow each **part** of it on its own edge
 * (OutlineModel::parts: the balloon, each tail). Where a part's edge goes under another part, its hairs run
 * on along it from where it was in sight, until their noise ends them, `k_partRunOn` half-widths at most:
 * the balloon's on into a tail's root, the tail's back into the balloon, crossing into a natural hatch.
 * So a tail's root is no corner at all.
 *
 * The hairs do not follow a **corner** of a part round (the model's: an offset turned round one folds over
 * itself); a tail's round end has none.
 * Between corners they run edge to edge; at a convex corner each runs on straight past where the other
 * side's hairs lie, so the two sides cross in a hatch, narrowing away over that run. At a **concave** one
 * (between a shout's spikes) each runs on straight until it meets the other side's core (its line carried
 * on past the corner), narrowing away just before it (`k_rootTaper`). At a **point** (a convex corner
 * under `k_pointAngle`, a tail's tip) they run into it instead, closing on the core so it ends sharp.
 * Round a **bend** tighter than a hair is far inside the edge (a round tail end), the hair would cross
 * the bend's centre and fold over itself: it fades away there instead, over its last lane before that
 * centre (bendFade()).
 *
 * Built only of rings wound one way (the core's turned to match), so the winding rule paints their
 * overlaps as one and nothing is subtracted: a boolean on paths this dense costs a frame.
 */
QPainterPath drybrushInk(const StyleProperties& s, const OutlineModel& outline, quint32 baseSeed)
{
    const qreal            half    = qMax(s.strokeWidth, 0) / 2.0;
    const qreal            core    = half * qBound(0, s.drybrush.core, 100) / 100.0;
    const int              perSide = qMax(1, s.drybrush.bristles / 2);
    const qreal            lane    = (half - core) / perSide;
    const qreal            dryness = qBound(0, s.drybrush.dryness, 100) / 100.0;
    const qreal            rootTaper = k_rootTaper * qMax(lane, k_brushStep / 2.0);   // a lane may be none

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (half <= 0.0)
        return out;   // no outline, no brush: its width is the outline's
    // The core, along the joined silhouette, wound as the hairs are.
    addCore(out, outline.joined, core);

    // The hairs, along each part's own edge.
    const qreal underReach = k_partRunOn * half;
    quint32     stream     = 0;   // a number per lane, for its own noise
    for (const OutlinePart& part : outline.parts)
    for (const OutlineRing& ring : part.rings) {
        const EdgeWalk& edge   = ring.walk;
        const qreal     length = edge.length();
        const auto wrap = [&ring](qreal at) { return ring.wrap(at); };

        // The stretches between corners; with none, one round the whole loop.
        const std::vector<OutlineCorner>& corners   = ring.corners;
        const bool                        convexWay = ring.clockwise;   // a turn this way is convex
        struct Stretch
        {
            qreal             from, to;
            const OutlineCorner* start;
            const OutlineCorner* end;
        };
        std::vector<Stretch> stretches;
        if (corners.empty())
            stretches.push_back({0.0, length, nullptr, nullptr});
        for (size_t i = 0; i < corners.size(); ++i) {
            const OutlineCorner& c    = corners[i];
            const OutlineCorner& next = corners[(i + 1) % corners.size()];
            stretches.push_back({c.at, next.at + (next.at > c.at ? 0.0 : length), &c, &next});
        }
        const auto concave = [&](const OutlineCorner* c) { return c && (c->turn > 0.0) != convexWay; };
        // How far from a point its hairs start closing on it: where the line's two edges meet.
        const auto closing = [&](const OutlineCorner* c) {
            if (!c || concave(c))
                return 0.0;
            const qreal inner = M_PI - std::abs(c->turn);
            return inner < qDegreesToRadians(k_pointAngle) ? half / std::tan(inner / 2.0) : 0.0;
        };

        for (int b = 0; b < 2 * perSide; ++b, ++stream) {
            const int     side   = b < perSide ? -1 : 1;
            const int     away   = b % perSide;                 // lanes from the core
            const qreal   offset = side * (core + lane * (away + 0.5));
            const qreal   dry    = dryness + (1.0 - dryness) * k_bristleThinning * (away + 0.5) / perSide;
            const qreal   level  = 1.0 + k_bristleSwell - (2.0 + k_bristleSwell) * dry;
            const quint32 seed   = baseSeed ^ ((stream + 1) * k_bristleStream);
            // The hair's half-width @p at along the edge: where its noise is under the dry level.
            const auto width = [&](qreal at) {
                return lane * k_bristleHair
                     * qBound(0.0, (level - loopedNoise(seed, wrap(at), length, k_bristleWave)) / k_bristleSwell, 1.0);
            };

            for (const Stretch& st : stretches) {
                // The hair's middle and its normal at each step, then its half-width: the runs where that
                // is over nothing become ribbons.
                struct Step
                {
                    QPointF mid, n;
                    qreal   w;
                    qreal   at     = 0.0;
                    bool    hidden = false;   //!< Under another part.
                };
                std::vector<Step> steps;
                const qreal closeStart = closing(st.start);
                const qreal closeEnd   = closing(st.end);
                // At a concave end, the other side's core: the line through the corner along that side, its
                // normal turned towards this side, so a hair is stopped where its distance to it is the core's.
                struct Root
                {
                    QPointF at, n;
                    qreal   along = -1.0;   //!< Where along the edge the corner is; none if not concave.
                };
                const auto rootOf = [&](const OutlineCorner* c, qreal mine) {   // mine: -1 this side before it
                    Root r;
                    if (!concave(c))
                        return r;
                    r.at                = edge.at(wrap(c->at)).at;
                    const QPointF other = mine > 0.0 ? c->in : c->out;
                    const QPointF own   = mine > 0.0 ? c->out : c->in;
                    r.n                 = QPointF(-other.y(), other.x());
                    if (QPointF::dotProduct(own * mine, r.n) < 0.0)
                        r.n = -r.n;
                    r.along = c->at;
                    return r;
                };
                const Root rootStart = rootOf(st.start, 1.0);
                const Root rootEnd   = rootOf(st.end, -1.0);
                // How much of a hair is left at @p p by a root's other side: none at its core, whole a little
                // before it. Only near the corner.
                const auto stop = [&](const Root& r, const QPointF& p) {
                    if (r.along < 0.0 || QLineF(p, r.at).length() > k_rootReach * half)
                        return 1.0;
                    return qBound(0.0, (QPointF::dotProduct(p - r.at, r.n) - core) / rootTaper, 1.0);
                };
                const qreal span       = st.to - st.from;
                const int   count      = qMax(2, qCeil(span / k_brushStep));
                for (int k = 0; k <= count; ++k) {
                    // Just inside the stretch at its ends, so the tangent is this side's, not the corner's.
                    const qreal   at   = st.from + qBound(0.01, span * k / count, span - 0.01);
                    const auto    p    = edge.at(wrap(at));
                    const QPointF n(-p.tangent.y(), p.tangent.x());
                    qreal         into = 1.0;   // 0 at a point, 1 clear of it
                    if (closeStart > 0.0)
                        into = qMin(into, (at - st.from) / closeStart);
                    if (closeEnd > 0.0)
                        into = qMin(into, (st.to - at) / closeEnd);
                    into = qBound(0.0, into, 1.0);
                    const qreal   c   = (offset + lane * k_bristleWander
                                                * loopedNoise(seed ^ k_brushStream, wrap(at), length, k_bristleWave)) * into;
                    const QPointF mid = p.at + n * c;
                    const qreal   bent = bendFade(edge, at, offset, lane, st.from + 0.01, st.to - 0.01);
                    steps.push_back({mid, n, width(at) * into * bent * stop(rootStart, mid) * stop(rootEnd, mid),
                                     at, ring.coveredAt(at)});
                }
                // Under another part, a hair runs on from where it was in sight, either way, as long as its
                // noise lets it and no further than the reach, narrowing over its last third; then it is gone
                // for good, so nothing starts again in there.
                const auto runUnder = [&](Step& s, qreal d) {
                    if (s.w <= 0.0 || d > underReach)
                        return false;
                    s.w *= qMin(1.0, 3.0 * (1.0 - d / underReach));
                    return true;
                };
                for (size_t i = 0; i < steps.size();) {
                    if (!steps[i].hidden) {
                        ++i;
                        continue;
                    }
                    size_t j = i;
                    while (j < steps.size() && steps[j].hidden)
                        ++j;
                    size_t k = i;   // [i, k) runs on from before, [m, j) from after, the rest is gone
                    while (i > 0 && k < j && runUnder(steps[k], steps[k].at - steps[i - 1].at))
                        ++k;
                    size_t m = j;
                    while (j < steps.size() && m > k && runUnder(steps[m - 1], steps[j].at - steps[m - 1].at))
                        --m;
                    for (size_t q = k; q < m; ++q)
                        steps[q].w = 0.0;
                    i = j;
                }
                // Across a corner that is not a point, the hair runs on straight. Past a convex one it narrows
                // away over a fixed run; past a concave one it keeps its own width (its noise read on along the
                // edge) until the other side's core stops it, or the noise does.
                const auto runOn = [&](const Step& from, qreal dir, const OutlineCorner* c, qreal close,
                                       const Root& root) {
                    std::vector<Step> more;
                    if (!c || close > 0.0 || from.w <= 0.0)
                        return more;
                    const QPointF t(from.n.y(), -from.n.x());   // the tangent back from the normal
                    if (root.along >= 0.0) {
                        for (qreal past = k_brushStep; past <= k_rootReach * half; past += k_brushStep) {
                            const QPointF mid = from.mid + t * (dir * past);
                            const qreal   w   = width(c->at + dir * past) * stop(root, mid);
                            if (w <= 0.0)
                                break;   // ended: it does not start again past the corner
                            more.push_back({mid, from.n, w});
                        }
                        return more;
                    }
                    const qreal   reach = (std::abs(offset) + k_hatchReach * half)
                                        * (1.25 + 0.25 * smoothNoise(seed ^ k_bristleStream, c->at / k_bristleWave));
                    const int     n     = qMax(2, qCeil(reach / k_brushStep));
                    for (int k = 1; k <= n; ++k)
                        more.push_back({from.mid + t * (dir * reach * k / n), from.n,
                                        from.w * (1.0 - qreal(k) / n)});
                    return more;
                };
                std::vector<Step> before = runOn(steps.front(), -1.0, st.start, closeStart, rootStart);
                std::vector<Step> after  = runOn(steps.back(), 1.0, st.end, closeEnd, rootEnd);
                std::reverse(before.begin(), before.end());
                steps.insert(steps.begin(), before.begin(), before.end());
                steps.insert(steps.end(), after.begin(), after.end());

                QPolygonF left, right;
                const auto flush = [&] {
                    if (left.size() > 1) {
                        out.addPolygon(left + right);
                        out.closeSubpath();
                    }
                    left.clear();
                    right.clear();
                };
                for (const Step& step : steps) {
                    if (step.w <= 0.0) {
                        flush();
                        continue;
                    }
                    left << step.mid + step.n * step.w;
                    right.prepend(step.mid - step.n * step.w);
                }
                flush();
            }
        }
    }
    return out;
}

/**
 * \brief The line's half-width three times over, a hair run on across a corner reaching past it by its
 *        lane and as far again; or the core's miter at a point, if a thick core reaches further.
 */
qreal drybrushReach(const StyleProperties& s)
{
    const qreal core = qBound(0, s.drybrush.core, 100) / 100.0;
    return qMax(3.0, 2.0 * k_coreMiter * core) * qMax(s.strokeWidth, 0) / 2.0;
}

// ---------------------------------------------------------------------------
// Rough: turbulence displacing the outline, then a blur (its filter is roughFilter(), below)
// ---------------------------------------------------------------------------

//! The displacement, and the blur's spread past it.
qreal roughReach(const TextureProperties& t)
{
    return qMax(t.rough.displacement, 0.0) + k_blurReach * qMax(t.rough.blur, 0.0);
}

// ---------------------------------------------------------------------------
// Sketch: a thin whole outline, and many short pen strokes over it
// ---------------------------------------------------------------------------

//! How much a stroke's length varies about `sketchLength`, as a share of it.
constexpr qreal   k_sketchVary    = 0.3;
//! How much a stroke's stiffness varies about `sketchStiffness`, as a share of it: hand-drawn, not stamped.
constexpr qreal   k_sketchStiffVary = 0.4;
//! How far a stroke's start is moved off an even share of the lap, in shares, so the starts do not line up.
constexpr qreal   k_sketchScatter = 0.5;
//! How much a stroke's width varies along it, as a share of the width: a pen's, less than a brush's.
constexpr qreal   k_sketchSwell   = 0.2;
//! How long one swell is along a stroke, in balloon pixels.
constexpr qreal   k_sketchWave    = 24.0;
//! Mixed into the seed for the strokes' places, and per stroke for its noise: a stream of its own.
constexpr quint32 k_sketchStream  = 0x27D4EB2Fu;
//! At full `sketchBlots`, one blot every this many balloon pixels along the line.
constexpr qreal   k_blotSpacing   = 12.0;
//! A blot's radius at the smallest and the largest, in a stroke's half-widths.
constexpr qreal   k_blotSmallest  = 0.7;
constexpr qreal   k_blotLargest   = 1.8;
//! The share of blots that are short strokes rather than spots.
constexpr qreal   k_blotStrokes   = 0.5;
//! A short stroke's length at the longest, in its radii, end to end.
constexpr qreal   k_blotLongest   = 8.0;
//! How far a spot's edge wanders in and out, as a share of its radius: a drop, not a stamped circle.
constexpr qreal   k_blotWobble    = 0.25;
//! The points round a blot's outline.
constexpr int     k_blotPoints    = 20;
//! How far from a round end a stroke starts to lose its stiffness and its offset, in the end's radii.
constexpr qreal   k_roundEase     = 2.0;
//! Mixed into the seed for the blots: a stream of their own, so they leave the strokes as they were.
constexpr quint32 k_blotStream    = 0x165667B1u;

//! A pen's pressure at @p u (0..1 along a stroke): landing over @p entry, lifting off over @p exit, eased.
qreal penPressure(qreal u, qreal entry, qreal exit)
{
    const qreal in  = entry > 0.0 ? qBound(0.0, u / entry, 1.0) : 1.0;
    const qreal out = exit > 0.0 ? qBound(0.0, (1.0 - u) / exit, 1.0) : 1.0;
    return std::sin(qMin(in, out) * M_PI / 2.0);
}

/**
 * \brief A blot of ink at @p at, @p r in radius, wound as a ribbon left side forward is (shoelace()) so it
 *        adds to the strokes it lies on: a spot whose edge wanders, or, @p length long along @p along, a
 *        short stroke with round ends.
 */
QPolygonF blot(const QPointF& at, const QPointF& along, qreal r, qreal length, quint32 seed)
{
    QPolygonF out;
    const QPointF across(-along.y(), along.x());
    const qreal   reach = qMax(0.0, length / 2.0 - r);   // from the middle to either end's centre
    for (int i = 0; i < k_blotPoints; ++i) {
        const qreal   angle = 2.0 * M_PI * i / k_blotPoints;
        const qreal   rr    = r * (1.0 + k_blotWobble * smoothNoise(seed, qreal(i) / 4.0, k_blotPoints / 4));
        const QPointF dir   = along * std::cos(angle) + across * std::sin(angle);
        out << at + along * (std::cos(angle) >= 0.0 ? reach : -reach) + dir * rr;
    }
    if (shoelace(out) > 0.0)
        std::reverse(out.begin(), out.end());
    return out;
}

/**
 * \brief The sketch round every closed edge of @p outline: a thin whole outline, and short strokes over it.
 *
 * The **outline**, `sketchCore` of the line's width, is the drybrush's core (addCore()): the fill is always
 * closed off, whatever the strokes do. The **strokes**, `sketchStrokeWidth` of the line's width, are
 * `sketchLength` long give or take k_sketchVary, as many as put `sketchLayers` of them over any place, their
 * starts spread round the lap. A short stroke does not bend with the curve as a long one would: each point
 * is the outline's, pulled by its stiffness (`sketchStiffness`, varied per stroke) towards the straight line
 * along the outline at the stroke's middle, so both its ends leave a curve alike and a run of them does not
 * saw. Its offset off the outline goes from one value within `sketchSpread` to another, so neighbours cross.
 * At a corner of the outline the pen lifts: the stroke ends there and goes on from it as one of its own. It
 * lifts too where a tail's round end begins and ends, and goes round the end on the outline itself, having
 * lost its stiffness and its offset over k_roundEase of the end's radius before it: straightened, a stroke
 * would leave the half circle; offset inwards past its radius, it would fold over itself.
 *
 * Its width is the pen's pressure (penPressure()), the way it goes on screen (`sketchClockwise`): a short
 * landing, a long lift-off. A stroke is a lap long at most.
 *
 * **Blots**, `sketchBlots` of one every k_blotSpacing, lie along the line within `sketchSpread`, of a stream
 * of their own: spots whose edge wanders, and short strokes with round ends along the line (blot()).
 */
QPainterPath sketchInk(const StyleProperties& s, const OutlineModel& outline, quint32 baseSeed)
{
    const qreal            width  = qMax(s.strokeWidth, 0);
    const qreal            half   = width / 2.0 * qBound(0, s.sketch.strokeWidth, 100) / 100.0;
    const qreal            length = qMax(s.sketch.length, 1.0);
    const qreal            stiff  = qBound(0, s.sketch.stiffness, 100) / 100.0;
    const qreal            spread = qMax(s.sketch.spread, 0.0);
    const qreal            entry  = qBound(0, s.sketch.entry, 100) / 100.0;
    const qreal            exit   = qBound(0, s.sketch.exit, 100) / 100.0;

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (width <= 0.0)
        return out;
    if (const qreal core = width / 2.0 * qBound(0, s.sketch.core, 100) / 100.0; core > 0.0)
        addCore(out, outline.joined, core);
    if (half <= 0.0)
        return out;

    QRandomGenerator rng(baseSeed ^ k_sketchStream);
    QRandomGenerator spots(baseSeed ^ k_blotStream);
    quint32          stroke = 0;   // a number per stroke, for its own noise
    for (const OutlineRing& ring : outline.rings) {
        const EdgeWalk& edge = ring.walk;
        const qreal     lap  = edge.length();
        // The walk's way round on screen; against the pen's, the pressure is read from the other end.
        const bool reversed = ring.clockwise != s.sketch.clockwise;
        const qreal stroked = qMin(length, lap);   // a stroke is a lap long at most
        const int  count    = qMax(1, qRound(qMax(s.sketch.layers, 1) * lap / stroked));
        const auto wrap     = [&ring](qreal at) { return ring.wrap(at); };
        // Where the pen lifts, in order round the lap: the corners, and either side of a round end.
        std::vector<qreal> corners;
        for (const OutlineCorner& c : ring.corners)
            corners.push_back(c.at);
        for (const OutlineEnd& e : ring.ends)
            if (e.round) {
                corners.push_back(wrap(e.from));
                corners.push_back(wrap(e.from + e.span));
            }
        std::sort(corners.begin(), corners.end());
        // How much of a stroke's stiffness and offset is left at @p at: none on a round end, all clear of it.
        const auto ease = [&](qreal at) {
            qreal w = 1.0;
            for (const OutlineEnd& e : ring.ends) {
                if (!e.round || e.radius <= 0.0)
                    continue;
                if (ring.within(e, at))
                    return 0.0;
                const qreal off = qMin(wrap(e.from - at), wrap(at - e.from - e.span));
                w = qMin(w, off / (k_roundEase * e.radius));
            }
            return w;
        };

        // One pen stroke from @p from to @p to along the edge.
        const auto draw = [&](qreal from, qreal to, qreal offFrom, qreal offTo, qreal k, quint32 seed) {
            const qreal len = to - from;
            if (len < 2.0 * k_brushStep)
                return;   // a sliver between a corner and the stroke's end: the pen does not land for it
            const auto    mid     = edge.at(wrap(from + len / 2.0));
            const int     samples = qMax(2, qCeil(len / k_brushStep));
            QPolygonF     centre;
            std::vector<qreal> widths;
            centre.reserve(samples + 1);
            widths.reserve(size_t(samples) + 1);
            for (int i = 0; i <= samples; ++i) {
                const qreal   u        = qreal(i) / samples;
                const qreal   at       = from + len * u;
                const auto    p        = edge.at(wrap(at));
                const QPointF n(-p.tangent.y(), p.tangent.x());
                const qreal   w        = ease(at);
                const qreal   off      = (offFrom + (offTo - offFrom) * u) * w;
                const QPointF bent     = p.at + n * off;
                const QPointF straight = mid.at + n * off + mid.tangent * (len * (u - 0.5));
                centre << bent + (straight - bent) * (k * w);
                widths.push_back(half * penPressure(reversed ? 1.0 - u : u, entry, exit)
                                 * (1.0 + k_sketchSwell * smoothNoise(seed, at / k_sketchWave)));
            }
            // The ribbon about the stroke's own line, its sides square to the line where it goes.
            QPolygonF left, right;
            for (int i = 0; i <= samples; ++i) {
                const QPointF d = centre.at(qMin(i + 1, samples)) - centre.at(qMax(i - 1, 0));
                const qreal   l = std::hypot(d.x(), d.y());
                const QPointF n = l > 0.0 ? QPointF(-d.y(), d.x()) / l : QPointF();
                left << centre.at(i) + n * widths[size_t(i)];
                right.prepend(centre.at(i) - n * widths[size_t(i)]);
            }
            out.addPolygon(left + right);
            out.closeSubpath();
        };

        for (int j = 0; j < count; ++j, ++stroke) {
            // All drawn before any is used, so a change to one row does not re-deal the others.
            const qreal   start   = (j + k_sketchScatter * rng.generateDouble()) * lap / count;
            const qreal   len     = qMin(stroked * (1.0 + k_sketchVary * (2.0 * rng.generateDouble() - 1.0)), lap);
            const qreal   offFrom = spread * (2.0 * rng.generateDouble() - 1.0);
            const qreal   offTo   = spread * (2.0 * rng.generateDouble() - 1.0);
            const qreal   k       = qBound(0.0, stiff * (1.0 + k_sketchStiffVary * (2.0 * rng.generateDouble() - 1.0)), 1.0);
            const quint32 seed    = baseSeed ^ ((stroke + 1) * k_sketchStream);
            // Cut at every corner it crosses, a lap later too, since a stroke may run past the lap's end. The
            // offset goes on across the cut, so the pieces still lie as one stroke would.
            qreal from = start;
            for (int round = 0; round < 2; ++round) {
                for (qreal c : corners) {
                    const qreal at = c + round * lap;
                    if (at > from && at < start + len) {
                        const auto offAt = [&](qreal x) { return offFrom + (offTo - offFrom) * (x - start) / len; };
                        draw(from, at, offAt(from), offAt(at), k, seed);
                        from = at;
                    }
                }
            }
            draw(from, start + len, offFrom + (offTo - offFrom) * (from - start) / len, offTo, k, seed);
        }

        const int blots = qRound(qBound(0, s.sketch.blots, 100) / 100.0 * lap / k_blotSpacing);
        for (int j = 0; j < blots; ++j) {
            // All drawn before any is used, so a change to one row does not re-deal the others.
            const qreal   at     = lap * spots.generateDouble();
            const qreal   off    = spread * (2.0 * spots.generateDouble() - 1.0);
            const qreal   r      = half * (k_blotSmallest + (k_blotLargest - k_blotSmallest) * spots.generateDouble());
            const bool    isLine = spots.generateDouble() < k_blotStrokes;
            const qreal   length = r * (2.0 + (k_blotLongest - 2.0) * spots.generateDouble());
            const quint32 seed   = spots.generate();
            const auto    p      = edge.at(at);
            out.addPolygon(blot(p.at + QPointF(-p.tangent.y(), p.tangent.x()) * off, p.tangent, r,
                                isLine ? length : 2.0 * r, seed));
            out.closeSubpath();
        }
    }
    return out;
}

//! Half a stroke at its widest swell, as far as it lies off the outline, and as far as its stiffness takes
//! it: a point of the straight line is no further from the outline's than the way from the stroke's middle.
//! A blot reaches as far as its largest radius, wobbled, and half its length where a straight blot leaves
//! a curve.
qreal sketchReach(const StyleProperties& s)
{
    const qreal            width = qMax(s.strokeWidth, 0);
    const qreal            stroke = width / 2.0 * qBound(0, s.sketch.strokeWidth, 100) / 100.0;
    const qreal            blots = s.sketch.blots > 0
                                     ? stroke * k_blotLargest * (1.0 + k_blotWobble + k_blotLongest / 2.0) : 0.0;
    const qreal            half  = qMax(width / 2.0 * qMax(qBound(0, s.sketch.strokeWidth, 100) / 100.0 * (1.0 + k_sketchSwell),
                                                           qBound(0, s.sketch.core, 100) / 100.0 * k_coreMiter),
                                        blots);
    const qreal            stiff = qMin(1.0, qBound(0, s.sketch.stiffness, 100) / 100.0 * (1.0 + k_sketchStiffVary));
    return half + qMax(s.sketch.spread, 0.0) + stiff * qMax(s.sketch.length, 1.0) * (1.0 + k_sketchVary);
}

// ---------------------------------------------------------------------------
// Explosion: a dry-brush ring with a flash's halo round it
// ---------------------------------------------------------------------------

//! How many times denser than the flash's outer spacing an explosion's spikes stand.
constexpr qreal k_haloDensity = 2.5;
//! How much thinner than the flash's outer width an explosion's spike is where it leaves the ring: a hair.
constexpr qreal k_haloThinning = 0.3;

/**
 * \brief @p style with the flash's numbers made into an explosion's halo: the outer strokes only, k_haloDensity
 *        times as dense and k_haloThinning as wide at their base. Their length, every scatter and the
 *        direction stay the flash's own.
 */
StyleProperties haloOf(StyleProperties style)
{
    style.flash.innerLength = 0.0;
    style.flash.outerPitch  = qMax(style.flash.outerPitch, 0.0) / k_haloDensity;
    style.flash.outerBase   = qMax(style.flash.outerBase, 0.0) * k_haloThinning;
    return style;
}

/**
 * \brief The dry brush's ring (drybrushInk()) in a halo of hairs flying out of it (haloOf(), drawn as
 *        flashInk()). The halo's strokes are wound as the ring's are, so where they overlap they add and
 *        never cut each other out.
 */
QPainterPath explosionInk(const StyleProperties& s, const OutlineModel& outline, quint32 seed)
{
    QPainterPath out = drybrushInk(s, outline, seed);
    for (QPolygonF stroke : flashInk(haloOf(s), outline, seed).toSubpathPolygons()) {
        if (shoelace(stroke) > 0.0)
            std::reverse(stroke.begin(), stroke.end());
        out.addPolygon(stroke);
        out.closeSubpath();
    }
    return out;
}

//! As far as either half reaches.
qreal explosionReach(const StyleProperties& s)
{
    return qMax(drybrushReach(s), flashReach(haloOf(s)));
}

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

using Line    = StyleProperties::Line;
using Texture = TextureProperties::Kind;

//! In the enum's order, so a kind is its own index; lineInfo() checks every entry names its kind.
//! A noise's seed as feTurbulence takes it.
constexpr quint32 k_noiseSeeds = 65536u;

//! How detailed dry ink's noise is: specks, not the Rough's long waves.
constexpr int   k_dryInkOctaves   = 3;
//! The noise level that wears the outline at no wear and at full wear: fractal noise sits about its middle.
constexpr qreal k_dryInkWornAt0   = 0.3;
constexpr qreal k_dryInkWornAt100 = 0.7;
//! How hard the edge of a hole is: the threshold's slope, so a speck is crisp and not a smear.
constexpr qreal k_dryInkEdge      = 25.0;

/**
 * \brief Dry ink's filter: the noise's red as an alpha, cut at the wear by a steep slope, below it a hole,
 *        above it ink; composited *in* the outline, so the holes are the paper showing through.
 */
Render::Filter dryInkFilter(const StyleProperties& s, quint32 seed)
{
    using Render::number;
    const qreal            wear = qBound(0, s.dryInk.wear, 100) / 100.0;
    const qreal            at   = k_dryInkWornAt0 + (k_dryInkWornAt100 - k_dryInkWornAt0) * wear;
    Render::Filter         f;
    f.primitives.append({QStringLiteral("feTurbulence"),
                         {{QStringLiteral("type"), QStringLiteral("fractalNoise")},
                          {QStringLiteral("baseFrequency"), number(qMax(s.dryInk.grain, 0.0), Render::k_valueDecimals)},
                          {QStringLiteral("numOctaves"), QString::number(k_dryInkOctaves)},
                          {QStringLiteral("seed"), QString::number(seed % k_noiseSeeds)},
                          {QStringLiteral("result"), QStringLiteral("pmNoise")}},
                         {}});
    f.primitives.append({QStringLiteral("feColorMatrix"),
                         {{QStringLiteral("in"), QStringLiteral("pmNoise")},
                          {QStringLiteral("type"), QStringLiteral("matrix")},
                          {QStringLiteral("values"), QStringLiteral("0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  1 0 0 0 0")},
                          {QStringLiteral("result"), QStringLiteral("pmGrain")}},
                         {}});
    f.primitives.append({QStringLiteral("feComponentTransfer"),
                         {{QStringLiteral("in"), QStringLiteral("pmGrain")}, {QStringLiteral("result"), QStringLiteral("pmWorn")}},
                         {{QStringLiteral("feFuncA"),
                           {{QStringLiteral("type"), QStringLiteral("linear")},
                            {QStringLiteral("slope"), number(k_dryInkEdge)},
                            {QStringLiteral("intercept"), number(-k_dryInkEdge * at)}},
                           {}}}});
    f.primitives.append({QStringLiteral("feComposite"),
                         {{QStringLiteral("in"), QStringLiteral("SourceGraphic")},
                          {QStringLiteral("in2"), QStringLiteral("pmWorn")},
                          {QStringLiteral("operator"), QStringLiteral("in")}},
                         {}});
    return f;
}

//! Dry ink's unworn middle, as a share of the outline's width.
qreal dryInkUnworn(const StyleProperties& s)
{
    return qBound(0, s.dryInk.core, 100) / 100.0;
}

// The sections' rows: what the Line panel calls each number, and what its spin offers. Lengths in strip pixels.
#define PM_TR(s) QT_TRANSLATE_NOOP("QObject", s)
const Fields::RowSpec k_flashRows[] = {
    {"flashOuterPitch",        PM_TR("Outer spacing:"),  1, 100, " px", PM_TR("One stroke every this many pixels along the outside of the edge.")},
    {"flashInnerPitch",        PM_TR("Inner spacing:"),  1, 100, " px", PM_TR("One stroke every this many pixels along the inside of the edge.")},
    {"flashOuterLength",       PM_TR("Outer length:"),   0, 200, " px", PM_TR("How far an outer stroke reaches out from the edge.")},
    {"flashInnerLength",       PM_TR("Inner length:"),   0, 200, " px", PM_TR("How far an inner stroke reaches in from the edge.")},
    {"flashOuterBase",         PM_TR("Outer width:"),    0, 50,  " px", PM_TR("An outer stroke's width where it leaves the edge.")},
    {"flashInnerBase",         PM_TR("Inner width:"),    0, 50,  " px", PM_TR("An inner stroke's width where it leaves the edge.")},
    {"flashOuterPitchJitter",  PM_TR("Outer scatter:"),  0, 100, " %",  PM_TR("How much each outer stroke's place varies, as a share of the spacing.")},
    {"flashInnerPitchJitter",  PM_TR("Inner scatter:"),  0, 100, " %",  PM_TR("How much each inner stroke's place varies, as a share of the spacing.")},
    {"flashOuterLengthJitter", PM_TR("Outer variance:"), 0, 100, " %",  PM_TR("How much each outer stroke's length varies.")},
    {"flashInnerLengthJitter", PM_TR("Inner variance:"), 0, 100, " %",  PM_TR("How much each inner stroke's length varies.")},
    {"flashCoreStretch",       PM_TR("Direction:"),      FlashParams::k_autoStretch, 100, "",
     PM_TR("What the strokes point away from: at 0 % the centre; more stretches it into a line along the "
           "longer side, which stands the strokes square to a long straight edge. Auto takes the shape's "
           "own, and follows it when the shape changes; a value set here stays.")},
};

//! The brush's numbers. Its width is the line's own, the *Width* row above them.
const Fields::RowSpec k_brushRows[] = {
    {"brushStrokes", PM_TR("Strokes:"), 1, 12,  "",    PM_TR("How many strokes go round the edge.")},
    {"brushOverlap", PM_TR("Overlap:"), 0, 100, " %",  PM_TR("How far each stroke runs past where the next begins, as a share of its length.")},
    {"brushTaper",   PM_TR("Taper:"),   0, 50,  " %",  PM_TR("How much of a stroke, at each end, narrows to its point.")},
    {"brushSwell",   PM_TR("Swell:"),   0, 100, " %",  PM_TR("How much a stroke's width varies along it. Its width is the outline's stroke width.")},
    {"brushDrift",   PM_TR("Drift:"),   0, 20,  " px", PM_TR("How far a stroke wanders off the edge."), 1},
};

//! The dry brush's numbers. Its width is the line's own too, shared among the bristles.
const Fields::RowSpec k_drybrushRows[] = {
    {"drybrushCore",     PM_TR("Outline:"),  0, 100, " %", PM_TR("How wide the unbroken line in the middle is, as a share of the width. It keeps the fill apart from what is round it; the hairs share the rest.")},
    {"drybrushBristles", PM_TR("Bristles:"), 1, 40,  "",   PM_TR("How many hairs across the line, half each side of its thin core: more makes them finer.")},
    {"drybrushDryness",  PM_TR("Dryness:"),  0, 100, " %", PM_TR("How dry the brush is: the share of gaps in the hairs by the core. They thin out away from it; the core always stays.")},
};

//! The dashed line's numbers. Its width is the line's own.
const Fields::RowSpec k_dashRows[] = {
    {"dashLength", PM_TR("Dash:"), 1, 200, " px", PM_TR("How long a dash is along the edge.")},
    {"dashGap",    PM_TR("Gap:"),  0, 200, " px", PM_TR("The gap between two dashes. It is stretched or squeezed a little so whole dashes go round; 0 draws a whole line.")},
};

//! The multi-line's numbers. The outline's width is the line's own.
const Fields::RowSpec k_multiRows[] = {
    {"multiLines", PM_TR("Lines:"),       2, 10,  "",    PM_TR("How many lines, the outline among them.")},
    {"multiGap",   PM_TR("Gap:"),         0, 100, " px", PM_TR("The space between one line and the next.")},
    {"multiWidth", PM_TR("Inner width:"), 0, 50,  " px", PM_TR("How wide each line inside the outline is. The outline's width is the Width above.")},
};

//! The sketch's numbers. Its width is the line's own.
const Fields::RowSpec k_sketchRows[] = {
    {"sketchCore",        PM_TR("Outline:"),       0, 100, " %",  PM_TR("How wide the whole thin line under the strokes is, as a share of the width. It keeps the fill closed off whatever the strokes do; 0 leaves only the strokes.")},
    {"sketchStrokeWidth", PM_TR("Stroke width:"),  1, 100, " %",  PM_TR("How wide a pen stroke is at its widest, as a share of the width.")},
    {"sketchLength",      PM_TR("Stroke length:"), 5, 1000, " px", PM_TR("How long a pen stroke is, a lap at most. Each varies a little about it. Short is a pen's hatching; long, loose strokes going round.")},
    {"sketchLayers",      PM_TR("Layers:"),        1, 12,  "",    PM_TR("How many strokes lie over any place of the outline: more makes the line denser.")},
    {"sketchStiffness",   PM_TR("Stiffness:"),     0, 100, " %",  PM_TR("How straight a stroke stays where the outline curves. 0 % follows the outline; more draws it straighter, so both its ends leave a curve. Each stroke varies a little about it.")},
    {"sketchSpread",      PM_TR("Spread:"),        0, 20,  " px", PM_TR("How far in or out a stroke lies from the outline. Each goes from one place to another within it, so neighbours cross."), 1},
    {"sketchEntry",       PM_TR("Taper in:"),      0, 100, " %",  PM_TR("How much of a stroke, at its start, narrows as the pen lands.")},
    {"sketchExit",        PM_TR("Taper out:"),     0, 100, " %",  PM_TR("How much of a stroke, at its end, narrows as the pen lifts off.")},
    {"sketchClockwise",   PM_TR("Direction:"),     0, 1,   "",    PM_TR("Which way round the pen goes: each stroke lands at its start and lifts off at its end."),
     0, 0.0, PM_TR("Clockwise"), PM_TR("Counter-clockwise")},
    {"sketchBlots",       PM_TR("Blots:"),         0, 100, " %",  PM_TR("Ink spilt along the line: round spots and short strokes with round ends, here and there. Re-roll deals them again.")},
};

//! Dry ink's numbers. Its width is the line's own.
const Fields::RowSpec k_dryInkRows[] = {
    {"dryInkGrain", PM_TR("Grain:"),   0.01, 1.0, "",
     PM_TR("How fine the specks are: higher gives smaller, closer holes in the line."), 2, 0.01},
    {"dryInkWear",  PM_TR("Wear:"),    0,    100, " %",
     PM_TR("How much of the line the paper eats.")},
    {"dryInkCore",  PM_TR("Outline:"), 0,    100, " %",
     PM_TR("How wide the middle of the line that stays whole is, as a share of the width. It keeps the fill "
           "closed off; the wear eats the line either side of it. 0 lets it eat right through.")},
};

//! Rough's numbers, the Texture panel's. Lengths in strip pixels, with a decimal: a blur's useful range is around a pixel.
const Fields::RowSpec k_roughRows[] = {
    // A step of 0.005: a wave's length is 1 / grain, so a change shows in proportion, and a thousandth
    // at the Marker's 0.03 is a wave 3 % shorter, nothing the eye sees.
    {"roughFrequency",    PM_TR("Grain:"),        0.001, 0.5, "",
     PM_TR("How fine the wobble is: about one wave every 1 / grain pixels, so 0.03 is a wave every 33 px "
           "and 0.07 one every 14 px. Higher gives smaller, closer waves."), 3, 0.005},
    {"roughOctaves",      PM_TR("Detail:"),       1,     8,   "",    PM_TR("How many finer wobbles ride on the first.")},
    {"roughDisplacement", PM_TR("Displacement:"), 0,     50,  " px", PM_TR("How far the outline wanders off its line."), 1},
    {"roughBlur",         PM_TR("Blur:"),         0,     10,  " px", PM_TR("How much the balloon softens after the wobble, as ink bleeding into paper. 0 keeps it sharp."), 1},
    {"roughFill",         PM_TR("Roughen the fill too"), 0, 1, "",
     PM_TR("Off, only the outline and the fill's edge wobble, and a pattern in the fill stays on its grid. On, "
           "the fill wobbles with them, pattern and all.")},
};
#undef PM_TR

const std::array<LineInfo, int(StyleProperties::k_lastLine) + 1> k_lines{{
    {Line::Clean, "clean", QT_TRANSLATE_NOOP("QObject", "Clean"), nullptr,  nullptr},
    {Line::Flash, "flash", QT_TRANSLATE_NOOP("QObject", "Flash"), flashInk, flashReach, nullptr, nullptr, {"flash"}},
    {Line::Brush, "brush", QT_TRANSLATE_NOOP("QObject", "Brush"), brushInk, brushReach, nullptr, nullptr, {"brush"}},
    {Line::Drybrush, "drybrush", QT_TRANSLATE_NOOP("QObject", "Drybrush"), drybrushInk, drybrushReach, nullptr, nullptr,
     {"drybrush"}},
    {Line::Dashed, "dashed", QT_TRANSLATE_NOOP("QObject", "Dashed"), dashedInk, dashedReach, nullptr, nullptr, {"dash"}},
    {Line::Multi, "multi", QT_TRANSLATE_NOOP("QObject", "Multi-line"), multiInk, multiReach, nullptr, nullptr, {"multi"}},
    {Line::Sketch, "sketch", QT_TRANSLATE_NOOP("QObject", "Sketch"), sketchInk, sketchReach, nullptr, nullptr, {"sketch"}},
    // The solid outline under a filter of its own: no strokes of its own to draw.
    {Line::DryInk, "dryink", QT_TRANSLATE_NOOP("QObject", "Dry ink"), nullptr, nullptr, dryInkFilter, dryInkUnworn,
     {"dryInk"}},
    // A dry brush in a flash's halo: the halo is the flash's outer strokes, so of its rows the inner ones go.
    {Line::Explosion, "explosion", QT_TRANSLATE_NOOP("QObject", "Explosion"), explosionInk, explosionReach, nullptr,
     nullptr, {"flash", "drybrush"}, "Inner"},
}};

//! Likewise, for patternInfo().
const std::array<PatternInfo, int(Paint::k_lastKind) + 1> k_patterns{{
    {Paint::Kind::Solid, "solid", QT_TRANSLATE_NOOP("QObject", "Solid")},
    {Paint::Kind::Dots,  "dots",  QT_TRANSLATE_NOOP("QObject", "Dots")},
    {Paint::Kind::Lines, "lines", QT_TRANSLATE_NOOP("QObject", "Lines")},
    {Paint::Kind::Grid,  "grid",  QT_TRANSLATE_NOOP("QObject", "Grid")},
}};

/**
 *  The closest a pattern's marks may be, in balloon pixels: below it the pattern is a flat tone, and its
 *  cell would vanish.
 */
constexpr qreal k_minPatternSpacing = 1.0;

//! The smallest mark a fading halftone keeps, in balloon pixels: below it a dot or a bar is not drawn.
constexpr qreal k_minMark = 0.1;

/**
 * \brief How far points lie from a shape's edge, up to a limit: positive inside, negative outside, in
 *        balloon pixels; a point further than the limit is given the limit, with its sign.
 *
 * The shape is flattened once into edges, filed in square buckets as wide as the limit, so the nearest
 * edge within it is in a point's bucket or the eight round it; and in rows as tall, so the crossings that
 * say whether a point is inside (under the shape's own fill rule) are counted along its row only.
 */
class SignedDistance
{
public:
    SignedDistance(const QPainterPath& shape, qreal limit)
        : m_winding(shape.fillRule() == Qt::WindingFill)
        , m_limit(limit)
        , m_cell(qMax(limit, 4.0))
        , m_origin(shape.boundingRect().topLeft())
    {
        const QRectF box = shape.boundingRect();
        m_cols           = qMax(1, qCeil(box.width() / m_cell) + 1);
        m_rows           = qMax(1, qCeil(box.height() / m_cell) + 1);
        m_buckets.resize(size_t(m_cols) * m_rows);
        m_bands.resize(size_t(m_rows));
        for (const QPolygonF& ring : shape.toSubpathPolygons()) {
            for (int i = 0; i + 1 < ring.size(); ++i)
                add(ring[i], ring[i + 1]);
            if (ring.size() > 2 && ring.first() != ring.last())
                add(ring.last(), ring.first());   // a subpath is filled as if closed
        }
    }

    qreal operator()(const QPointF& p) const
    {
        const int col = qFloor((p.x() - m_origin.x()) / m_cell);
        const int row = qFloor((p.y() - m_origin.y()) / m_cell);
        qreal     nearest = m_limit * m_limit;
        for (int r = qMax(row - 1, 0); r <= qMin(row + 1, m_rows - 1); ++r)
            for (int c = qMax(col - 1, 0); c <= qMin(col + 1, m_cols - 1); ++c)
                for (int e : m_buckets[size_t(r) * m_cols + c])
                    nearest = qMin(nearest, squaredDistance(p, m_edges[size_t(e)]));
        int winding = 0;
        if (row >= 0 && row < m_rows)
            for (int e : m_bands[size_t(row)]) {
                const auto& [a, b] = m_edges[size_t(e)];
                if ((a.y() <= p.y()) != (b.y() <= p.y())
                    && a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y()) > p.x())
                    winding += b.y() > a.y() ? 1 : -1;
            }
        const bool inside = m_winding ? winding != 0 : (winding % 2) != 0;
        return inside ? std::sqrt(nearest) : -std::sqrt(nearest);
    }

private:
    using Edge = std::pair<QPointF, QPointF>;

    static qreal squaredDistance(const QPointF& p, const Edge& e)
    {
        const QPointF ab  = e.second - e.first;
        const qreal   len = QPointF::dotProduct(ab, ab);
        const qreal   t   = len > 0.0 ? std::clamp(QPointF::dotProduct(p - e.first, ab) / len, 0.0, 1.0) : 0.0;
        const QPointF off = p - (e.first + t * ab);
        return QPointF::dotProduct(off, off);
    }

    void add(const QPointF& a, const QPointF& b)
    {
        const int e = int(m_edges.size());
        m_edges.push_back({a, b});
        const int c0 = std::clamp(qFloor((qMin(a.x(), b.x()) - m_origin.x()) / m_cell), 0, m_cols - 1);
        const int c1 = std::clamp(qFloor((qMax(a.x(), b.x()) - m_origin.x()) / m_cell), 0, m_cols - 1);
        const int r0 = std::clamp(qFloor((qMin(a.y(), b.y()) - m_origin.y()) / m_cell), 0, m_rows - 1);
        const int r1 = std::clamp(qFloor((qMax(a.y(), b.y()) - m_origin.y()) / m_cell), 0, m_rows - 1);
        for (int r = r0; r <= r1; ++r) {
            m_bands[size_t(r)].push_back(e);
            for (int c = c0; c <= c1; ++c)
                m_buckets[size_t(r) * m_cols + c].push_back(e);
        }
    }

    bool                          m_winding;
    qreal                         m_limit;
    qreal                         m_cell;
    QPointF                       m_origin;
    int                           m_cols = 1;
    int                           m_rows = 1;
    std::vector<Edge>             m_edges;
    std::vector<std::vector<int>> m_buckets;   //!< The edges crossing each square, row by row.
    std::vector<std::vector<int>> m_bands;     //!< The edges crossing each row.
};

/**
 * \brief Rough's filter: turbulence displacing the balloon, then a blur.
 *
 * baseFrequency is in the filter region's *user space*, and the viewBox is in balloon pixels, so the
 * texture scales with the bubble rather than with the output resolution. A chapter re-profiled to twice the
 * width re-renders the same wobble, larger, not twice as much of it.
 */
Render::Filter roughFilter(const TextureProperties& s, quint32 seed)
{
    using Render::number;
    const bool     blurred = s.rough.blur > 0.0;
    Render::Filter f;
    f.primitives.append({QStringLiteral("feTurbulence"),
                         {{QStringLiteral("type"), QStringLiteral("fractalNoise")},
                          {QStringLiteral("baseFrequency"), number(qMax(s.rough.frequency, 0.0), Render::k_valueDecimals)},
                          {QStringLiteral("numOctaves"), QString::number(qMax(s.rough.octaves, 1))},
                          {QStringLiteral("seed"), QString::number(seed % k_noiseSeeds)},
                          {QStringLiteral("result"), QStringLiteral("pmNoise")}},
                         {}});
    Render::Primitive displace{QStringLiteral("feDisplacementMap"),
                               {{QStringLiteral("in"), QStringLiteral("SourceGraphic")},
                                {QStringLiteral("in2"), QStringLiteral("pmNoise")},
                                {QStringLiteral("scale"), number(qMax(s.rough.displacement, 0.0))},
                                {QStringLiteral("xChannelSelector"), QStringLiteral("R")},
                                {QStringLiteral("yChannelSelector"), QStringLiteral("G")}},
                               {}};
    if (blurred)
        displace.attrs.append({QStringLiteral("result"), QStringLiteral("pmBled")});
    f.primitives.append(displace);
    if (blurred)
        f.primitives.append({QStringLiteral("feGaussianBlur"),
                             {{QStringLiteral("in"), QStringLiteral("pmBled")},
                              {QStringLiteral("stdDeviation"), number(s.rough.blur)}},
                             {}});
    return f;
}

//! Likewise, for textureInfo().
const std::array<TextureInfo, int(TextureProperties::k_lastKind) + 1> k_textures{{
    {Texture::None,  "none",  QT_TRANSLATE_NOOP("QObject", "None"),  nullptr, nullptr},
    {Texture::Rough, "rough", QT_TRANSLATE_NOOP("QObject", "Rough"), roughReach, roughFilter, {"rough"}},
}};

} // namespace

const LineInfo& lineInfo(StyleProperties::Line kind)
{
    const int i = int(kind);
    // A kind past the table is a file from a newer build, or a corrupt value: draw it clean, the
    // fallback the readers already use, rather than reading past the end.
    if (i < 0 || i >= int(k_lines.size()))
        return k_lines[int(Line::Clean)];
    Q_ASSERT(k_lines[i].kind == kind);   // the table is out of the enum's order
    return k_lines[i];
}

const TextureInfo& textureInfo(TextureProperties::Kind kind)
{
    const int i = int(kind);
    if (i < 0 || i >= int(k_textures.size()))
        return k_textures[int(Texture::None)];
    Q_ASSERT(k_textures[i].kind == kind);
    return k_textures[i];
}

const PatternInfo& patternInfo(Paint::Kind kind)
{
    const int i = int(kind);
    if (i < 0 || i >= int(k_patterns.size()))
        return k_patterns[int(Paint::Kind::Solid)];
    Q_ASSERT(k_patterns[i].kind == kind);
    return k_patterns[i];
}

PatternCell patternCell(const Paint& p)
{
    PatternCell cell;
    cell.size  = qMax(p.spacing, k_minPatternSpacing);
    cell.angle = p.angle;
    cell.marks.setFillRule(Qt::WindingFill);
    const qreal side = cell.size;
    const qreal mid  = side / 2.0;
    const qreal size = qMax(p.size, 0.0);
    switch (p.kind) {
    case Paint::Kind::Solid:
        break;
    case Paint::Kind::Dots: {
        // A dot no wider than the square stays inside it; a larger one spills into its neighbours, so
        // theirs spill back in, and the dots merge as a real tone's do.
        const qreal radius = size / 2.0;
        const int   reach  = radius <= mid ? 0 : 1;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx)
                cell.marks.addEllipse(QPointF(mid + dx * side, mid + dy * side), radius, radius);
        break;
    }
    case Paint::Kind::Grid:
        cell.marks.addRect(QRectF(mid - qMin(size, side) / 2.0, 0.0, qMin(size, side), side));
        Q_FALLTHROUGH();   // ...and the line across, as Lines have
    case Paint::Kind::Lines:
        cell.marks.addRect(QRectF(0.0, mid - qMin(size, side) / 2.0, side, qMin(size, side)));
        break;
    }
    return cell;
}

QPainterPath halftoneMarks(const Paint& p, const QRectF& area, const std::function<qreal(const QPointF&)>& tone,
                           qreal step)
{
    QPainterPath marks;
    marks.setFillRule(Qt::WindingFill);
    if (p.kind == Paint::Kind::Solid || area.isEmpty())
        return marks;
    const qreal side = qMax(p.spacing, k_minPatternSpacing);
    const qreal size = qMax(p.size, 0.0);
    // The grid of patternCell(), turned the same way, over the area seen from the grid.
    QTransform turn;
    turn.rotate(p.angle);
    const QRectF grid = turn.inverted().mapRect(area.adjusted(-size, -size, size, size));
    const int    i0   = qFloor(grid.left() / side);
    const int    i1   = qCeil(grid.right() / side);
    const int    j0   = qFloor(grid.top() / side);
    const int    j1   = qCeil(grid.bottom() / side);

    if (p.kind == Paint::Kind::Dots) {
        // A dot's area follows the tone, as ink coverage does.
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const QPointF centre = turn.map(QPointF((i + 0.5) * side, (j + 0.5) * side));
                const qreal   radius = size / 2.0 * std::sqrt(std::clamp(tone(centre), 0.0, 1.0));
                if (radius >= k_minMark)
                    marks.addEllipse(centre, radius, radius);
            }
        return marks;
    }

    // A bar along a line of the grid, its width the tone's share of the full one, sampled every `step`.
    // Wound one way whichever its direction (the side it starts on is a quarter turn from its
    // direction), so a grid's crossings paint once.
    const qreal full = qMin(size, side);
    const qreal ds   = qMax(step, 0.25);
    const auto  bar  = [&](const QPointF& from, const QPointF& dir, qreal length) {
        const QPointF  normal(-dir.y(), dir.x());
        QList<QPointF> spine;
        QList<qreal>   half;
        const auto     flush = [&] {
            if (spine.size() >= 2) {
                QPolygonF ring;
                for (int k = 0; k < spine.size(); ++k)
                    ring << turn.map(spine[k] - normal * half[k]);
                for (int k = spine.size() - 1; k >= 0; --k)
                    ring << turn.map(spine[k] + normal * half[k]);
                ring << ring.first();
                marks.addPolygon(ring);
            }
            spine.clear();
            half.clear();
        };
        const int n = qMax(1, qCeil(length / ds));
        for (int k = 0; k <= n; ++k) {
            const QPointF at = from + dir * (length * k / n);
            const qreal   h  = full / 2.0 * std::clamp(tone(turn.map(at)), 0.0, 1.0);
            if (h < k_minMark / 2.0) {
                flush();
                continue;
            }
            // A sample between two of the same width adds nothing: a flat run is one quad.
            if (half.size() >= 2 && std::abs(half.last() - h) < 1e-3 && std::abs(half[half.size() - 2] - h) < 1e-3) {
                spine.last() = at;
                continue;
            }
            spine << at;
            half << h;
        }
        flush();
    };
    const qreal width  = (i1 - i0) * side;
    const qreal height = (j1 - j0) * side;
    for (int j = j0; j < j1; ++j)
        bar(QPointF(i0 * side, (j + 0.5) * side), QPointF(1, 0), width);
    if (p.kind == Paint::Kind::Grid)
        for (int i = i0; i < i1; ++i)
            bar(QPointF((i + 0.5) * side, j0 * side), QPointF(0, 1), height);
    return marks;
}

namespace {

//! One arm of a fade's cross: its two fields, and which way it points.
struct FadeArm
{
    qreal Paint::* from;
    qreal Paint::* to;
    QPointF        axis;
};

//! Left, right, top, bottom: a fade handle's index.
const std::array<FadeArm, 4> k_fadeArms{{
    {&Paint::fadeLeftFrom,   &Paint::fadeLeftTo,   QPointF(-1, 0)},
    {&Paint::fadeRightFrom,  &Paint::fadeRightTo,  QPointF(1, 0)},
    {&Paint::fadeTopFrom,    &Paint::fadeTopTo,    QPointF(0, -1)},
    {&Paint::fadeBottomFrom, &Paint::fadeBottomTo, QPointF(0, 1)},
}};

//! Where the cross stands in @p body.
QPointF fadeCentre(const Paint& p, const QRectF& body)
{
    return body.topLeft() + QPointF(body.width() * p.fadeCentreX, body.height() * p.fadeCentreY) / 100.0;
}

//! How far an arm reaches from the cross's centre, at 100 %: to the side it points at.
qreal armReach(const FadeArm& arm, const QRectF& body, const QPointF& centre)
{
    const QPointF side(arm.axis.x() < 0.0 ? body.left() : body.right(), arm.axis.y() < 0.0 ? body.top() : body.bottom());
    return std::abs(arm.axis.x()) > 0.5 ? std::abs(side.x() - centre.x()) : std::abs(side.y() - centre.y());
}

//! A tenth of a per cent: steady numbers in the file.
qreal tenth(qreal pct)
{
    return qRound(pct * 10.0) / 10.0;
}

/**
 * How much has faded at @p u along an arm (0 at the centre, 1 at the side): none up to `from`, all past
 * `to`, in a line between; none at all on an arm that does not fade.
 */
qreal fadedAlong(qreal u, qreal from, qreal to)
{
    from /= 100.0;
    to /= 100.0;
    if (from >= 1.0 || u <= from)
        return 0.0;
    if (to <= from)
        return 1.0;
    return qMin(1.0, (u - from) / (to - from));
}

/**
 * How far from the centre, along unit direction @p c, a quarter ellipse with half-axes @p a across and
 * @p b down reaches: 0 where the direction has a part along a half-axis of 0, infinite for two infinite.
 */
qreal ellipseReach(qreal a, qreal b, const QPointF& c)
{
    qreal k = 0.0;
    for (const auto& [half, along] : {std::pair{a, c.x()}, std::pair{b, c.y()}}) {
        if (qFuzzyIsNull(along))
            continue;
        if (half <= 0.0)
            return 0.0;
        k += (along / half) * (along / half);
    }
    return k > 0.0 ? 1.0 / std::sqrt(k) : std::numeric_limits<qreal>::infinity();
}

/**
 * How much has faded at @p d from the centre on ellipses: the starts of @p h and @p v (that quarter's arms)
 * the half-axes of one, their ends of the other, and a line between along the ray. An arm whose start is at
 * 100 % does not fade, so both its half-axes are infinite: one arm alone is the same straight fade as
 * without ellipses.
 */
qreal fadedRound(const Paint& p, const FadeArm& h, const FadeArm& v, qreal reachX, qreal reachY, const QPointF& d)
{
    const qreal r = std::hypot(d.x(), d.y());
    if (r <= 0.0)
        return 0.0;
    const auto half = [&p](const FadeArm& arm, qreal Paint::* end, qreal reach) {
        return p.*arm.from >= 100.0 ? std::numeric_limits<qreal>::infinity() : reach * (p.*end) / 100.0;
    };
    const QPointF c    = d / r;
    const qreal   from = ellipseReach(half(h, h.from, reachX), half(v, v.from, reachY), c);
    const qreal   to   = ellipseReach(half(h, h.to, reachX), half(v, v.to, reachY), c);
    if (r <= from)
        return 0.0;
    if (to <= from)
        return 1.0;
    return qMin(1.0, (r - from) / (to - from));
}

} // namespace

bool fades(const Paint& p)
{
    if (p.kind == Paint::Kind::Solid)
        return false;
    for (const FadeArm& arm : k_fadeArms)
        if (p.*arm.from < 100.0)
            return true;
    return false;
}

qreal fadeTone(const Paint& p, const QRectF& body, const QPointF& at)
{
    const QPointF  centre = fadeCentre(p, body);
    const QPointF  d      = at - centre;
    const FadeArm& h      = k_fadeArms[d.x() < 0.0 ? 0 : 1];
    const FadeArm& v      = k_fadeArms[d.y() < 0.0 ? 2 : 3];
    const qreal    reachX = armReach(h, body, centre);
    const qreal    reachY = armReach(v, body, centre);
    qreal          faded  = 0.0;
    if (p.fadeRound) {
        faded = fadedRound(p, h, v, reachX, reachY, d);
    } else {
        const qreal fx = reachX > 0.0 ? fadedAlong(std::abs(d.x()) / reachX, p.*h.from, p.*h.to) : 0.0;
        const qreal fy = reachY > 0.0 ? fadedAlong(std::abs(d.y()) / reachY, p.*v.from, p.*v.to) : 0.0;
        // As a distance: one arm alone is a straight fade, two neighbours round the corner between them.
        faded = qMin(1.0, std::hypot(fx, fy));
    }
    return p.fadeReversed ? faded : 1.0 - faded;
}

QPainterPath fillTone(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const Paint& p = a.skin.fill;
    if (!fades(p) || silhouette.isEmpty())
        return {};
    const QRectF body = balloonRect(a);
    // A bar's width is sampled about every half cell: a fade spans a good part of the balloon.
    return halftoneMarks(p, silhouette.boundingRect(), [&](const QPointF& at) { return fadeTone(p, body, at); },
                         qMax(p.spacing, k_minPatternSpacing) / 2.0);
}

QList<ShapeHandle> fadeHandles(const Paint& p, const QRectF& body)
{
    QList<ShapeHandle> out;
    if (p.kind == Paint::Kind::Solid || body.isEmpty())
        return out;
    const QPointF centre = fadeCentre(p, body);
    // Every start before every end: where the two sit together (a side that does not fade), a press takes
    // the start, and pulling it in is what begins a fade.
    for (const auto role : {ShapeHandle::Role::FadeFrom, ShapeHandle::Role::FadeTo})
        for (int i = 0; i < int(k_fadeArms.size()); ++i) {
            const FadeArm& arm = k_fadeArms[size_t(i)];
            const qreal    pct = p.*(role == ShapeHandle::Role::FadeFrom ? arm.from : arm.to);
            out.append({role, i, centre + arm.axis * armReach(arm, body, centre) * pct / 100.0, arm.axis, centre});
        }
    // The centre last: a start pulled all the way in sits on it, and must still come out again.
    out.append({ShapeHandle::Role::FadeCentre, 0, centre, QPointF(), centre});
    return out;
}

Paint dragFadeHandle(const Paint& p, const QRectF& body, const ShapeHandle& handle, const QPointF& to)
{
    if (body.isEmpty())
        return p;
    Paint next = p;
    if (handle.role == ShapeHandle::Role::FadeCentre) {
        // Anywhere in the rectangle; the arms keep their per cents, so they still reach their sides.
        next.fadeCentreX = tenth(std::clamp((to.x() - body.left()) / body.width() * 100.0, 0.0, 100.0));
        next.fadeCentreY = tenth(std::clamp((to.y() - body.top()) / body.height() * 100.0, 0.0, 100.0));
        return next;
    }
    if (handle.index < 0 || handle.index >= int(k_fadeArms.size()))
        return p;
    const FadeArm& arm    = k_fadeArms[size_t(handle.index)];
    const QPointF  centre = fadeCentre(p, body);
    const qreal    reach  = armReach(arm, body, centre);
    if (reach <= 0.0)
        return p;   // the centre on this side: the arm has no length to drag along
    const qreal at = tenth(std::clamp(QPointF::dotProduct(to - centre, arm.axis) / reach * 100.0, 0.0, 100.0));
    // The start never passes the end: whichever is dragged takes the other along.
    if (handle.role == ShapeHandle::Role::FadeFrom) {
        next.*arm.from = at;
        next.*arm.to   = qMax(next.*arm.to, at);
    } else if (handle.role == ShapeHandle::Role::FadeTo) {
        next.*arm.to   = at;
        next.*arm.from = qMin(next.*arm.from, at);
    }
    return next;
}

TextureProperties inkRough(TextureProperties t)
{
    t.kind              = Texture::Rough;
    t.rough.frequency    = k_inkFrequency;
    t.rough.octaves      = k_inkOctaves;
    t.rough.displacement = k_inkDisplacement;
    t.rough.blur         = k_inkBlur;
    return t;
}

bool blursShadow(const Shadow& s, const Paint& fill)
{
    return s.on && s.softness > 0.0 && fill.kind == Paint::Kind::Solid;
}

bool fadesShadow(const Shadow& s, const Paint& fill)
{
    return s.on && s.softness > 0.0 && fill.kind != Paint::Kind::Solid;
}

bool hasFilter(const ObjectRecord& a)
{
    return textureInfo(a.texture.kind).filter != nullptr || lineInfo(a.style.line).outlineFilter != nullptr || blursShadow(a)
        || (!a.text.body.isEmpty() && blursShadow(a.text.shadow, a.text.shadowFill));
}

Paint shadowPaint(const Shadow& s, const Paint& fill)
{
    Paint p = fill;
    if (fadesShadow(s, fill)) {
        p.kind = Paint::Kind::Solid;   // the path is its marks already
    } else if (p.kind != Paint::Kind::Solid) {
        p.second = p.colour;           // the marks in the shadow's colour, on nothing
        p.colour = Qt::transparent;
    }
    return p;
}

ShadowPaths shadowOf(const Shadow& s, const Paint& fill, const QPainterPath& caster)
{
    if (!s.on || caster.isEmpty())
        return {};
    const QPainterPath moved = caster.translated(s.x, s.y);
    const qreal        grow  = qMax(s.spread, 0.0);
    if (!fadesShadow(s, fill))
        return {moved, grow};
    // A halftone fades as a blur does: a mark's tone is the blurred edge's at its centre, half at the
    // edge itself, and gone three deviations out. The edge is the grown one's: the signed distance from the
    // moved caster (positive inside) plus the spread, which a round growth is exactly.
    const qreal          deviation = s.softness;
    const qreal          reach     = k_blurReach * deviation;
    const SignedDistance distance(moved, reach + grow);   // past it the tone is whole, or nothing
    const qreal          out       = reach + grow;
    return {halftoneMarks(fill, moved.boundingRect().adjusted(-out, -out, out, out),
                          [&](const QPointF& at) {
                              return 0.5 * std::erfc(-(distance(at) + grow) / (deviation * M_SQRT2));
                          },
                          deviation / 2.0),
            0.0};
}

Paint shadowLayerPaint(const Shadow& s, const Paint& fill, qreal* opacity)
{
    Paint   p      = shadowPaint(s, fill);
    QColor& colour = p.kind == Paint::Kind::Solid ? p.colour : p.second;   // a pattern's marks are its colour
    if (opacity)
        *opacity = colour.alphaF();
    colour.setAlpha(255);
    return p;
}

qreal shadowReach(const Shadow& s)
{
    // As far as it grows and is moved and its blur spreads, on every side, which is generous but simple.
    return s.on ? qMax(s.spread, 0.0) + qMax(std::abs(s.x), std::abs(s.y)) + k_blurReach * qMax(s.softness, 0.0)
                : 0.0;
}

bool blursShadow(const ObjectRecord& a)
{
    return blursShadow(a.effects.shadow, a.effects.shadowFill);
}

bool fadesShadow(const ObjectRecord& a)
{
    return fadesShadow(a.effects.shadow, a.effects.shadowFill);
}

ShadowPaths shadowOf(const ObjectRecord& a, const QPainterPath& silhouette)
{
    return shadowOf(a.effects.shadow, a.effects.shadowFill, silhouette);
}

Paint shadowPaint(const ObjectRecord& a)
{
    return shadowPaint(a.effects.shadow, a.effects.shadowFill);
}

bool hasOutline(const ObjectRecord& a)
{
    return lineInfo(a.style.line).ink == nullptr;
}

QPainterPath ink(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const auto fn = lineInfo(a.style.line).ink;
    return fn && !silhouette.isEmpty() ? fn(a.style, outlineModel(a, silhouette), a.styleSeed) : QPainterPath();
}

qreal styleMargin(const ObjectRecord& a)
{
    // The texture's filter moves the line's strokes too (it hangs on both), so the two add; the shadow's
    // too, which reaches as shadowReach() says.
    const LineInfo&    line    = lineInfo(a.style.line);
    const TextureInfo& texture = textureInfo(a.texture.kind);
    const qreal        shadow  = shadowReach(a.effects.shadow);
    const qreal        rough   = texture.reach ? texture.reach(a.texture) : 0.0;
    return qMax((line.inkReach ? line.inkReach(a.style) : 0.0) + rough, shadow + rough);
}

}  // namespace Geometry

// The line's sections, as the Line panel shows them (propertygroups.hpp).

const Fields::Section& FlashParams::section()
{
    static const Fields::Section s = Fields::sectionOf("flash", QT_TRANSLATE_NOOP("QObject", "Flash"), Geometry::k_flashRows);
    return s;
}

const Fields::Section& BrushParams::section()
{
    static const Fields::Section s = Fields::sectionOf("brush", QT_TRANSLATE_NOOP("QObject", "Brush"), Geometry::k_brushRows);
    return s;
}

const Fields::Section& DrybrushParams::section()
{
    static const Fields::Section s = Fields::sectionOf("drybrush", QT_TRANSLATE_NOOP("QObject", "Drybrush"), Geometry::k_drybrushRows);
    return s;
}

const Fields::Section& DashParams::section()
{
    static const Fields::Section s = Fields::sectionOf("dash", QT_TRANSLATE_NOOP("QObject", "Dashed"), Geometry::k_dashRows);
    return s;
}

const Fields::Section& MultiParams::section()
{
    static const Fields::Section s = Fields::sectionOf("multi", QT_TRANSLATE_NOOP("QObject", "Multi-line"), Geometry::k_multiRows);
    return s;
}

const Fields::Section& SketchParams::section()
{
    static const Fields::Section s = Fields::sectionOf("sketch", QT_TRANSLATE_NOOP("QObject", "Sketch"), Geometry::k_sketchRows);
    return s;
}

const Fields::Section& DryInkParams::section()
{
    static const Fields::Section s = Fields::sectionOf("dryInk", QT_TRANSLATE_NOOP("QObject", "Dry ink"), Geometry::k_dryInkRows);
    return s;
}

const Fields::Section& RoughParams::section()
{
    static const Fields::Section s = Fields::sectionOf("rough", QT_TRANSLATE_NOOP("QObject", "Rough"), Geometry::k_roughRows);
    return s;
}
