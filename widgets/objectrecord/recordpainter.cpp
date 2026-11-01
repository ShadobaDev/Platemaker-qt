#include "recordpainter.hpp"
#include "styles.hpp"

#include <QPainter>
#include <QPen>
#include <QtMath>

namespace Painter {

namespace {

/**
 *  How many tile pixels a balloon pixel of a pattern gets: enough that the dots stay round when the strip
 *  is zoomed in, since the tile is drawn once and scaled.
 */
constexpr int k_tileOversample = 4;

} // namespace

QBrush paintBrush(const Paint& paint)
{
    if (paint.kind == Paint::Kind::Solid)
        return paint.colour;

    const Geometry::PatternCell cell = Geometry::patternCell(paint);
    const int                   side = qMax(1, qCeil(cell.size * k_tileOversample));
    QImage                      tile(side, side, QImage::Format_ARGB32_Premultiplied);
    tile.fill(paint.colour);
    {
        QPainter p(&tile);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(side / cell.size, side / cell.size);
        p.fillPath(cell.marks, paint.second);
    }
    // Back to balloon pixels, then turned: the same transform the SVG's pattern carries.
    QBrush     brush(tile);
    QTransform t;
    t.rotate(cell.angle);
    t.scale(cell.size / side, cell.size / side);
    brush.setTransform(t);
    return brush;
}

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
    // A pattern's tile is scaled and turned: sampled smoothly, its dots keep the edges librsvg gives them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!silhouette.isEmpty()) {
        // The shadow under it: hard here, whatever its softness — a soft one is the library's to draw,
        // and a balloon with one is previewed from there at rest (Geometry::hasFilter()).
        if (const QPainterPath shadow = Geometry::shadowOf(a, silhouette); !shadow.isEmpty())
            painter.fillPath(shadow, paintBrush(a.effects.shadowFill));
        painter.fillPath(silhouette, paintBrush(a.skin.fill));
        // A style with strokes of its own draws them instead of the outline: they are the edge.
        if (a.style.strokeWidth > 0 && Geometry::hasOutline(a)) {
            QPen pen(paintBrush(a.style.stroke), a.style.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(silhouette, pen);
        }
    }
    if (!ink.isEmpty())
        painter.fillPath(ink, paintBrush(a.style.stroke));

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
