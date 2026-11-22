#include "painter.hpp"
#include "styles.hpp"

#include <QHash>
#include <QLinearGradient>
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

//! Places a dot's stamp is drawn for, per device pixel and axis: a dot lands within 1/16 px of its own.
constexpr int k_stampPhases = 8;
//! The step between the diameters stamps are drawn for, in device pixels.
constexpr qreal k_stampSizeStep = 1.0 / 16.0;
//! How far a mark's width and height may differ, relatively, for it to be taken for a dot.
constexpr qreal k_roundness = 1e-3;

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

//! A round mark: its centre and radius.
struct Dot
{
    QPointF centre;
    qreal   radius = 0.0;
};

/**
 * \brief Sorts @p marks: the round ones (a subpath of four curves, as QPainterPath::addEllipse() makes a
 *        circle) into @p dots, the others into @p rest.
 */
void splitDots(const QPainterPath& marks, QList<Dot>& dots, QPainterPath& rest)
{
    constexpr int k_circleElements = 13;   // a move, then four curves of three elements each
    rest.setFillRule(marks.fillRule());
    int i = 0;
    while (i < marks.elementCount()) {
        int end = i + 1;
        while (end < marks.elementCount() && !marks.elementAt(end).isMoveTo())
            ++end;
        bool round = end - i == k_circleElements;
        for (int k = i + 1; round && k < end; k += 3)
            round = marks.elementAt(k).isCurveTo();
        if (round) {
            // The four curves' ends are the circle's extremes: right, bottom, left, top.
            const QPointF right = marks.elementAt(i), bottom = marks.elementAt(i + 3);
            const QPointF left = marks.elementAt(i + 6), top = marks.elementAt(i + 9);
            const qreal   wide = std::abs(right.x() - left.x()), tall = std::abs(bottom.y() - top.y());
            round = wide > 0.0 && std::abs(wide - tall) <= k_roundness * wide;
            if (round)
                dots.append({QPointF((left.x() + right.x()) / 2.0, (top.y() + bottom.y()) / 2.0), (wide + tall) / 4.0});
        }
        if (!round) {
            rest.moveTo(marks.elementAt(i));
            for (int k = i + 1; k < end; ++k) {
                const QPainterPath::Element e = marks.elementAt(k);
                if (e.isCurveTo()) {
                    rest.cubicTo(e, marks.elementAt(k + 1), marks.elementAt(k + 2));
                    k += 2;
                } else {
                    rest.lineTo(e);
                }
            }
        }
        i = end;
    }
}

/**
 * \brief Draws @p dots in @p colour with @p painter, whose transform only scales evenly and moves, as stamps.
 *
 * Qt's rasteriser fills thousands of small curved shapes slowly, smoothed or not (a fading halftone's
 * 16 000 dots: 45 ms at zoom 2, 28 ms unsmoothed). A dot is a disc, so each size (to k_stampSizeStep) at each
 * place within a pixel (to 1 / k_stampPhases) is drawn once, smoothed, into a small image, which is copied to
 * every dot of that size and place: about 1 ms for those dots, within a level or two of alpha of the filled
 * path. The stamps live for this call only.
 */
void stampDots(QPainter& painter, const QList<Dot>& dots, const QColor& colour)
{
    const QTransform       toPixels = painter.transform();
    const qreal            scale    = std::abs(toPixels.m11());
    QHash<quint64, QImage> stamps;
    painter.save();
    painter.resetTransform();
    for (const Dot& dot : dots) {
        const QPointF at     = toPixels.map(dot.centre);
        const qint64  qx     = qRound64(at.x() * k_stampPhases);
        const qint64  qy     = qRound64(at.y() * k_stampPhases);
        const int     px     = qFloor(qreal(qx) / k_stampPhases);
        const int     py     = qFloor(qreal(qy) / k_stampPhases);
        const int     phaseX = int(qx - qint64(px) * k_stampPhases);
        const int     phaseY = int(qy - qint64(py) * k_stampPhases);
        const int     size   = qRound(2.0 * dot.radius * scale / k_stampSizeStep);
        const quint64 key    = (quint64(size) << 16) | (quint64(phaseX) << 8) | quint64(phaseY);
        auto          it     = stamps.find(key);
        if (it == stamps.end()) {
            const qreal diameter = size * k_stampSizeStep;
            const int   side     = qCeil(diameter) + 2;   // a pixel either side for the smoothing and the place
            QImage      stamp(side, side, QImage::Format_ARGB32_Premultiplied);
            stamp.fill(Qt::transparent);
            QPainter s(&stamp);
            s.setRenderHint(QPainter::Antialiasing, true);
            s.setPen(Qt::NoPen);
            s.setBrush(colour);
            s.drawEllipse(QPointF(side / 2 + qreal(phaseX) / k_stampPhases, side / 2 + qreal(phaseY) / k_stampPhases),
                          diameter / 2.0, diameter / 2.0);
            s.end();
            it = stamps.insert(key, stamp);
        }
        painter.drawImage(QPoint(px - it->width() / 2, py - it->height() / 2), *it);
    }
    painter.restore();
}

/**
 * \brief @p path cut between its subpaths into pieces of about k_outlinePerFill of outline each, at @p scale
 *        device pixels to the unit, each wound by the nonzero rule. A cubic counts by its control polygon,
 *        which is longer than the curve: on the safe side.
 */
QList<QPainterPath> piecesOf(const QPainterPath& path, qreal scale)
{
    QList<QPainterPath> pieces;
    QPainterPath        piece;
    piece.setFillRule(Qt::WindingFill);   // marks overlap where they cross, and must not cancel there
    qreal      outline = 0.0;
    QPointF    last;
    const auto walk = [&](const QPointF& to) {
        outline += QLineF(last, to).length() * scale;
        last = to;
    };
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            if (outline > k_outlinePerFill) {
                pieces.append(piece);
                piece = QPainterPath();
                piece.setFillRule(Qt::WindingFill);
                outline = 0.0;
            }
            piece.moveTo(e);
            last = e;
        } else if (e.isLineTo()) {
            piece.lineTo(e);
            walk(e);
        } else if (e.isCurveTo()) {
            const QPointF c2 = path.elementAt(i + 1), end = path.elementAt(i + 2);
            piece.cubicTo(e, c2, end);
            walk(e);
            walk(c2);
            walk(end);
            i += 2;
        }
    }
    if (!piece.isEmpty())
        pieces.append(piece);
    return pieces;
}

/**
 * \brief Fills @p path's marks (many small subpaths) with @p colour, in pieces, through a layer of its own.
 *
 * Pieces overlap where marks do (a grid's crossings, dots large enough to merge), and a transparent colour
 * filled piece by piece would darken there. So they are filled opaque into a layer (drawInLayer()), which
 * is drawn once with the colour's alpha: the marks paint as one, as they do in the SVG. Dots are stamped
 * (stampDots()) where the layer is neither turned nor stretched; the other marks are filled.
 */
void fillMarks(QPainter& painter, const QPainterPath& path, const QColor& colour)
{
    drawInLayer(painter, path.boundingRect(), colour.alphaF(), [&](QPainter& p, const QTransform& toPixels) {
        QColor opaque = colour;
        opaque.setAlpha(255);
        const QTransform& t     = p.transform();
        const bool        plain = t.type() <= QTransform::TxScale && qFuzzyCompare(std::abs(t.m11()), std::abs(t.m22()));
        QList<Dot>        dots;
        QPainterPath      rest;
        if (plain) {
            splitDots(path, dots, rest);
            stampDots(p, dots, opaque);
        }
        for (const QPainterPath& piece : piecesOf(plain ? rest : path, std::sqrt(std::abs(toPixels.determinant()))))
            p.fillPath(piece, opaque);
    });
}

/**
 * \brief Strokes @p path with @p pen, in @p ink, a few subpaths at a time where it has many (lettering's
 *        outline round each glyph's contours).
 *
 * As a fill of many small shapes, a stroke round them is slow as one path: its outline is filled whole
 * (137 letters' outline: 120 ms at zoom 2, 16 ms in pieces). Each contour's stroke is its own, so cutting
 * between them changes nothing drawn; where strokes of two pieces overlap, a transparent colour would
 * darken, so the pieces go through a layer, opaque, given the colour's alpha once. A pattern's stroke is
 * drawn whole.
 */
void strokeInPieces(QPainter& painter, const QPainterPath& path, const QPen& pen, const Render::Ink& ink)
{
    const QList<QPainterPath> pieces =
        piecesOf(path, std::sqrt(std::abs(painter.combinedTransform().determinant())));
    if (pieces.size() < 2 || ink.paint.kind != Paint::Kind::Solid) {
        painter.strokePath(path, pen);
        return;
    }
    const qreal half = pen.widthF() / 2.0;
    drawInLayer(painter, path.boundingRect().adjusted(-half, -half, half, half), ink.paint.colour.alphaF(),
                [&](QPainter& p, const QTransform&) {
                    QColor opaque = ink.paint.colour;
                    opaque.setAlpha(255);
                    QPen solid = pen;
                    solid.setColor(opaque);
                    for (const QPainterPath& piece : pieces)
                        p.strokePath(piece, solid);
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
        strokeInPieces(painter, i.path, pen, *i.stroke);
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

Paths resolve(const Composition& c)
{
    Paths p;
    if (c.balloon) {
        p.silhouette = Geometry::silhouette(*c.balloon, c.seed);
        p.balloon    = Render::balloon(*c.balloon, c.seed, p.silhouette);
    }
    p.texts = Geometry::textOutlines(c);
    p.words = Render::lettering(c, p.texts, Geometry::textFillsOf(c));
    return p;
}

void paint(QPainter& painter, const Composition& c)
{
    paintPaths(painter, c, resolve(c));
}

void paintPaths(QPainter& painter, const Composition& c, const Paths& paths)
{
    if (frameOf(c).isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A pattern's tile is scaled and turned: sampled smoothly, its dots keep the edges librsvg gives them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    play(painter, paths.balloon.items);
    play(painter, paths.words.items);
    painter.restore();
}

QImage render(const Composition& c)
{
    if (frameOf(c).isEmpty())
        return {};

    const QRectF bounds = Geometry::bounds(c);
    if (bounds.isEmpty())
        return {};

    // ARGB32 (not premultiplied) so the image carries straight, unmultiplied alpha — which is what
    // libvips reads back and what the compositor's source-over expects.
    QImage img(bounds.size().toSize(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    // The composition is drawn in balloon coordinates, and a tail can reach above or left of the balloon,
    // so the buffer's origin is the bounds' origin rather than the balloon's.
    p.translate(-bounds.topLeft());
    paintCut(p, c, bounds, [&](QPainter& q) { paint(q, c); });
    p.end();
    return img;
}

void paintCut(QPainter& painter, const Composition& c, const QRectF& bounds,
              const std::function<void(QPainter&)>& draw)
{
    const QPainterPath region = c.mask ? Geometry::maskOutline(*c.mask, c.seed) : QPainterPath();
    if (region.isEmpty()) {
        draw(painter);
        return;
    }
    drawInLayer(painter, bounds, 1.0, [&](QPainter& p, const QTransform&) {
        draw(p);
        // What the region hides, as alpha, in an image the layer's size: up to the fade's end less the pattern's marks,
        // or along the fade's gradient, or whole; as the SVG's black and white.
        QImage   cover(p.device()->width(), p.device()->height(), QImage::Format_ARGB32_Premultiplied);
        cover.fill(Qt::transparent);
        QPainter q(&cover);
        q.setRenderHint(QPainter::Antialiasing, true);
        q.setTransform(p.transform());
        if (const Geometry::MaskMarks marks = Geometry::maskMarks(*c.mask, c.seed); !marks.hidden.isEmpty()) {
            q.fillPath(marks.hidden, Qt::black);
            q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            fillMarks(q, marks.shown, Qt::black);   // many small marks: stamped, or filled in pieces
        } else if (const auto fade = Geometry::maskFade(*c.mask)) {
            QLinearGradient g(fade->p1(), fade->p2());
            g.setColorAt(0.0, Qt::black);
            g.setColorAt(1.0, Qt::transparent);
            q.fillPath(region, g);
        } else {
            q.fillPath(region, Qt::black);
        }
        q.end();
        // Taken away from the object; or, kept inside, the object kept only where it covers: the whole image is
        // drawn, so what lies outside the region goes too.
        p.save();
        p.resetTransform();
        p.setCompositionMode(c.mask->keepInside ? QPainter::CompositionMode_DestinationIn
                                                : QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, cover);
        p.restore();
    });
}

} // namespace Painter
