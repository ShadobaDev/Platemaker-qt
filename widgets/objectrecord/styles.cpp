// The lines and the textures: one table entry each, the flash's strokes, and the room each needs past the
// geometry.

#include "styles.hpp"
#include "edgewalk.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

#include <QObject>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
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
    const qreal            half  = qMax(a.skin.strokeWidth, 0) / 2.0;
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
    return qMax(a.skin.strokeWidth, 0) / 2.0 * (1.0 + qBound(0, s.brushSwell, 100) / 100.0)
         + std::abs(s.brushDrift);
}

// ---------------------------------------------------------------------------
// Rough: turbulence displacing the outline, then a blur (the filter is in recordsvg.cpp)
// ---------------------------------------------------------------------------

//! The displacement, and the blur's spread past it.
qreal roughReach(const StyleProperties& s)
{
    return qMax(s.roughDisplacement, 0.0) + k_blurReach * qMax(s.roughBlur, 0.0);
}

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

using Line    = StyleProperties::Line;
using Texture = StyleProperties::Texture;

//! In the enum's order, so a kind is its own index; lineInfo() checks every entry names its kind.
const std::array<LineInfo, int(StyleProperties::k_lastLine) + 1> k_lines{{
    {Line::Clean, "clean", QT_TRANSLATE_NOOP("QObject", "Clean"), nullptr,  nullptr},
    {Line::Flash, "flash", QT_TRANSLATE_NOOP("QObject", "Flash"), flashInk, flashReach},
    {Line::Brush, "brush", QT_TRANSLATE_NOOP("QObject", "Brush"), brushInk, brushReach},
}};

//! Likewise, for textureInfo().
const std::array<TextureInfo, int(StyleProperties::k_lastTexture) + 1> k_textures{{
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

const TextureInfo& textureInfo(StyleProperties::Texture kind)
{
    const int i = int(kind);
    if (i < 0 || i >= int(k_textures.size()))
        return k_textures[int(Texture::None)];
    Q_ASSERT(k_textures[i].kind == kind);
    return k_textures[i];
}

StyleProperties inkRough(StyleProperties s)
{
    s.texture           = Texture::Rough;
    s.roughFrequency    = k_inkFrequency;
    s.roughOctaves      = k_inkOctaves;
    s.roughDisplacement = k_inkDisplacement;
    s.roughBlur         = k_inkBlur;
    return s;
}

bool hasFilter(const ObjectRecord& a)
{
    return textureInfo(a.style.texture).reach != nullptr;
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
    // The texture's filter moves the line's strokes too (it hangs on both), so the two add.
    const LineInfo&    line    = lineInfo(a.style.line);
    const TextureInfo& texture = textureInfo(a.style.texture);
    return (line.inkReach ? line.inkReach(a) : 0.0) + (texture.reach ? texture.reach(a.style) : 0.0);
}

}  // namespace Geometry
