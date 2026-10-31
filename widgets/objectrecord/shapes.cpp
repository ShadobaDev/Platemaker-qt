#include "shapes.hpp"

#include <QByteArray>
#include <QObject>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Geometry {

namespace {

// ---------------------------------------------------------------------------
// The shapes' own numbers, beside the shapes that use them
// ---------------------------------------------------------------------------

/**
 *  Points on a "shout" burst — 12 spikes reads as a shout without turning into a sunburst.
 */
constexpr int   k_burstSpikes    = 12;
/**
 *  Inner radius of the burst, as a fraction of the outer one.
 */
constexpr qreal k_burstInner     = 0.74;
/**
 *  1/sqrt(2) — the half-axis fraction at which a rectangle inscribes an ellipse.
 */
constexpr qreal k_invSqrt2       = 0.70710678118654752;
/**
 *  Below this tan(θ/2) a polygon's corner folds back on itself, and no arc fits in it.
 */
constexpr qreal k_hairpinCorner  = 1e-6;
/**
 *  Fraction of the width the trapezoid's top edge loses on each side.
 */
constexpr qreal k_trapezoidSlant = 0.16;
/**
 *  Lobes around a thought balloon. Odd, so no two sit exactly opposite and it reads as drawn, not
 *  generated.
 */
constexpr int   k_thoughtLobes   = 11;
/**
 *  Lobe radius, as a fraction of the balloon's shorter half-axis.
 */
constexpr qreal k_thoughtLobe    = 0.40;
/**
 *  Depth of a banner's end notch, as a fraction of its width.
 */
constexpr qreal k_bannerNotch    = 0.12;
/**
 *  Width of a scroll's rolled end, as a fraction of the balloon's width.
 */
constexpr qreal k_scrollRoll     = 0.10;
/**
 *  How far a scroll's long edges bow inward, as a fraction of its height.
 */
constexpr qreal k_scrollBow      = 0.06;
/**
 *  Samples across each side of the grid a free outline's text area is found on (largestInscribedRect()).
 *  64 puts the rectangle within a sixty-fourth of the side of the true largest, well inside the padding.
 */
constexpr int   k_areaSamples    = 64;
/**
 *  Halvings that push each side of that rectangle back out to the outline: a cell to a thousandth.
 */
constexpr int   k_growSteps      = 10;
/**
 *  A caption box's corner radius, in balloon pixels. The speech balloon's is the struct's default
 *  (ShapeProperties::cornerRadius): 0.28 of the default 160 px height, as it was before it was a number.
 */
constexpr qreal k_captionCorner  = 4.0;

//! The largest axis-aligned rectangle inside the ellipse inscribed in \p r.
QRectF inscribedInEllipse(const ShapeProperties&, const QRectF& r)
{
    const qreal kx = r.width()  * (1.0 - k_invSqrt2) / 2.0;
    const qreal ky = r.height() * (1.0 - k_invSqrt2) / 2.0;
    return r.adjusted(kx, ky, -kx, -ky);
}

//! The whole rectangle: the lettering may use all of it.
QRectF wholeArea(const ShapeProperties&, const QRectF& body)
{
    return body;
}

// ---------------------------------------------------------------------------
// Box, Ellipse
// ---------------------------------------------------------------------------

//! The corner radius a Box in @p r actually gets: never negative, never past half the shorter side.
qreal boxRadius(const ShapeProperties& shape, const QRectF& r)
{
    return qBound(0.0, shape.cornerRadius, cornerRadiusLimit(r.size()));
}

QPainterPath boxPath(const ShapeProperties& shape, const QRectF& r)
{
    const qreal radius = boxRadius(shape, r);
    QPainterPath p;
    p.addRoundedRect(r, radius, radius);
    return p;
}

QRectF boxArea(const ShapeProperties& shape, const QRectF& body)
{
    // In from every side by where a corner's arc crosses its diagonal: the rectangle's corners then sit
    // on the arcs, so no letter in a corner pokes past the outline.
    const qreal in = boxRadius(shape, body) * (1.0 - k_invSqrt2);
    return body.adjusted(in, in, -in, -in);
}

QPainterPath ellipsePath(const ShapeProperties&, const QRectF& r)
{
    QPainterPath p;
    p.addEllipse(r);
    return p;
}

// ---------------------------------------------------------------------------
// Polygon
// ---------------------------------------------------------------------------

//! A Polygon's vertices in @p r: its nodes, or the rectangle's corners when it has fewer than three.
QList<QPointF> polygonPoints(const ShapeProperties& shape, const QRectF& r)
{
    static const QList<QPointF> k_rectangle{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const QList<QPointF>& nodes = shape.nodes.size() >= 3 ? shape.nodes : k_rectangle;
    QList<QPointF>        out;
    out.reserve(nodes.size());
    for (const QPointF& n : nodes)
        out.append(QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height()));
    return out;
}

/**
 * @brief The nodes joined by straight edges, each corner rounded by an arc of `cornerRadius`.
 *
 * A corner's arc is tangent to both its edges, and never reaches past the middle of either, so two
 * neighbouring corners cannot overlap; a sharp corner therefore rounds less than a wide one. The arc is
 * one cubic. Self-intersecting nodes are allowed, and filled by the non-zero rule.
 */
QPainterPath polygonPath(const ShapeProperties& shape, const QRectF& r)
{
    const QList<QPointF> pts    = polygonPoints(shape, r);
    const qreal          radius = qMax(0.0, shape.cornerRadius);
    const int            n      = int(pts.size());

    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    for (int i = 0; i < n; ++i) {
        const QPointF v  = pts[i];
        const QPointF a  = pts[(i + n - 1) % n] - v;   // towards the previous vertex
        const QPointF b  = pts[(i + 1) % n] - v;       // towards the next one
        const qreal   la = std::hypot(a.x(), a.y());
        const qreal   lb = std::hypot(b.x(), b.y());

        // How far the arc's ends sit from the vertex: r / tan(θ/2) for a corner of angle θ, no further
        // than half the shorter edge. Nothing to round at a zero-length edge or a hairpin.
        qreal tanHalf = 0.0;
        qreal reach   = 0.0;
        if (radius > 0.0 && la > 0.0 && lb > 0.0) {
            const qreal cosine = qBound(-1.0, (a.x() * b.x() + a.y() * b.y()) / (la * lb), 1.0);
            tanHalf            = std::tan(std::acos(cosine) / 2.0);
            if (tanHalf > k_hairpinCorner)
                reach = qMin(radius / tanHalf, qMin(la, lb) / 2.0);
        }
        if (reach <= 0.0) {
            if (i == 0) p.moveTo(v); else p.lineTo(v);
            continue;
        }

        const QPointF ua   = a / la;
        const QPointF ub   = b / lb;
        const QPointF from = v + ua * reach;
        const QPointF to   = v + ub * reach;
        // A circular arc of sweep φ as one cubic: handles 4/3·tan(φ/4)·r long, along the edges.
        const qreal arcRadius = reach * tanHalf;
        const qreal sweep     = M_PI - 2.0 * std::atan(tanHalf);
        const qreal handle    = 4.0 / 3.0 * std::tan(sweep / 4.0) * arcRadius;
        if (i == 0) p.moveTo(from); else p.lineTo(from);
        p.cubicTo(from - ua * handle, to - ub * handle, to);
    }
    p.closeSubpath();
    return p;
}

/**
 * @brief The largest axis-aligned rectangle inside @p outline, found on a grid of samples over @p body.
 *
 * For an outline with no formula of its own: each row of samples is filled between the outline's
 * crossings (by its own fill rule), and the largest rectangle of inside samples wins, the one nearest the
 * middle among equals; then each side is pushed out to the outline. It stays inside but for a notch
 * narrower than a cell. The grid is relative to the body, so an outline that scales with the box gives a
 * rectangle that scales with it too, which is what the box fit's search relies on. It returns an area
 * and places nothing.
 *
 * ponytail: a grid, not the exact polygon answer, so a rectangle of another proportion that is a little
 * larger can be missed by under a cell; the exact one is a polygon algorithm, if that ever shows.
 */
QRectF largestInscribedRect(const QPainterPath& outline, const QRectF& body)
{
    constexpr int n  = k_areaSamples;
    const qreal   sx = body.width() / n;
    const qreal   sy = body.height() / n;
    const auto    at = [&](int col, int row) {
        return QPointF(body.left() + (col + 0.5) * sx, body.top() + (row + 0.5) * sy);
    };

    // Which samples are inside: each row against the outline's edges.
    const QList<QPolygonF> rings   = outline.toSubpathPolygons();
    const bool             winding = outline.fillRule() == Qt::WindingFill;
    std::array<std::array<bool, n>, n> inside{};
    std::vector<std::pair<qreal, int>> crossings;
    for (int row = 0; row < n; ++row) {
        const qreal y = at(0, row).y();
        crossings.clear();
        for (const QPolygonF& ring : rings) {
            for (qsizetype k = 0; k < ring.size(); ++k) {
                const QPointF e0 = ring[k];
                const QPointF e1 = ring[(k + 1) % ring.size()];
                if ((e0.y() <= y) == (e1.y() <= y))
                    continue;
                const qreal x = e0.x() + (y - e0.y()) * (e1.x() - e0.x()) / (e1.y() - e0.y());
                crossings.push_back({x, e1.y() > e0.y() ? 1 : -1});
            }
        }
        std::sort(crossings.begin(), crossings.end());
        size_t next  = 0;
        int    count = 0;
        for (int col = 0; col < n; ++col) {
            const qreal x = at(col, row).x();
            for (; next < crossings.size() && crossings[next].first < x; ++next)
                count += winding ? crossings[next].second : 1;
            inside[row][col] = winding ? count != 0 : (count % 2) != 0;
        }
    }

    // The largest rectangle of inside samples: each row is the floor of a histogram of the columns'
    // runs of inside samples above it, and a histogram's largest rectangle is one pass with a stack.
    std::array<int, n> run{};
    int                bestArea = 0;
    qreal              bestOff  = 0.0;
    QRect              best;
    const qreal        middle   = (n - 1) / 2.0;
    std::vector<int>   stack;
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col)
            run[col] = inside[row][col] ? run[col] + 1 : 0;
        stack.clear();
        for (int col = 0; col <= n; ++col) {
            const int h = col < n ? run[col] : 0;
            while (!stack.empty() && run[stack.back()] >= h) {
                const int height = run[stack.back()];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const int area = height * (col - left);
                if (area == 0 || area < bestArea)
                    continue;
                // Among equals, the one whose middle is nearest the balloon's.
                const qreal cx  = (left + col - 1) / 2.0 - middle;
                const qreal cy  = (2 * row - height + 1) / 2.0 - middle;
                const qreal off = std::hypot(cx, cy);
                if (area > bestArea || off < bestOff) {
                    bestArea = area;
                    bestOff  = off;
                    best     = QRect(left, row - height + 1, col - left, height);
                }
            }
            stack.push_back(col);
        }
    }
    if (bestArea == 0)
        return QRectF(body.center(), QSizeF(0, 0));

    // The grid's rectangle runs between sample centres, so it stops up to a cell short of the outline
    // on every side. Each side is pushed back out, as far as the outline lets it, within that cell.
    // Asked of the even-odd copy: for a non-zero path, QPainterPath::contains(QRectF) answers a
    // rectangle that crosses the outline by sampling a few points, which let a side through a notch.
    QPainterPath strict = outline;
    strict.setFillRule(Qt::OddEvenFill);
    QRectF rect(at(best.left(), best.top()), at(best.right(), best.bottom()));
    const auto grow = [&](auto moveSide, qreal cell) {
        qreal fits = 0.0;
        qreal past = cell;
        for (int i = 0; i < k_growSteps; ++i) {
            const qreal mid   = (fits + past) / 2.0;
            QRectF      trial = rect;
            moveSide(trial, mid);
            (strict.contains(trial) ? fits : past) = mid;
        }
        moveSide(rect, fits);
    };
    grow([](QRectF& r, qreal d) { r.setLeft(r.left() - d); }, sx);
    grow([](QRectF& r, qreal d) { r.setRight(r.right() + d); }, sx);
    grow([](QRectF& r, qreal d) { r.setTop(r.top() - d); }, sy);
    grow([](QRectF& r, qreal d) { r.setBottom(r.bottom() + d); }, sy);
    return rect;
}

QRectF polygonArea(const ShapeProperties& shape, const QRectF& body)
{
    return largestInscribedRect(polygonPath(shape, body), body);
}

// ---------------------------------------------------------------------------
// Thought
// ---------------------------------------------------------------------------

/**
 * @brief A thought balloon: a ring of overlapping circles around an inner ellipse.
 *
 * United rather than drawn as separate circles, so the arcs *inside* the outline disappear and one
 * stroke follows the scalloped edge. The inner ellipse is what keeps the middle solid when the lobes
 * are small relative to the balloon.
 */
QPainterPath thoughtPath(const ShapeProperties&, const QRectF& r)
{
    const QPointF c    = r.center();
    const qreal   rx   = r.width()  / 2.0;
    const qreal   ry   = r.height() / 2.0;
    const qreal   lobe = qMax(2.0, qMin(rx, ry) * k_thoughtLobe);

    QPainterPath p;
    p.addEllipse(c, qMax(1.0, rx - lobe * 0.95), qMax(1.0, ry - lobe * 0.95));
    for (int i = 0; i < k_thoughtLobes; ++i) {
        const qreal   ang = (2.0 * M_PI * i) / k_thoughtLobes;
        const QPointF at(c.x() + qCos(ang) * (rx - lobe), c.y() + qSin(ang) * (ry - lobe));
        QPainterPath  bump;
        bump.addEllipse(at, lobe, lobe);
        p = p.united(bump);
    }
    return p;
}

QRectF thoughtArea(const ShapeProperties& shape, const QRectF& body)
{
    // The lobes eat into the ring, so the safe area is the inner ellipse rather than the whole one.
    const qreal lobe = qMin(body.width(), body.height()) * k_thoughtLobe * 0.5;
    return inscribedInEllipse(shape, body.adjusted(lobe, lobe, -lobe, -lobe));
}

// ---------------------------------------------------------------------------
// Scroll, Banner
// ---------------------------------------------------------------------------

//! An unrolled scroll: a panel with bowed long edges and a rolled end on each side.
QPainterPath scrollPath(const ShapeProperties& shape, const QRectF& r)
{
    const qreal  roll = qMax(2.0, qMin(r.width() * k_scrollRoll, r.height() / 2.0));
    const qreal  bow  = r.height() * k_scrollBow;
    const QRectF body = r.adjusted(roll, 0, -roll, 0);
    if (body.width() <= 1.0)
        return boxPath(shape, r);   // too narrow to roll: fall back to something still drawable

    QPainterPath p;
    p.moveTo(body.left(), body.top());
    p.quadTo(body.center().x(), body.top() + bow, body.right(), body.top());
    // Qt measures arcs in counter-clockwise degrees; a -180 sweep from the top bulges the roll outward.
    p.arcTo(QRectF(body.right() - roll, body.top(), roll * 2.0, body.height()), 90.0, -180.0);
    p.quadTo(body.center().x(), body.bottom() - bow, body.left(), body.bottom());
    p.arcTo(QRectF(body.left() - roll, body.top(), roll * 2.0, body.height()), -90.0, -180.0);
    p.closeSubpath();
    return p;
}

QRectF scrollArea(const ShapeProperties&, const QRectF& body)
{
    const qreal roll = qMin(body.width() * k_scrollRoll, body.height() / 2.0);
    const qreal bow  = body.height() * k_scrollBow;
    return body.adjusted(roll, bow, -roll, -bow);
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

using Kind = ShapeProperties::Kind;

//! In the enum's order, so a kind is its own index; shapeInfo() checks every entry names its kind.
// The flash stretches were chosen by eye, shape by shape, on rendered sheets (PLAN-S A18); the Box's is
// a round number between its two templates' old values, to be checked by eye (PLAN-L B22).
const std::array<ShapeInfo, int(ShapeProperties::k_lastKind) + 1> k_shapes{{
    {Kind::None,      "none",      QT_TRANSLATE_NOOP("QObject", "Text only — no balloon"), false, false, nullptr,       wholeArea,          0},
    {Kind::Box,       "box",       QT_TRANSLATE_NOOP("QObject", "Box"),                    true,  true,  boxPath,       boxArea,            50},
    {Kind::Ellipse,   "ellipse",   QT_TRANSLATE_NOOP("QObject", "Round balloon"),          true,  false, ellipsePath,   inscribedInEllipse, 0},
    {Kind::Polygon,   "polygon",   QT_TRANSLATE_NOOP("QObject", "Polygon"),                false, true,  polygonPath,   polygonArea,        0},
    {Kind::Thought,   "thought",   QT_TRANSLATE_NOOP("QObject", "Thought balloon"),        true,  false, thoughtPath,   thoughtArea,        0},
    {Kind::Scroll,    "scroll",    QT_TRANSLATE_NOOP("QObject", "Scroll"),                 false, false, scrollPath,    scrollArea,         50},
}};

//! A kind with every parameter at its default — the template of a kind that has no parameters.
ShapeProperties plain(Kind kind)
{
    ShapeProperties s;
    s.kind = kind;
    return s;
}

//! A Box with corner radius @p radius.
ShapeProperties box(qreal radius)
{
    ShapeProperties s = plain(Kind::Box);
    s.cornerRadius    = radius;
    return s;
}

/**
 * @brief A sharp-cornered Polygon through @p nodes, rounded to the decimals a node is saved with, so a
 *        placed template reads back from its file as itself.
 */
ShapeProperties polygon(const QList<QPointF>& nodes)
{
    const qreal     scale = std::pow(10.0, ShapeProperties::k_nodeDecimals);
    ShapeProperties s     = plain(Kind::Polygon);
    s.cornerRadius        = 0.0;
    for (const QPointF& n : nodes)
        s.nodes.append(QPointF(qRound(n.x() * scale) / scale, qRound(n.y() * scale) / scale));
    return s;
}

//! A star of @p spikes points, its inner vertices at @p inner of the outer radius, the first one on top.
QList<QPointF> starNodes(int spikes, qreal inner)
{
    QList<QPointF> out;
    for (int i = 0; i < spikes * 2; ++i) {
        const qreal ang = (M_PI * i) / spikes - M_PI / 2.0;
        const qreal k   = (i % 2 == 0) ? 0.5 : 0.5 * inner;
        out.append(QPointF(0.5 + qCos(ang) * k, 0.5 + qSin(ang) * k));
    }
    return out;
}

} // namespace

const ShapeInfo& shapeInfo(ShapeProperties::Kind kind)
{
    const int i = int(kind);
    // A kind past the table is a file from a newer build, or a corrupt value: draw a Box, the fallback
    // the readers already use, rather than reading past the end.
    if (i < 0 || i >= int(k_shapes.size()))
        return k_shapes[int(Kind::Box)];
    Q_ASSERT(k_shapes[i].kind == kind);   // the table is out of the enum's order
    return k_shapes[i];
}

const QList<ShapeTemplate>& shapeTemplates()
{
    // The picker's order: the speaking shapes first, the most used of them leading.
    static const QList<ShapeTemplate> templates{
        {"speech",    QT_TRANSLATE_NOOP("QObject", "Speech balloon"),  plain(Kind::Box),       true},
        {"ellipse",   QT_TRANSLATE_NOOP("QObject", "Round balloon"),   plain(Kind::Ellipse),   true},
        {"thought",   QT_TRANSLATE_NOOP("QObject", "Thought balloon"), plain(Kind::Thought),   true},
        {"shout",     QT_TRANSLATE_NOOP("QObject", "Shout"),
         polygon(starNodes(k_burstSpikes, k_burstInner)), true},
        {"caption",   QT_TRANSLATE_NOOP("QObject", "Caption box"),     box(k_captionCorner),   false},
        // A caption plate seen in perspective: full width at the bottom, narrowed at the top.
        {"trapezoid", QT_TRANSLATE_NOOP("QObject", "Caption plate"),
         polygon({{k_trapezoidSlant, 0}, {1 - k_trapezoidSlant, 0}, {1, 1}, {0, 1}}), false},
        {"diamond",   QT_TRANSLATE_NOOP("QObject", "Diamond"),
         polygon({{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}), false},
        // A ribbon with a V notched into each end.
        {"banner",    QT_TRANSLATE_NOOP("QObject", "Banner"),
         polygon({{0, 0}, {1, 0}, {1 - k_bannerNotch, 0.5}, {1, 1}, {0, 1}, {k_bannerNotch, 0.5}}), false},
        {"scroll",    QT_TRANSLATE_NOOP("QObject", "Scroll"),          plain(Kind::Scroll),    false},
    };
    return templates;
}

const ShapeTemplate& shapeTemplate(const char* id)
{
    for (const ShapeTemplate& t : shapeTemplates())
        if (qstrcmp(t.id, id) == 0)
            return t;
    Q_ASSERT_X(false, "shapeTemplate", "no template has this id");
    return shapeTemplates().first();
}

const ShapeTemplate* templateOf(const ShapeProperties& shape)
{
    for (const ShapeTemplate& t : shapeTemplates())
        if (t.shape == shape)
            return &t;
    return nullptr;
}

bool shapeSpeaks(const ShapeProperties& shape)
{
    const ShapeTemplate* t = templateOf(shape);
    return t ? t->speaks : shapeInfo(shape.kind).speaks;
}

qreal cornerRadiusLimit(const QSizeF& size)
{
    return qMin(size.width(), size.height()) / 2.0;
}

QRectF balloonRect(const ObjectRecord& a)
{
    const qreal  sw = a.skin.strokeWidth / 2.0;
    QRectF body(0, 0, a.box.width(), a.box.height());
    body.adjust(sw, sw, -sw, -sw);
    return body.normalized();
}

QPainterPath outlineOf(const ObjectRecord& a, const QRectF& body)
{
    if (!a.hasSilhouette() || body.isEmpty())
        return {};
    const auto outline = shapeInfo(a.shape.kind).outline;
    return outline ? outline(a.shape, body) : QPainterPath();
}

}  // namespace Geometry
