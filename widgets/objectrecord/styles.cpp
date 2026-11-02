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
 * @brief Adds one ring of strokes along every closed edge in @p edges.
 *
 * Strokes are spread evenly by arc length, not by angle, which would crowd them at the ends of a wide
 * oval: the edge's perimeter is divided into whole steps near the pitch, so there is no seam where the
 * walk meets its start. Each stroke points along the ray from the nearest point of @p core through its
 * place on the edge (A14): away from it outside, towards it inside, where it is cut short at the core.
 */
void addRing(QPainterPath& out, const QList<EdgeWalk>& edges, const QRectF& core, const Ring& r,
             QRandomGenerator& rng)
{
    const qreal pitch = qMax(r.pitch, k_minPitch);
    const qreal half  = qMax(r.base, 0.0) / 2.0;

    for (const EdgeWalk& edge : edges) {
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

QPainterPath flashInk(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const StyleProperties& s      = a.style;
    const QList<EdgeWalk>  edges  = edgeWalks(silhouette);
    const QPointF          centre = QRectF(0, 0, a.box.width(), a.box.height()).center();

    // What the strokes point away from: the centre, stretched into a line along the box's longer axis.
    // At 100 % the line runs between the centres of the two circles that fit the box's ends, so every
    // stroke on a long straight side is square to it and only the ends fan out; a square box keeps a
    // point whatever the stretch. On Auto the shape says how far.
    const int    percent = s.flashCoreStretch == StyleProperties::k_autoStretch
                               ? shapeInfo(a.shape.kind).flashStretch
                               : s.flashCoreStretch;
    const qreal  stretch = qBound(0, percent, 100) / 100.0;
    const QSizeF half(stretch * qMax(0, a.box.width() - a.box.height()) / 2.0,
                      stretch * qMax(0, a.box.height() - a.box.width()) / 2.0);
    const QRectF core(centre.x() - half.width(), centre.y() - half.height(), 2 * half.width(),
                      2 * half.height());

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    // The seed is the balloon's own (A5), so its strokes stay put while it is edited.
    QRandomGenerator outer(a.styleSeed);
    QRandomGenerator inner(a.styleSeed ^ k_innerStream);
    addRing(out, edges, core,
            {s.flashOuterPitch, s.flashOuterLength, s.flashOuterBase,
             s.flashOuterPitchJitter, s.flashOuterLengthJitter, false},
            outer);
    addRing(out, edges, core,
            {s.flashInnerPitch, s.flashInnerLength, s.flashInnerBase,
             s.flashInnerPitchJitter, s.flashInnerLengthJitter, true},
            inner);
    return out;
}

//! The longest outer stroke, plus half the widest base: a base turned across a corner reaches out too.
qreal flashReach(const ObjectRecord& a)
{
    const StyleProperties& s     = a.style;
    const qreal            jit   = qBound(0, s.flashOuterLengthJitter, 100) / 100.0;
    const qreal            reach = qMax(s.flashOuterLength, 0.0) * (1.0 + jit);
    return reach + qMax(qMax(s.flashOuterBase, s.flashInnerBase), 0.0) / 2.0;
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

/**
 * @brief The brush's strokes along every closed edge of @p silhouette.
 *
 * Each edge is shared out among `brushStrokes` strokes, their joins scattered off the even share and
 * each running `brushOverlap` past the next one's start, so they overlap and do not quite close. A stroke
 * is a ribbon about the edge, as wide as the outline, narrowing to a point over `brushTaper` of its
 * length at each end, its width varied by `brushSwell` and its middle moved off the edge by
 * `brushDrift`, both along smooth noise. Every ribbon winds the same way, so overlaps paint as one.
 */
QPainterPath brushInk(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const StyleProperties& s     = a.style;
    const qreal            half  = qMax(a.style.strokeWidth, 0) / 2.0;
    const int              count = qMax(1, s.brushStrokes);
    const qreal            over  = qBound(0, s.brushOverlap, 100) / 100.0;
    const qreal            taper = qBound(0, s.brushTaper, 50) / 100.0;
    const qreal            swell = qBound(0, s.brushSwell, 100) / 100.0;

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (half <= 0.0)
        return out;   // no outline, no brush: its width is the outline's
    QRandomGenerator rng(a.styleSeed ^ k_brushStream);
    quint32          stroke = 0;   // a number per stroke, for its own noise
    for (const EdgeWalk& edge : edgeWalks(silhouette)) {
        const qreal length = edge.length();
        const qreal share  = length / count;
        std::vector<qreal> join(size_t(count) + 1);
        for (int j = 0; j < count; ++j)
            join[size_t(j)] = (j + k_brushScatter * (2.0 * rng.generateDouble() - 1.0)) * share;
        join[size_t(count)] = join[0] + length;   // the last stroke ends where the first begins, round

        for (int j = 0; j < count; ++j, ++stroke) {
            const qreal   from    = join[size_t(j)] - over * share / 2.0;
            const qreal   to      = join[size_t(j) + 1] + over * share / 2.0;
            const int     samples = qMax(2, qCeil((to - from) / k_brushStep));
            const quint32 seed    = a.styleSeed ^ ((stroke + 1) * k_strokeStream);
            QPolygonF     left, right;
            for (int k = 0; k <= samples; ++k) {
                const qreal  u  = qreal(k) / samples;
                const qreal  at = from + (to - from) * u;
                const auto   p  = edge.at(at - std::floor(at / length) * length);
                const QPointF n(-p.tangent.y(), p.tangent.x());
                const qreal  w  = half * taperAt(u, taper) * (1.0 + swell * smoothNoise(seed, at / k_brushWave));
                const qreal  c  = s.brushDrift * smoothNoise(seed ^ k_brushStream, at / k_brushWave);
                left << p.at + n * (c + w);
                right.prepend(p.at + n * (c - w));
            }
            out.addPolygon(left + right);
            out.closeSubpath();
        }
    }
    return out;
}

//! Half the outline at its widest swell, and as far as a stroke wanders.
qreal brushReach(const ObjectRecord& a)
{
    const StyleProperties& s = a.style;
    return qMax(a.style.strokeWidth, 0) / 2.0 * (1.0 + qBound(0, s.brushSwell, 100) / 100.0)
         + std::abs(s.brushDrift);
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
//! The turn at a vertex, in degrees, from which it is a corner the hairs do not follow round.
constexpr qreal   k_cornerTurn    = 35.0;
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
//! How far either side of a vertex its turn is measured, in balloon pixels: a flattened curve's own
//! small steps are not corners, one corner split by a sliver of an edge still is.
constexpr qreal   k_cornerProbe   = 1.5;
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
 * @brief Smooth noise round a closed edge @p length long, about @p wave per span: a whole number of
 *        spans to the lap, repeating, so it closes on itself where the walk does.
 */
qreal loopedNoise(quint32 seed, qreal at, qreal length, qreal wave)
{
    const qint64 spans = qMax<qint64>(1, qRound64(length / wave));
    return smoothNoise(seed, at / length * spans, spans);
}

//! Twice @p ring's signed area (the shoelace): a ribbon, left side forward and right side back, is negative.
qreal shoelace(const QPolygonF& ring)
{
    qreal area = 0.0;
    for (int i = 0; i < ring.size(); ++i) {
        const QPointF& p = ring[i];
        const QPointF& q = ring[(i + 1) % ring.size()];
        area += p.x() * q.y() - q.x() * p.y();
    }
    return area;
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

//! A vertex the hairs do not follow round: where along the edge, and how far it turns (radians, signed).
struct EdgeCorner
{
    qreal at;
    qreal turn;
};

//! The corners of @p edge, one per turn: of vertices closer than the probe, the sharpest.
std::vector<EdgeCorner> cornersOf(const EdgeWalk& edge)
{
    const qreal length = edge.length();
    const auto  wrap   = [length](qreal at) { return at - std::floor(at / length) * length; };
    std::vector<EdgeCorner> out;
    const std::vector<qreal>& at = edge.corners();
    for (size_t i = 0; i + 1 < at.size(); ++i) {   // the last is the first again
        const QPointF t1   = edge.at(wrap(at[i] - k_cornerProbe)).tangent;
        const QPointF t2   = edge.at(wrap(at[i] + k_cornerProbe)).tangent;
        const qreal   turn = std::atan2(t1.x() * t2.y() - t1.y() * t2.x(), QPointF::dotProduct(t1, t2));
        if (std::abs(turn) < qDegreesToRadians(k_cornerTurn))
            continue;
        if (!out.empty() && at[i] - out.back().at < 2.0 * k_cornerProbe) {
            if (std::abs(turn) > std::abs(out.back().turn))
                out.back() = {at[i], turn};
            continue;
        }
        out.push_back({at[i], turn});
    }
    if (out.size() > 1 && out.front().at + length - out.back().at < 2.0 * k_cornerProbe) {
        if (std::abs(out.back().turn) > std::abs(out.front().turn))
            out.front() = out.back();
        out.pop_back();
    }
    return out;
}

/**
 * @brief The dry brush round every closed edge of @p silhouette.
 *
 * A **core**, `drybrushCore` of the line's width, is the plain outline, mitred so a point stays a
 * point: always whole, so the fill is never open to what is round it. Either side of it the rest of the
 * width is shared into lanes, `drybrushBristles` in all, one **hair** each: a thin ribbon along the
 * edge, wandering in its lane, there where its noise is under a dry level and narrowing away over it.
 * The level rises from `drybrushDryness` next to the core most of the way to bone dry at the line's
 * edge (`k_bristleThinning`), so the hairs thin out away from it.
 *
 * The core follows the joined silhouette; the hairs follow each **part** of it on its own edge
 * (silhouetteParts(): the balloon, each tail). Where a part's edge goes under another part, its hairs run
 * on along it from where it was in sight, until their noise ends them, `k_partRunOn` half-widths at most:
 * the balloon's on into a tail's root, the tail's back into the balloon, crossing into a natural hatch.
 * So a tail's root is no corner at all.
 *
 * The hairs do not follow a **corner** of a part round (an offset turned round one folds over itself).
 * Between corners they run edge to edge; at a convex corner each runs on straight past where the other
 * side's hairs lie, so the two sides cross in a hatch, narrowing away over that run. At a **concave** one
 * (between a shout's spikes) each runs on straight until it meets the other side's core (its line carried
 * on past the corner), narrowing away just before it (`k_rootTaper`). At a **point** (a convex corner
 * under `k_pointAngle`, a tail's tip) they run into it instead, closing on the core so it ends sharp.
 *
 * Built only of rings wound one way (the core's turned to match), so the winding rule paints their
 * overlaps as one and nothing is subtracted: a boolean on paths this dense costs a frame.
 */
QPainterPath drybrushInk(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const StyleProperties& s       = a.style;
    const qreal            half    = qMax(s.strokeWidth, 0) / 2.0;
    const qreal            core    = half * qBound(0, s.drybrushCore, 100) / 100.0;
    const int              perSide = qMax(1, s.drybrushBristles / 2);
    const qreal            lane    = (half - core) / perSide;
    const qreal            dryness = qBound(0, s.drybrushDryness, 100) / 100.0;
    const qreal            rootTaper = k_rootTaper * qMax(lane, k_brushStep / 2.0);   // a lane may be none

    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (half <= 0.0)
        return out;   // no outline, no brush: its width is the outline's
    QPainterPathStroker pen;
    pen.setWidth(2.0 * core);
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setMiterLimit(k_coreMiter);

    // The core, along the joined silhouette, wound as the hairs are.
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

    // The hairs, along each part's own edge.
    const QList<QPainterPath> parts      = silhouetteParts(a);
    const qreal               underReach = k_partRunOn * half;
    quint32                   stream     = 0;   // a number per lane, for its own noise
    for (qsizetype part = 0; part < parts.size(); ++part)
    for (const QPolygonF& ring : parts[part].toSubpathPolygons()) {
        const EdgeWalk edge(ring);
        const qreal    length = edge.length();
        if (length <= 0.0)
            continue;
        const auto wrap = [length](qreal at) { return at - std::floor(at / length) * length; };

        // Where the edge is under another part, every step along it.
        const int         marks = qMax(1, qCeil(length / k_brushStep));
        std::vector<char> under(size_t(marks), 0);
        for (int k = 0; k < marks; ++k) {
            const QPointF p = edge.at(length * k / marks).at;
            for (qsizetype other = 0; other < parts.size() && !under[size_t(k)]; ++other)
                under[size_t(k)] = other != part && parts[other].contains(p);
        }
        const auto hidden = [&](qreal at) { return under[size_t(qRound(wrap(at) / length * marks) % marks)] != 0; };

        // The stretches between corners; with none, one round the whole loop.
        const std::vector<EdgeCorner> corners   = cornersOf(edge);
        const bool                    convexWay = shoelace(ring) > 0.0;   // a turn this way is convex
        struct Stretch
        {
            qreal             from, to;
            const EdgeCorner* start;
            const EdgeCorner* end;
        };
        std::vector<Stretch> stretches;
        if (corners.empty())
            stretches.push_back({0.0, length, nullptr, nullptr});
        for (size_t i = 0; i < corners.size(); ++i) {
            const EdgeCorner& c    = corners[i];
            const EdgeCorner& next = corners[(i + 1) % corners.size()];
            stretches.push_back({c.at, next.at + (next.at > c.at ? 0.0 : length), &c, &next});
        }
        const auto concave = [&](const EdgeCorner* c) { return c && (c->turn > 0.0) != convexWay; };
        // How far from a point its hairs start closing on it: where the line's two edges meet.
        const auto closing = [&](const EdgeCorner* c) {
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
            const quint32 seed   = a.styleSeed ^ ((stream + 1) * k_bristleStream);
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
                const auto rootOf = [&](const EdgeCorner* c, qreal mine) {   // mine: -1 this side before it
                    Root r;
                    if (!concave(c))
                        return r;
                    r.at                = edge.at(wrap(c->at)).at;
                    const QPointF other = edge.at(wrap(c->at - mine * k_cornerProbe)).tangent;
                    const QPointF own   = edge.at(wrap(c->at + mine * k_cornerProbe)).tangent;
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
                    steps.push_back({mid, n, width(at) * into * stop(rootStart, mid) * stop(rootEnd, mid), at,
                                     hidden(at)});
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
                const auto runOn = [&](const Step& from, qreal dir, const EdgeCorner* c, qreal close,
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
 * @brief The line's half-width three times over, a hair run on across a corner reaching past it by its
 *        lane and as far again; or the core's miter at a point, if a thick core reaches further.
 */
qreal drybrushReach(const ObjectRecord& a)
{
    const qreal core = qBound(0, a.style.drybrushCore, 100) / 100.0;
    return qMax(3.0, 2.0 * k_coreMiter * core) * qMax(a.style.strokeWidth, 0) / 2.0;
}

// ---------------------------------------------------------------------------
// Rough: turbulence displacing the outline, then a blur (the filter is in recordsvg.cpp)
// ---------------------------------------------------------------------------

//! The displacement, and the blur's spread past it.
qreal roughReach(const TextureProperties& t)
{
    return qMax(t.roughDisplacement, 0.0) + k_blurReach * qMax(t.roughBlur, 0.0);
}

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

using Line    = StyleProperties::Line;
using Texture = TextureProperties::Kind;

//! In the enum's order, so a kind is its own index; lineInfo() checks every entry names its kind.
const std::array<LineInfo, int(StyleProperties::k_lastLine) + 1> k_lines{{
    {Line::Clean, "clean", QT_TRANSLATE_NOOP("QObject", "Clean"), nullptr,  nullptr},
    {Line::Flash, "flash", QT_TRANSLATE_NOOP("QObject", "Flash"), flashInk, flashReach},
    {Line::Brush, "brush", QT_TRANSLATE_NOOP("QObject", "Brush"), brushInk, brushReach},
    {Line::Drybrush, "drybrush", QT_TRANSLATE_NOOP("QObject", "Drybrush"), drybrushInk, drybrushReach},
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
 * @brief How far points lie from a shape's edge, up to a limit: positive inside, negative outside, in
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

//! Likewise, for textureInfo().
const std::array<TextureInfo, int(TextureProperties::k_lastKind) + 1> k_textures{{
    {Texture::None,  "none",  QT_TRANSLATE_NOOP("QObject", "None"),  nullptr},
    {Texture::Rough, "rough", QT_TRANSLATE_NOOP("QObject", "Rough"), roughReach},
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
    t.roughFrequency    = k_inkFrequency;
    t.roughOctaves      = k_inkOctaves;
    t.roughDisplacement = k_inkDisplacement;
    t.roughBlur         = k_inkBlur;
    return t;
}

bool blursShadow(const ObjectRecord& a)
{
    return a.effects.shadow && a.effects.shadowSoftness > 0.0 && a.effects.shadowFill.kind == Paint::Kind::Solid;
}

bool fadesShadow(const ObjectRecord& a)
{
    return a.effects.shadow && a.effects.shadowSoftness > 0.0 && a.effects.shadowFill.kind != Paint::Kind::Solid;
}

bool hasFilter(const ObjectRecord& a)
{
    return textureInfo(a.texture.kind).reach != nullptr || blursShadow(a);
}

Paint shadowPaint(const ObjectRecord& a)
{
    Paint p = a.effects.shadowFill;
    if (fadesShadow(a)) {
        p.kind = Paint::Kind::Solid;   // the path is its marks already
    } else if (p.kind != Paint::Kind::Solid) {
        p.second = p.colour;           // the marks in the shadow's colour, on nothing
        p.colour = Qt::transparent;
    }
    return p;
}

QPainterPath shadowOf(const ObjectRecord& a, const QPainterPath& silhouette)
{
    if (!a.effects.shadow || silhouette.isEmpty())
        return {};
    QPainterPath grown = silhouette;
    if (a.effects.shadowSpread > 0.0) {
        // Grown on every side: united with a round stroke as wide as the spread each way.
        QPainterPathStroker rim;
        rim.setWidth(2.0 * a.effects.shadowSpread);
        rim.setJoinStyle(Qt::RoundJoin);
        rim.setCapStyle(Qt::RoundCap);
        grown = silhouette.united(rim.createStroke(silhouette));
    }
    grown.translate(a.effects.shadowX, a.effects.shadowY);
    if (!fadesShadow(a))
        return grown;
    // A halftone fades as a blur does: a mark's tone is the blurred edge's at its centre, half at the
    // edge itself, and gone three deviations out.
    const qreal          deviation = a.effects.shadowSoftness;
    const qreal          reach     = k_blurReach * deviation;
    const SignedDistance distance(grown, reach);   // past it the tone is whole, or nothing
    return halftoneMarks(a.effects.shadowFill, grown.boundingRect().adjusted(-reach, -reach, reach, reach),
                         [&](const QPointF& at) { return 0.5 * std::erfc(-distance(at) / (deviation * M_SQRT2)); },
                         deviation / 2.0);
}

bool hasOutline(const ObjectRecord& a)
{
    return lineInfo(a.style.line).ink == nullptr;
}

QPainterPath ink(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const auto fn = lineInfo(a.style.line).ink;
    return fn && !silhouette.isEmpty() ? fn(a, silhouette) : QPainterPath();
}

qreal styleMargin(const ObjectRecord& a)
{
    // The texture's filter moves the line's strokes too (it hangs on both), so the two add. A shadow
    // reaches as far as it grows and is moved and its blur spreads, on every side, which is generous but
    // simple.
    const LineInfo&         line    = lineInfo(a.style.line);
    const TextureInfo&      texture = textureInfo(a.texture.kind);
    const EffectProperties& e       = a.effects;
    const qreal shadow = e.shadow ? qMax(e.shadowSpread, 0.0) + qMax(std::abs(e.shadowX), std::abs(e.shadowY))
                                        + k_blurReach * qMax(e.shadowSoftness, 0.0)
                                  : 0.0;
    const qreal rough = texture.reach ? texture.reach(a.texture) : 0.0;
    return qMax((line.inkReach ? line.inkReach(a) : 0.0) + rough, shadow + rough);
}

}  // namespace Geometry
