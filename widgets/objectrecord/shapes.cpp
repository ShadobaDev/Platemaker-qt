#include "shapes.hpp"

#include <QObject>
#include <QtMath>

#include <array>

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
 *  A speech balloon's corner radius, as a fraction of its height.
 */
constexpr qreal k_speechCorner   = 0.28;
/**
 *  What the speech balloon's rounded corners cost the lettering in width, on each side.
 */
constexpr qreal k_speechInset    = 6.0;
/**
 *  A caption box's corner radius, in balloon pixels.
 */
constexpr qreal k_captionCorner  = 4.0;

//! The largest axis-aligned rectangle inside the ellipse inscribed in \p r.
QRectF inscribedInEllipse(const QRectF& r)
{
    const qreal kx = r.width()  * (1.0 - k_invSqrt2) / 2.0;
    const qreal ky = r.height() * (1.0 - k_invSqrt2) / 2.0;
    return r.adjusted(kx, ky, -kx, -ky);
}

//! The whole rectangle: the lettering may use all of it.
QRectF wholeArea(const QRectF& body)
{
    return body;
}

// ---------------------------------------------------------------------------
// Speech, Caption, Ellipse
// ---------------------------------------------------------------------------

QPainterPath speechPath(const QRectF& r)
{
    QPainterPath p;
    p.addRoundedRect(r, r.height() * k_speechCorner, r.height() * k_speechCorner);
    return p;
}

QRectF speechArea(const QRectF& body)
{
    return body.adjusted(k_speechInset, 0, -k_speechInset, 0);   // the rounded corners cost a little width
}

QPainterPath captionPath(const QRectF& r)
{
    QPainterPath p;
    p.addRoundedRect(r, k_captionCorner, k_captionCorner);
    return p;
}

QPainterPath ellipsePath(const QRectF& r)
{
    QPainterPath p;
    p.addEllipse(r);
    return p;
}

// ---------------------------------------------------------------------------
// Shout
// ---------------------------------------------------------------------------

//! A star polygon inscribed in \p r — the "shout" silhouette.
QPainterPath burstPath(const QRectF& r)
{
    const QPointF c  = r.center();
    const qreal   rx = r.width()  / 2.0;
    const qreal   ry = r.height() / 2.0;

    QPainterPath p;
    const int steps = k_burstSpikes * 2;
    for (int i = 0; i < steps; ++i) {
        const qreal ang = (2.0 * M_PI * i) / steps - M_PI / 2.0;
        const qreal k   = (i % 2 == 0) ? 1.0 : k_burstInner;
        const QPointF pt(c.x() + qCos(ang) * rx * k, c.y() + qSin(ang) * ry * k);
        if (i == 0) p.moveTo(pt); else p.lineTo(pt);
    }
    p.closeSubpath();
    return p;
}

QRectF burstArea(const QRectF& body)
{
    // A star's usable interior is its *inner* radius, not its bounding box — inset accordingly, or the
    // text runs out between the spikes.
    const qreal kx = body.width()  * (1.0 - k_burstInner * 0.92) / 2.0;
    const qreal ky = body.height() * (1.0 - k_burstInner * 0.92) / 2.0;
    return body.adjusted(kx, ky, -kx, -ky);
}

// ---------------------------------------------------------------------------
// Diamond, Trapezoid
// ---------------------------------------------------------------------------

//! A rhombus with its vertices at the midpoints of \p r's edges.
QPainterPath diamondPath(const QRectF& r)
{
    const QPointF c = r.center();
    QPainterPath p;
    p.moveTo(c.x(), r.top());
    p.lineTo(r.right(), c.y());
    p.lineTo(c.x(), r.bottom());
    p.lineTo(r.left(), c.y());
    p.closeSubpath();
    return p;
}

QRectF diamondArea(const QRectF& body)
{
    // The largest rectangle inside a rhombus is half its width and half its height, centred.
    return body.adjusted(body.width() / 4.0, body.height() / 4.0, -body.width() / 4.0, -body.height() / 4.0);
}

//! A trapezoid: full width at the bottom, narrowed at the top. Reads as a caption plate seen in
//! perspective, which is what it is usually for.
QPainterPath trapezoidPath(const QRectF& r)
{
    const qreal inset = r.width() * k_trapezoidSlant;
    QPainterPath p;
    p.moveTo(r.left() + inset, r.top());
    p.lineTo(r.right() - inset, r.top());
    p.lineTo(r.right(), r.bottom());
    p.lineTo(r.left(), r.bottom());
    p.closeSubpath();
    return p;
}

QRectF trapezoidArea(const QRectF& body)
{
    // Narrowest at the top, so the top width is the only one safe for every line.
    const qreal inset = body.width() * k_trapezoidSlant;
    return body.adjusted(inset, 0, -inset, 0);
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
QPainterPath thoughtPath(const QRectF& r)
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

QRectF thoughtArea(const QRectF& body)
{
    // The lobes eat into the ring, so the safe area is the inner ellipse rather than the whole one.
    const qreal lobe = qMin(body.width(), body.height()) * k_thoughtLobe * 0.5;
    return inscribedInEllipse(body.adjusted(lobe, lobe, -lobe, -lobe));
}

// ---------------------------------------------------------------------------
// Scroll, Banner
// ---------------------------------------------------------------------------

//! An unrolled scroll: a panel with bowed long edges and a rolled end on each side.
QPainterPath scrollPath(const QRectF& r)
{
    const qreal  roll = qMax(2.0, qMin(r.width() * k_scrollRoll, r.height() / 2.0));
    const qreal  bow  = r.height() * k_scrollBow;
    const QRectF body = r.adjusted(roll, 0, -roll, 0);
    if (body.width() <= 1.0)
        return trapezoidPath(r);   // too narrow to roll: fall back to something still drawable

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

QRectF scrollArea(const QRectF& body)
{
    const qreal roll = qMin(body.width() * k_scrollRoll, body.height() / 2.0);
    const qreal bow  = body.height() * k_scrollBow;
    return body.adjusted(roll, bow, -roll, -bow);
}

//! A ribbon with a V notched into each end.
QPainterPath bannerPath(const QRectF& r)
{
    const qreal n = qMin(r.width() * k_bannerNotch, r.width() / 3.0);
    QPainterPath p;
    p.moveTo(r.left(), r.top());
    p.lineTo(r.right(), r.top());
    p.lineTo(r.right() - n, r.center().y());
    p.lineTo(r.right(), r.bottom());
    p.lineTo(r.left(), r.bottom());
    p.lineTo(r.left() + n, r.center().y());
    p.closeSubpath();
    return p;
}

QRectF bannerArea(const QRectF& body)
{
    const qreal n = qMin(body.width() * k_bannerNotch, body.width() / 3.0);
    return body.adjusted(n, 0, -n, 0);
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

using Kind = ShapeProperties::Kind;

//! In the enum's order, so a kind is its own index; shapeInfo() checks every entry names its kind.
const std::array<ShapeInfo, int(ShapeProperties::k_lastKind) + 1> k_shapes{{
    {Kind::None,      "none",      QT_TRANSLATE_NOOP("QObject", "Text only — no balloon"), false, nullptr,       wholeArea},
    {Kind::Speech,    "speech",    QT_TRANSLATE_NOOP("QObject", "Speech balloon"),         true,  speechPath,    speechArea},
    {Kind::Shout,     "shout",     QT_TRANSLATE_NOOP("QObject", "Shout"),                  true,  burstPath,     burstArea},
    {Kind::Caption,   "caption",   QT_TRANSLATE_NOOP("QObject", "Caption box"),            false, captionPath,   wholeArea},
    {Kind::Ellipse,   "ellipse",   QT_TRANSLATE_NOOP("QObject", "Round balloon"),          true,  ellipsePath,   inscribedInEllipse},
    {Kind::Diamond,   "diamond",   QT_TRANSLATE_NOOP("QObject", "Diamond"),                false, diamondPath,   diamondArea},
    {Kind::Trapezoid, "trapezoid", QT_TRANSLATE_NOOP("QObject", "Caption plate"),          false, trapezoidPath, trapezoidArea},
    {Kind::Thought,   "thought",   QT_TRANSLATE_NOOP("QObject", "Thought balloon"),        true,  thoughtPath,   thoughtArea},
    {Kind::Scroll,    "scroll",    QT_TRANSLATE_NOOP("QObject", "Scroll"),                 false, scrollPath,    scrollArea},
    {Kind::Banner,    "banner",    QT_TRANSLATE_NOOP("QObject", "Banner"),                 false, bannerPath,    bannerArea},
}};

} // namespace

const ShapeInfo& shapeInfo(ShapeProperties::Kind kind)
{
    const int i = int(kind);
    // A kind past the table is a file from a newer build, or a corrupt value: draw a speech balloon,
    // the fallback the readers already use, rather than reading past the end.
    if (i < 0 || i >= int(k_shapes.size()))
        return k_shapes[int(Kind::Speech)];
    Q_ASSERT(k_shapes[i].kind == kind);   // the table is out of the enum's order
    return k_shapes[i];
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
    return outline ? outline(body) : QPainterPath();
}

}  // namespace Geometry
