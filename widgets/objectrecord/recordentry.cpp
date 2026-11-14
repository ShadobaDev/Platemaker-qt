// The record's way in to the geometry, the render list, the painter and the SVG, until the record goes: each
// form is the composition's of compositionOf(), its balloon's and its words'. Whatever is still written
// against a record (the editor, the project) reaches the drawing through here, and only through here.

#include "composition.hpp"
#include "geometry.hpp"
#include "recordpainter.hpp"
#include "recordsvg.hpp"
#include "shapes.hpp"
#include "styles.hpp"

namespace Geometry {

QRectF balloonRect(const ObjectRecord& a)
{
    // The record's box less half its line, whatever the record is: what the editor's grips are laid on.
    const qreal sw = a.style.strokeWidth / 2.0;
    QRectF      body(0, 0, a.box.width(), a.box.height());
    body.adjust(sw, sw, -sw, -sw);
    return body.normalized();
}

QPainterPath outlineOf(const ObjectRecord& a, const QRectF& body)
{
    const Composition c = compositionOf(a);
    return c.balloon ? outlineOf(c.balloon->bodies.first(), body, c.seed) : QPainterPath();
}

QPainterPath silhouette(const ObjectRecord& a)
{
    const Composition c = compositionOf(a);
    return c.balloon ? silhouette(*c.balloon, c.seed) : QPainterPath();
}

OutlineModel outlineModel(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const Composition c = compositionOf(a);
    if (!c.balloon) {
        OutlineModel m;
        m.joined = silhouette;
        return m;
    }
    return outlineModel(*c.balloon, c.seed, silhouette);
}

QList<bool> tailsDrawn(const ObjectRecord& a)
{
    const Composition c = compositionOf(a);
    return c.balloon ? tailsDrawn(*c.balloon, c.seed) : QList<bool>(a.tails.items.size(), false);
}

Part partAt(const ObjectRecord& a, const QPointF& local, qreal slack)
{
    return partAt(compositionOf(a), local, slack);
}

QRectF bounds(const ObjectRecord& a)
{
    return bounds(compositionOf(a));
}

QRectF boundsOf(const ObjectRecord& a, const QPainterPath& silhouette, const QPainterPath& text)
{
    return boundsOf(compositionOf(a), silhouette, text);
}

QPainterPath textOutline(const ObjectRecord& a)
{
    const Composition c = compositionOf(a);
    return textOutline(c, c.words.first());
}

ShadowPaths textShadowOf(const ObjectRecord& a, const QPainterPath& text)
{
    return textShadowOf(compositionOf(a).words.first(), text);
}

QString fontFallback(const ObjectRecord& a)
{
    return fontFallback(compositionOf(a).words.first());
}

QRectF textArea(const ObjectRecord& a)
{
    const Composition c = compositionOf(a);
    return textArea(c, c.words.first());
}

// A style's questions are its groups', whatever the record is: the editor asks them of a selection's look.
QPainterPath fillTone(const ObjectRecord& a, const QPainterPath& silhouette)
{
    Balloon b;
    b.fill = a.skin;
    b.line = a.style;
    b.bodies.append({0, {}, a.box, a.shape});
    return fillTone(b, silhouette);
}

bool hasFilter(const ObjectRecord& a)
{
    return hasFilter(compositionOf(a));
}

bool fadesShadow(const ObjectRecord& a)
{
    return fadesShadow(a.effects.shadow, a.effects.shadowFill);
}

ShadowPaths shadowOf(const ObjectRecord& a, const QPainterPath& silhouette)
{
    return shadowOf(a.effects.shadow, a.effects.shadowFill, silhouette);
}

Paint shadowPaint(const ObjectRecord& a)
{
    return shadowPaint(a.effects.shadow, a.effects.shadowFill);
}

QPainterPath ink(const ObjectRecord& a, const QPainterPath& silhouette)
{
    const Composition c = compositionOf(a);
    return c.balloon ? ink(*c.balloon, c.seed, silhouette) : QPainterPath();
}

bool hasOutline(const ObjectRecord& a)
{
    return lineInfo(a.style.line).ink == nullptr;
}

}  // namespace Geometry

namespace Painter {

Paths resolve(const ObjectRecord& a)
{
    return resolve(compositionOf(a));
}

void paintPaths(QPainter& painter, const ObjectRecord& a, const Paths& paths)
{
    paintPaths(painter, compositionOf(a), paths);
}

void paintLettering(QPainter& painter, const ObjectRecord& a, const QPainterPath& text,
                    const Geometry::ShadowPaths& shadow)
{
    paintLettering(painter, compositionOf(a).words.first(), text, shadow);
}

QImage render(const ObjectRecord& a)
{
    return render(compositionOf(a));
}

}  // namespace Painter

namespace Svg {

QByteArray write(const ObjectRecord& a, const QByteArray& picture, const QString& mime)
{
    return write(compositionOf(a), picture, mime);
}

QString contentOf(const ObjectRecord& a)
{
    return contentOf(compositionOf(a));
}

}  // namespace Svg
