#include "rowglyph.hpp"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include "artifactpainter.hpp"

namespace StripEdit {

namespace {

/**
 * @brief Starts a glyph: a transparent pixmap at the **screen's** density, ready for antialiased drawing.
 *
 * The painter still works in points, so everything drawn against @p px lands where it should — the extra
 * pixels only make the edges sharp. Drawing at a ratio of one and letting the view scale the result up is
 * what made the first version look soft.
 */
[[nodiscard]] QPixmap blank(int px, qreal dpr)
{
    QPixmap pm(QSize(px, px) * qMax(1.0, dpr));
    pm.setDevicePixelRatio(qMax(1.0, dpr));
    pm.fill(Qt::transparent);
    return pm;
}

//! The colour every glyph is drawn in — the palette's text colour, softened so a column of them is not a
//! column of black bars.
[[nodiscard]] QColor ink(const QPalette& pal)
{
    QColor c = pal.color(QPalette::Text);
    c.setAlpha(200);
    return c;
}

[[nodiscard]] QPen outlinePen(const QPalette& pal, qreal width)
{
    QPen pen(ink(pal), width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    return pen;
}

}  // namespace

QIcon objectGlyph(const Artifact& a, const QPalette& pal, int px, qreal dpr)
{
    QPixmap  pm = blank(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // No silhouette to borrow: lettering with no balloon around it says so with letters. Drawn as
    // **outlines** rather than as text — type at this size is at the mercy of hinting and of what the
    // font calls an advance, and a path is measured and scaled like any other shape.
    if (!a.hasSilhouette() || a.box.isEmpty()) {
        QFont f;
        f.setBold(true);
        f.setPixelSize(64);
        QPainterPath letters;
        letters.addText(0, 0, f, QStringLiteral("Aa"));
        const QRectF ext = letters.boundingRect();
        if (ext.isEmpty())
            return QIcon(pm);

        const qreal k = qMin((px - 4.0) / ext.width(), (px - 4.0) / ext.height());
        p.translate((px - ext.width() * k) / 2.0, (px - ext.height() * k) / 2.0);
        p.scale(k, k);
        p.translate(-ext.topLeft());
        p.setPen(Qt::NoPen);
        p.setBrush(ink(pal));
        p.drawPath(letters);
        return QIcon(pm);
    }

    const QPainterPath silhouette = artifactSilhouette(a);
    const QRectF       extent     = silhouette.boundingRect();
    if (silhouette.isEmpty() || extent.isEmpty())
        return {};

    // One scale for both axes, so a wide balloon stays wide: a glyph that lied about the proportions
    // would make two different objects look like the same one.
    const qreal margin = 1.5;   // room for the outline, which is drawn centred on the path
    const qreal k      = qMin((px - 2 * margin) / extent.width(), (px - 2 * margin) / extent.height());
    p.translate((px - extent.width() * k) / 2.0, (px - extent.height() * k) / 2.0);
    p.scale(k, k);
    p.translate(-extent.topLeft());

    // The object's own fill, the palette's outline. A black balloon on a dark panel would otherwise be a
    // hole, and a white one on a light panel nothing at all.
    p.setBrush(a.skin.fill);
    QPen pen = outlinePen(pal, 1.0);
    pen.setWidthF(qMax(0.5, 1.0 / k));   // one point on screen, whatever the balloon's own size
    p.setPen(pen);
    p.drawPath(silhouette);
    return QIcon(pm);
}

QIcon assetGlyph(const QPixmap& art, int px, qreal dpr)
{
    if (art.isNull())
        return {};
    const qreal ratio  = qMax(1.0, dpr);
    QPixmap     scaled = art.scaled(QSize(px, px) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(ratio);
    return QIcon(scaled);
}

QIcon tailGlyph(const QPalette& pal, int px, qreal dpr)
{
    QPixmap  pm = blank(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // What a tail *is*: a sliver that leaves the balloon wide and arrives somewhere narrow, with the bend
    // a drawn tail has. Filled rather than stroked, and asymmetric, so it cannot be mistaken for the
    // tree's own expander chevron sitting a few points to its left.
    const qreal  w = px;
    QPainterPath tail;
    tail.moveTo(w * 0.14, w * 0.16);
    tail.cubicTo(w * 0.24, w * 0.52, w * 0.46, w * 0.66, w * 0.86, w * 0.88);   // the outer edge, curving
    tail.cubicTo(w * 0.50, w * 0.58, w * 0.42, w * 0.40, w * 0.52, w * 0.16);   // and back up the inside
    tail.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(ink(pal));
    p.drawPath(tail);
    return QIcon(pm);
}

QIcon pageGlyph(const QPalette& pal, int px, qreal dpr)
{
    QPixmap  pm = blank(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(outlinePen(pal, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(px * 0.26, px * 0.16, px * 0.48, px * 0.68), px * 0.06, px * 0.06);
    return QIcon(pm);
}

QIcon stripGlyph(const QPalette& pal, int px, qreal dpr)
{
    QPixmap  pm = blank(px, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(outlinePen(pal, 1.2));
    p.setBrush(Qt::NoBrush);
    // The pages, stacked: what the strip is, and why it is pinned under everything else in the list.
    for (int i = 0; i < 3; ++i)
        p.drawRoundedRect(QRectF(px * 0.26, px * (0.14 + i * 0.26), px * 0.48, px * 0.20),
                          px * 0.05, px * 0.05);
    return QIcon(pm);
}

}  // namespace StripEdit
