// The lines and the textures: one table entry each, the flash's strokes, and the room each needs past the
// geometry.

#include "styles.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

#include <QLineF>
#include <QObject>
#include <QPolygonF>
#include <QRandomGenerator>

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
void addRing(QPainterPath& out, const QList<QPolygonF>& edges, const QRectF& core, const Ring& r,
             QRandomGenerator& rng)
{
    const qreal pitch = qMax(r.pitch, k_minPitch);
    const qreal half  = qMax(r.base, 0.0) / 2.0;

    for (QPolygonF edge : edges) {
        if (edge.size() < 2)
            continue;
        if (edge.first() != edge.last())
            edge << edge.first();

        // How far along the edge each vertex is.
        std::vector<qreal> at(size_t(edge.size()), 0.0);
        for (int i = 1; i < edge.size(); ++i)
            at[size_t(i)] = at[size_t(i - 1)] + QLineF(edge[i - 1], edge[i]).length();
        const qreal perimeter = at.back();
        if (perimeter <= 0.0)
            continue;

        const int   count = qMax(1, qRound(perimeter / pitch));
        const qreal step  = perimeter / count;
        for (int i = 0; i < count; ++i) {
            // Both numbers are drawn before anything can skip the stroke, so one skipped stroke does
            // not shift every stroke after it.
            const qreal shift  = 0.5 * (spread(rng, r.pitchJitter) - 1.0);   // ± half a step at 100 %
            qreal       length = r.length * spread(rng, r.lengthJitter);

            // Where along the edge, and on which segment of it.
            const qreal  s    = std::fmod((i + shift) * step + perimeter, perimeter);
            const auto   next = std::upper_bound(at.begin(), at.end(), s);
            const size_t k    = size_t(std::clamp<std::ptrdiff_t>(next - at.begin(), 1,
                                                                  std::ptrdiff_t(at.size()) - 1)) - 1;
            const qreal  seg  = at[k + 1] - at[k];
            const qreal  t    = seg > 0.0 ? (s - at[k]) / seg : 0.0;
            const QPointF p   = edge[int(k)] + (edge[int(k) + 1] - edge[int(k)]) * t;

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
    const QList<QPolygonF> edges  = silhouette.toSubpathPolygons();
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
// The table
// ---------------------------------------------------------------------------

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
