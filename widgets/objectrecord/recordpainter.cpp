#include "recordpainter.hpp"
#include "styles.hpp"

#include <QPainter>
#include <QPen>
#include <QtMath>

#include <cmath>
#include <functional>

namespace Painter {

namespace {

/**
 *  How many tile pixels a balloon pixel of a pattern gets: enough that the dots stay round when the strip
 *  is zoomed in, since the tile is drawn once and scaled.
 */
constexpr int k_tileOversample = 4;

/**
 *  How much outline one fill takes, in device pixels. Qt's antialiased rasteriser slows down sharply on a
 *  single path with a great deal of outline over a large area (a 600 × 400 halftone at a 4 px spacing:
 *  150 to 250 ms a paint); in pieces of this much, 10 to 20 ms. Measured: smaller gains nothing more.
 */
constexpr qreal k_outlinePerFill = 512.0;

/**
 * \brief Runs @p draw on a layer of its own at device resolution, covering @p bounds (in the painter's
 *        coordinates), and draws the layer once at @p opacity.
 *
 * What is drawn into it opaque and overlaps itself (a grid's crossings, a shadow's body and its band)
 * then paints as one, as it does in an SVG group with an opacity.
 */
void drawInLayer(QPainter& painter, const QRectF& bounds, qreal opacity,
                 const std::function<void(QPainter&, const QTransform& toPixels)>& draw)
{
    const QPaintDevice* device = painter.device();
    const qreal         dpr    = device->devicePixelRatio();
    // The device's extent bounds the layer when zoomed far in (an image's width is in pixels already, so
    // there it is generous; a bound only).
    const QRect         screen(0, 0, qCeil(device->width() * dpr), qCeil(device->height() * dpr));
    // To the layer's pixels: the painter's transform with the pixel ratio, which is where resetTransform()
    // leaves drawImage(). Not deviceTransform(): on a widget that also holds the widget's place in its
    // window, which Qt adds again on drawing, so the layer landed that far off (seen on the strip).
    const QTransform    toPixels = painter.combinedTransform();
    const QRect         area     = toPixels.mapRect(bounds).toAlignedRect().intersected(screen);
    if (area.isEmpty())
        return;

    QImage layer(area.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter p(&layer);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setTransform(toPixels * QTransform::fromTranslate(-area.left(), -area.top()));
        draw(p, toPixels);
    }
    layer.setDevicePixelRatio(dpr);
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(QPointF(area.topLeft()) / dpr, layer);
    painter.restore();
}

/**
 * \brief Fills @p marks (many small subpaths) with @p colour, in pieces, through a layer of its own.
 *
 * Pieces overlap where marks do (a grid's crossings, dots large enough to merge), and a transparent colour
 * filled piece by piece would darken there. So they are filled opaque into a layer (drawInLayer()), which
 * is drawn once with the colour's alpha: the marks paint as one, as they do in the SVG.
 */
void fillMarks(QPainter& painter, const QPainterPath& marks, const QColor& colour)
{
    drawInLayer(painter, marks.boundingRect(), colour.alphaF(), [&](QPainter& p, const QTransform& toPixels) {
        QColor opaque = colour;
        opaque.setAlpha(255);
        // A piece ends at a mark's start once it holds enough outline (a cubic counted by its control
        // polygon, which is longer than the curve: on the safe side).
        const qreal  scale = std::sqrt(std::abs(toPixels.determinant()));
        QPainterPath piece;
        qreal        outline = 0.0;
        QPointF      last;
        const auto   walk = [&](const QPointF& to) {
            outline += QLineF(last, to).length() * scale;
            last = to;
        };
        for (int i = 0; i < marks.elementCount(); ++i) {
            const QPainterPath::Element e = marks.elementAt(i);
            if (e.isMoveTo()) {
                if (outline > k_outlinePerFill) {
                    p.fillPath(piece, opaque);
                    piece   = QPainterPath();
                    outline = 0.0;
                }
                piece.moveTo(e);
                last = e;
            } else if (e.isLineTo()) {
                piece.lineTo(e);
                walk(e);
            } else if (e.isCurveTo()) {
                const QPointF c2 = marks.elementAt(i + 1), end = marks.elementAt(i + 2);
                piece.cubicTo(e, c2, end);
                walk(e);
                walk(c2);
                walk(end);
                i += 2;
            }
        }
        p.fillPath(piece, opaque);
    });
}

//! What @p ink paints with: its Paint's pattern, or its colour when it names no pattern (a fading fill's ground).
QBrush brushOf(const Render::Ink& ink)
{
    return ink.patternId.isEmpty() ? QBrush(ink.paint.colour) : paintBrush(ink.paint);
}

//! What @p items cover, strokes included: a layer's extent.
QRectF extentOf(const QList<Render::Item>& items)
{
    QRectF r;
    for (const Render::Item& i : items) {
        if (i.kind == Render::Item::Kind::Group) {
            r = r.united(extentOf(i.children));
        } else if (i.kind == Render::Item::Kind::Path) {
            const qreal half = i.stroke ? i.strokeWidth / 2.0 : 0.0;
            r = r.united(i.path.boundingRect().adjusted(-half, -half, half, half));
        }
    }
    return r;
}

void playItem(QPainter& painter, const Render::Item& i)
{
    switch (i.kind) {
    case Render::Item::Kind::Image:
        return;   // the object that shows a picture draws it itself
    case Render::Item::Kind::Group:
        if (i.layer) {
            drawInLayer(painter, extentOf(i.children), i.opacity,
                        [&](QPainter& p, const QTransform&) { play(p, i.children); });
        } else if (!i.mask.isEmpty()) {
            for (const Render::Item& child : i.children) {
                if (!child.marks) {
                    playItem(painter, child);
                    continue;
                }
                painter.save();
                painter.setClipPath(i.mask, Qt::IntersectClip);
                playItem(painter, child);
                painter.restore();
            }
        } else {
            play(painter, i.children);
        }
        return;
    case Render::Item::Kind::Path:
        break;
    }
    if (i.filteredOnly)
        return;
    if (!i.clip.isEmpty()) {
        painter.save();
        painter.setClipPath(i.clip, Qt::IntersectClip);
    }
    if (i.fill) {
        if (i.marks)
            fillMarks(painter, i.path, i.fill->paint.colour);
        else
            painter.fillPath(i.path, brushOf(*i.fill));
    }
    if (i.stroke && i.strokeWidth > 0.0) {
        QPen pen(brushOf(*i.stroke), i.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        if (i.roundCap)
            pen.setCapStyle(Qt::RoundCap);
        painter.strokePath(i.path, pen);
    }
    if (!i.clip.isEmpty())
        painter.restore();
}

} // namespace

void play(QPainter& painter, const QList<Render::Item>& items)
{
    for (const Render::Item& i : items)
        playItem(painter, i);
}

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

Paths resolve(const ObjectRecord& a)
{
    Paths p;
    p.silhouette = Geometry::silhouette(a);
    p.text       = Geometry::textOutline(a);
    p.balloon    = Render::balloon(a, p.silhouette);
    p.words      = Render::lettering(a, p.text, Geometry::textShadowOf(a, p.text));
    return p;
}

void paint(QPainter& painter, const ObjectRecord& a)
{
    paintPaths(painter, a, resolve(a));
}

void paintPaths(QPainter& painter, const ObjectRecord& a, const Paths& paths)
{
    if (a.box.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A pattern's tile is scaled and turned: sampled smoothly, its dots keep the edges librsvg gives them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    play(painter, paths.balloon.items);
    play(painter, paths.words.items);
    painter.restore();
}

void paintLettering(QPainter& painter, const ObjectRecord& a, const QPainterPath& text,
                    const Geometry::ShadowPaths& shadow)
{
    // Filled outlines rather than drawn text, so that what is on screen is what Svg::write() writes
    // and what the library rasterises — one geometry, three consumers.
    play(painter, Render::lettering(a, text, shadow).items);
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
