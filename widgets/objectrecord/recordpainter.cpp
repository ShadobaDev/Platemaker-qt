#include "recordpainter.hpp"
#include "styles.hpp"

#include <QPainter>
#include <QPen>

namespace Painter {

void paint(QPainter& painter, const ObjectRecord& a)
{
    const QPainterPath silhouette = Geometry::silhouette(a);
    paintPaths(painter, a, silhouette, Geometry::ink(a, silhouette), Geometry::textOutline(a));
}

void paintPaths(QPainter& painter, const ObjectRecord& a, const QPainterPath& silhouette,
                const QPainterPath& ink, const QPainterPath& text)
{
    if (a.box.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!silhouette.isEmpty()) {
        painter.fillPath(silhouette, a.skin.fill);
        // A style with strokes of its own draws them instead of the outline: they are the edge.
        if (a.skin.strokeWidth > 0 && Geometry::hasOutline(a)) {
            QPen pen(a.skin.stroke, a.skin.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(silhouette, pen);
        }
    }
    if (!ink.isEmpty())
        painter.fillPath(ink, a.skin.stroke);

    // Filled outlines rather than drawn text, so that what is on screen is what Svg::write() writes
    // and what the library rasterises — one geometry, three consumers.
    if (!text.isEmpty())
        painter.fillPath(text, a.text.colour);

    painter.restore();
}

QImage render(const ObjectRecord& a)
{
    if (a.box.isEmpty())
        return {};

    const QRectF bounds = Geometry::bounds(a);
    if (bounds.isEmpty())
        return {};

    // ARGB32 (not premultiplied) so the image carries straight, unmultiplied alpha — which is what
    // libvips reads back and what the compositor's source-over expects.
    QImage img(bounds.size().toSize(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    // The record is drawn in balloon coordinates, and a tail can reach above or left of the balloon,
    // so the buffer's origin is the bounds' origin rather than the balloon's.
    p.translate(-bounds.topLeft());
    paint(p, a);
    p.end();
    return img;
}

} // namespace Painter
