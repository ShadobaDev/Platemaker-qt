#include "artifactpainter.h"

#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

#include <cmath>

namespace {

//! How far the tail's base is pulled back inside the silhouette, as a fraction of its width. Enough
//! that the union is watertight, little enough that the taper still reads from where it emerges.
constexpr qreal k_tailBaseInset = 0.6;
//! Binary-search steps for the silhouette crossing. 24 halvings resolve a 4000px balloon to well under
//! a pixel, and each step is one QPainterPath::contains().
constexpr int   k_edgeSteps     = 24;
//! Points on a "shout" burst — 12 spikes reads as a shout without turning into a sunburst.
constexpr int   k_burstSpikes   = 12;
//! Inner radius of the burst, as a fraction of the outer one.
constexpr qreal k_burstInner    = 0.74;
//! Re-measure passes in fittedBox(); it converges in two or three, this is headroom.
constexpr int   k_fitPasses     = 6;
//! 1/sqrt(2) — the half-axis fraction at which a rectangle inscribes an ellipse.
constexpr qreal k_invSqrt2      = 0.70710678118654752;
//! Fraction of the width the trapezoid's top edge loses on each side.
constexpr qreal k_trapezoidSlant = 0.16;
//! Lobes around a thought balloon. Odd, so no two sit exactly opposite and it reads as drawn, not
//! generated.
constexpr int   k_thoughtLobes  = 11;
//! Lobe radius, as a fraction of the balloon's shorter half-axis.
constexpr qreal k_thoughtLobe   = 0.40;
//! Depth of a banner's end notch, as a fraction of its width.
constexpr qreal k_bannerNotch   = 0.12;
//! Width of a scroll's rolled end, as a fraction of the balloon's width.
constexpr qreal k_scrollRoll    = 0.10;
//! How far a scroll's long edges bow inward, as a fraction of its height.
constexpr qreal k_scrollBow     = 0.06;

/**
 * @brief The balloon's own rectangle inside the box — the box, less room for the stroke.
 *
 * The box *is* the balloon now. It used to double as the artifact's whole extent, with a fixed 18 % of
 * its height reserved at the bottom for the tail to live in — which is what limited a tail to the
 * bottom edge and to inside the box. The drawn extent is computed instead (artifactBounds()).
 */
QRectF balloonRect(const TextArtifact& a)
{
    const qreal  sw = a.strokeWidth / 2.0;
    QRectF body(0, 0, a.box.width(), a.box.height());
    body.adjust(sw, sw, -sw, -sw);
    return body.normalized();
}

//! The largest axis-aligned rectangle inside the ellipse inscribed in \p r.
QRectF inscribedInEllipse(const QRectF& r)
{
    const qreal kx = r.width()  * (1.0 - k_invSqrt2) / 2.0;
    const qreal ky = r.height() * (1.0 - k_invSqrt2) / 2.0;
    return r.adjusted(kx, ky, -kx, -ky);
}

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

/**
 * @brief One tail, from wherever the ray to its tip leaves the silhouette out to that tip.
 *
 * Shape-agnostic by construction. Rather than knowing where a given outline's edge is, it casts a ray
 * from the balloon's centre toward the tip and binary-searches for the crossing with
 * QPainterPath::contains(). That works for a rounded rectangle, a burst, and anything added later,
 * and it is what lets a tail leave any edge instead of only the bottom one.
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
    ray /= len;

    // Find the crossing as a fraction of the distance to the tip. Extend past the tip first if it lies
    // inside the balloon, so the search always brackets an inside/outside pair.
    qreal outside = 1.0;
    while (outside < 8.0 && outline.contains(centre + ray * (len * outside)))
        outside *= 1.5;
    qreal inside = 0.0;
    for (int i = 0; i < k_edgeSteps; ++i) {
        const qreal mid = (inside + outside) / 2.0;
        if (outline.contains(centre + ray * (len * mid)))
            inside = mid;
        else
            outside = mid;
    }

    const qreal edge = len * inside;
    if (edge >= len)
        return {};                       // the tip sits inside the balloon: nothing would show

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

/**
 * @brief The rectangle text may occupy — inset from the silhouette so letters do not touch the stroke.
 *
 * This is the half of a shape that takes the thought. A path is a few lines; knowing where text fits
 * *inside* it is what stops a wide bubble spilling its words out between a burst's spikes or past a
 * diamond's slope. Every case here is the largest axis-aligned rectangle the outline actually contains,
 * then inset for the stroke.
 */
QRectF textSafeArea(const TextArtifact& a, const QRectF& body)
{
    if (a.shape == TextArtifact::Shape::None)
        return QRectF(0, 0, a.box.width(), a.box.height());

    const qreal pad = a.strokeWidth + 8.0;
    QRectF      usable;

    switch (a.shape) {
    case TextArtifact::Shape::Shout: {
        // A star's usable interior is its *inner* radius, not its bounding box — inset accordingly, or
        // the text runs out between the spikes.
        const qreal kx = body.width()  * (1.0 - k_burstInner * 0.92) / 2.0;
        const qreal ky = body.height() * (1.0 - k_burstInner * 0.92) / 2.0;
        usable = body.adjusted(kx, ky, -kx, -ky);
        break;
    }
    case TextArtifact::Shape::Ellipse:
        usable = inscribedInEllipse(body);
        break;
    case TextArtifact::Shape::Thought:
        // The lobes eat into the ring, so the safe area is the inner ellipse rather than the whole one.
        usable = inscribedInEllipse(body.adjusted(qMin(body.width(), body.height()) * k_thoughtLobe * 0.5,
                                                  qMin(body.width(), body.height()) * k_thoughtLobe * 0.5,
                                                  -qMin(body.width(), body.height()) * k_thoughtLobe * 0.5,
                                                  -qMin(body.width(), body.height()) * k_thoughtLobe * 0.5));
        break;
    case TextArtifact::Shape::Diamond:
        // The largest rectangle inside a rhombus is half its width and half its height, centred.
        usable = body.adjusted(body.width() / 4.0, body.height() / 4.0,
                               -body.width() / 4.0, -body.height() / 4.0);
        break;
    case TextArtifact::Shape::Trapezoid: {
        // Narrowest at the top, so the top width is the only one safe for every line.
        const qreal inset = body.width() * k_trapezoidSlant;
        usable = body.adjusted(inset, 0, -inset, 0);
        break;
    }
    case TextArtifact::Shape::Scroll: {
        const qreal roll = qMin(body.width() * k_scrollRoll, body.height() / 2.0);
        const qreal bow  = body.height() * k_scrollBow;
        usable = body.adjusted(roll, bow, -roll, -bow);
        break;
    }
    case TextArtifact::Shape::Banner: {
        const qreal n = qMin(body.width() * k_bannerNotch, body.width() / 3.0);
        usable = body.adjusted(n, 0, -n, 0);
        break;
    }
    case TextArtifact::Shape::Speech:
        usable = body.adjusted(6.0, 0, -6.0, 0);   // the rounded corners cost a little width
        break;
    case TextArtifact::Shape::Caption:
    case TextArtifact::Shape::None:
        usable = body;
        break;
    }

    return usable.adjusted(pad, pad, -pad, -pad);
}

//! The laid-out text, ready to draw or measure. Width-bound; height falls out of the wrap.
void layOutText(QTextDocument& doc, const TextArtifact& a, qreal width)
{
    QFont f;
    if (!a.fontFamily.isEmpty())
        f.setFamily(a.fontFamily);
    f.setPixelSize(qMax(1, a.fontPixelSize));
    f.setBold(a.bold);

    QTextOption opt;
    opt.setAlignment(static_cast<Qt::Alignment>(a.align));
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    doc.setDefaultFont(f);
    doc.setDefaultTextOption(opt);
    doc.setDocumentMargin(0);
    doc.setPlainText(a.text);
    doc.setTextWidth(qMax(qreal(1), width));
}

} // namespace

// ---------------------------------------------------------------------------
// Rasterising
// ---------------------------------------------------------------------------

QPainterPath artifactSilhouette(const TextArtifact& a)
{
    const QRectF body = balloonRect(a);
    if (a.shape == TextArtifact::Shape::None || body.isEmpty())
        return {};

    QPainterPath path;
    switch (a.shape) {
    case TextArtifact::Shape::Speech:
        path.addRoundedRect(body, body.height() * 0.28, body.height() * 0.28);
        break;
    case TextArtifact::Shape::Caption:
        path.addRoundedRect(body, 4, 4);
        break;
    case TextArtifact::Shape::Shout:
        path = burstPath(body);
        break;
    case TextArtifact::Shape::Ellipse:
        path.addEllipse(body);
        break;
    case TextArtifact::Shape::Diamond:
        path = diamondPath(body);
        break;
    case TextArtifact::Shape::Trapezoid:
        path = trapezoidPath(body);
        break;
    case TextArtifact::Shape::Thought:
        path = thoughtPath(body);
        break;
    case TextArtifact::Shape::Scroll:
        path = scrollPath(body);
        break;
    case TextArtifact::Shape::Banner:
        path = bannerPath(body);
        break;
    case TextArtifact::Shape::None:
        break;
    }
    // Each tail is aimed at the *bare* outline, not at the accumulating union: otherwise the second
    // tail would ray-cast against the first one and emerge from its flank.
    const QPainterPath outline = path;
    for (const Tail& t : a.tails) {
        const QPainterPath tp = tailPath(outline, body, t);
        if (!tp.isEmpty())
            path = path.united(tp);
    }
    return path;
}

QRectF artifactBounds(const TextArtifact& a)
{
    return artifactBoundsOf(a, artifactSilhouette(a), artifactTextOutline(a));
}

QRectF artifactBoundsOf(const TextArtifact& a, const QPainterPath& silhouette, const QPainterPath& text)
{
    // The balloon always counts, even when nothing is drawn in it — an empty shapeless artifact still
    // occupies the box the author dragged.
    QRectF r(0, 0, a.box.width(), a.box.height());

    if (!silhouette.isEmpty())
        r = r.united(silhouette.boundingRect());
    if (!text.isEmpty())
        r = r.united(text.boundingRect());

    // The stroke straddles the path, so half of it lies outside; one more pixel keeps antialiasing off
    // the edge of the buffer.
    const qreal pad = a.strokeWidth / 2.0 + 1.0;
    r = r.adjusted(-pad, -pad, pad, pad);

    // Whole pixels, so the rasterised buffer and the SVG viewBox describe the same rectangle rather
    // than two roundings of it.
    const qreal left = qFloor(r.left());
    const qreal top  = qFloor(r.top());
    return QRectF(left, top, qCeil(r.right()) - left, qCeil(r.bottom()) - top);
}

QPainterPath artifactTextOutline(const TextArtifact& a)
{
    if (a.text.isEmpty())
        return {};

    const QRectF body = balloonRect(a);
    const QRectF safe = textSafeArea(a, body);
    if (safe.width() <= 1 || safe.height() <= 1)
        return {};

    QTextDocument doc;
    layOutText(doc, a, safe.width());

    // Vertically centred in the safe area — a bubble's text sits in the middle of the balloon, never
    // pinned to its top edge.
    const qreal   h = doc.size().height();
    const QPointF origin(safe.left(), safe.top() + qMax(qreal(0), (safe.height() - h) / 2.0));

    // The document does the wrapping — unchanged — and then each laid-out line is converted to glyph
    // outlines at the position the layout gave it. Going through the layout rather than re-wrapping by
    // hand is what keeps this identical to what the document would have painted; going to *outlines* is
    // what lets the same geometry be written into an SVG the library can rasterise without a font.
    const QFont  f = doc.defaultFont();
    QPainterPath out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextLayout* lay = b.layout();
        if (!lay)
            continue;
        const QString blockText = b.text();
        for (int i = 0; i < lay->lineCount(); ++i) {
            const QTextLine line = lay->lineAt(i);
            const QString   s    = blockText.mid(line.textStart(), line.textLength());
            if (s.trimmed().isEmpty())
                continue;
            // naturalTextRect(), *not* position(): Qt stores every line at the layout's left edge and
            // applies the alignment offset inside QTextLine::draw(). Nothing here goes through draw(),
            // so reading position() silently left-aligns every bubble whatever the setting says.
            // naturalTextRect() is the accessor that has the offset already folded in.
            const QPointF p = lay->position() + line.naturalTextRect().topLeft();
            // addText places by the text *baseline*, which is the line's top plus its ascent.
            out.addText(origin.x() + p.x(), origin.y() + p.y() + line.ascent(), f, s);
        }
    }
    if (out.isEmpty())
        return out;

    // The old painter clipped to the safe area so an overlong string could not bleed past the stroke.
    // Intersecting keeps that guarantee in vector form, so it survives into the SVG too.
    QPainterPath clip;
    clip.addRect(safe);
    return out.intersected(clip);
}

void paintArtifact(QPainter& painter, const TextArtifact& a)
{
    paintArtifactPaths(painter, a, artifactSilhouette(a), artifactTextOutline(a));
}

void paintArtifactPaths(QPainter& painter, const TextArtifact& a,
                        const QPainterPath& silhouette, const QPainterPath& text)
{
    if (a.box.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!silhouette.isEmpty()) {
        painter.fillPath(silhouette, a.fill);
        if (a.strokeWidth > 0) {
            QPen pen(a.stroke, a.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(silhouette, pen);
        }
    }

    // Filled outlines rather than drawn text, so that what is on screen is what artifactToSvg() writes
    // and what the library rasterises — one geometry, three consumers.
    if (!text.isEmpty())
        painter.fillPath(text, a.textColour);

    painter.restore();
}

QImage renderArtifact(const TextArtifact& a)
{
    if (a.box.isEmpty())
        return {};

    const QRectF bounds = artifactBounds(a);
    if (bounds.isEmpty())
        return {};

    // ARGB32 (not premultiplied) so the image carries straight, unmultiplied alpha — which is what
    // libvips reads back and what the compositor's source-over expects.
    QImage img(bounds.size().toSize(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    // The artifact is drawn in balloon coordinates, and a tail can reach above or left of the balloon,
    // so the buffer's origin is the bounds' origin rather than the balloon's.
    p.translate(-bounds.topLeft());
    paintArtifact(p, a);
    p.end();
    return img;
}

QSize fittedBox(const TextArtifact& a)
{
    if (a.text.isEmpty())
        return a.box;

    // Growing the box leaves the stroke and the shape's inset to pay for, so adding the shortfall once
    // falls short. Re-measure instead of deriving a closed form here: it converges in a pass or two and
    // cannot drift out of step with balloonRect() / textSafeArea() the way a duplicated formula would.
    TextArtifact probe = a;
    for (int pass = 0; pass < k_fitPasses; ++pass) {
        const QRectF safe = textSafeArea(probe, balloonRect(probe));
        if (safe.width() < 1)
            break;

        QTextDocument doc;
        layOutText(doc, probe, safe.width());

        const qreal extra = doc.size().height() - safe.height();
        if (extra <= 0.5)
            break;
        probe.box.setHeight(qRound(probe.box.height() + extra));
    }
    return probe.box;
}

QString artifactLabel(const TextArtifact& a)
{
    const QString first = a.text.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!first.isEmpty())
        return first.length() > 28 ? first.left(27) + QStringLiteral("…") : first;

    switch (a.shape) {
    case TextArtifact::Shape::Speech:  return QObject::tr("(speech bubble)");
    case TextArtifact::Shape::Shout:   return QObject::tr("(shout)");
    case TextArtifact::Shape::Caption: return QObject::tr("(caption)");
    case TextArtifact::Shape::None:    return QObject::tr("(text)");
    }
    return QObject::tr("(overlay)");
}
