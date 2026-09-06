#include "artifactpainter.h"

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>

namespace {

//! Fraction of the box height reserved below the balloon for its tail.
constexpr qreal k_tailAllowance = 0.18;
//! Half-width of the tail's base, as a fraction of the balloon's width. The base is hidden inside the
//! silhouette, so what shows is the taper below it — about half this at the point it emerges.
constexpr qreal k_tailBaseHalf  = 0.16;
//! Points on a "shout" burst — 12 spikes reads as a shout without turning into a sunburst.
constexpr int   k_burstSpikes   = 12;
//! Inner radius of the burst, as a fraction of the outer one.
constexpr qreal k_burstInner    = 0.74;
//! Re-measure passes in fittedBox(); it converges in two or three, this is headroom.
constexpr int   k_fitPasses     = 6;

/**
 * @brief The balloon's own rectangle inside the box — the box less the stroke and the tail allowance.
 *
 * The tail lives *inside* the artifact's box rather than hanging off it, which is what keeps the
 * bitmap, the box the author drags and the library's placement all one rectangle. It costs a little
 * reach: a tail cannot point past the box, so a speaker far outside it needs a bigger box.
 * ponytail: bottom-edge tails only, which is nearly every comic bubble. A tail leaving another edge
 * means computing the nearest edge and rotating the base segment — worth doing when someone asks.
 */
QRectF balloonRect(const TextArtifact& a)
{
    const qreal  sw = a.strokeWidth / 2.0;
    QRectF body(0, 0, a.box.width(), a.box.height());
    body.adjust(sw, sw, -sw, -sw);
    if (a.hasTail())
        body.setBottom(body.bottom() - a.box.height() * k_tailAllowance);
    return body.normalized();
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
 * @brief Triangle reaching from deep inside the balloon out to the tail tip.
 *
 * The base sits at the body's vertical **centre**, not on its bottom edge, so the triangle is certain to
 * overlap the silhouette whatever shape it is. That matters for the burst: its outline is a star
 * inscribed in the body rect, so at the tail's x the shape's boundary can be far above the rect's bottom
 * — a tail based on that edge comes out as a separate triangle floating under the shape. Everything
 * inside the silhouette disappears into the union anyway, and the taper reads as a proper tail from
 * wherever it emerges, which is the point: this needs no idea of where the outline actually is.
 */
QPainterPath tailPath(const TextArtifact& a, const QRectF& body)
{
    const qreal baseY = body.center().y();
    const QPointF tip(qBound(body.left(), qreal(a.tail.x()), body.right()),
                      qBound(body.bottom(), qreal(a.tail.y()), qreal(a.box.height()) - a.strokeWidth / 2.0));

    // k_tailBaseHalf is the half-width where the tail *emerges*, which is the only part anyone sees.
    // The base is hidden inside the balloon, so widen it by the taper ratio to land on that width at the
    // edge — otherwise the tail's thickness would silently depend on the box's proportions.
    // Clamped to leave room on both sides: qBound() below is undefined, not merely wide, if its minimum
    // ends up above its maximum — which a very narrow balloon would otherwise produce.
    const qreal half = qMin(qMax(4.0, body.width() * k_tailBaseHalf),
                            qMax(1.0, body.width() / 2.0 - 1.0));
    const qreal cx   = qBound(body.left() + half, qreal(a.tail.x()), body.right() - half);

    QPainterPath p;
    p.moveTo(cx - half, baseY);
    p.lineTo(cx + half, baseY);
    p.lineTo(tip);
    p.closeSubpath();
    return p;
}

//! The rectangle text may occupy — inset from the silhouette so letters do not touch the stroke.
QRectF textSafeArea(const TextArtifact& a, const QRectF& body)
{
    if (a.shape == TextArtifact::Shape::None)
        return QRectF(0, 0, a.box.width(), a.box.height());

    // A star's usable interior is its *inner* radius, not its bounding box — inset accordingly, or the
    // text runs out between the spikes.
    if (a.shape == TextArtifact::Shape::Shout) {
        const qreal kx = body.width()  * (1.0 - k_burstInner * 0.92) / 2.0;
        const qreal ky = body.height() * (1.0 - k_burstInner * 0.92) / 2.0;
        return body.adjusted(kx, ky, -kx, -ky);
    }

    const qreal inset = a.strokeWidth + (a.shape == TextArtifact::Shape::Speech ? 14.0 : 8.0);
    return body.adjusted(inset, inset, -inset, -inset);
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

void paintArtifact(QPainter& painter, const TextArtifact& a)
{
    if (a.box.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF body = balloonRect(a);

    if (a.shape != TextArtifact::Shape::None && !body.isEmpty()) {
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
        case TextArtifact::Shape::None:
            break;
        }
        if (a.hasTail())
            path = path.united(tailPath(a, body));

        painter.fillPath(path, a.fill);
        if (a.strokeWidth > 0) {
            QPen pen(a.stroke, a.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(path, pen);
        }
    }

    if (!a.text.isEmpty()) {
        const QRectF safe = textSafeArea(a, body);
        if (safe.width() > 1 && safe.height() > 1) {
            QTextDocument doc;
            layOutText(doc, a, safe.width());

            // Vertically centred in the safe area — a bubble's text sits in the middle of the balloon,
            // never pinned to its top edge. Clipped so an overlong string cannot bleed past the stroke.
            const qreal h = doc.size().height();
            painter.setClipRect(safe);
            painter.translate(safe.left(), safe.top() + qMax(qreal(0), (safe.height() - h) / 2.0));

            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.palette.setColor(QPalette::Text, a.textColour);
            doc.documentLayout()->draw(&painter, ctx);
        }
    }

    painter.restore();
}

QImage renderArtifact(const TextArtifact& a)
{
    if (a.box.isEmpty())
        return {};

    // ARGB32 (not premultiplied) so the saved PNG carries straight, unmultiplied alpha — which is what
    // libvips reads back and what the compositor's source-over expects.
    QImage img(a.box, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    paintArtifact(p, a);
    p.end();
    return img;
}

QSize fittedBox(const TextArtifact& a)
{
    if (a.text.isEmpty())
        return a.box;

    // Growing the box also grows the tail allowance (a fraction of the height) and leaves the stroke and
    // inset to pay for, so adding the shortfall once always falls short. Re-measure instead of deriving
    // a closed form here: it converges in a couple of passes and cannot drift out of step with
    // balloonRect() / textSafeArea() the way a duplicated formula would.
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
