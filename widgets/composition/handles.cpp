// A composition's handles: each element's own, with the drag that changes that element.

#include "handles.hpp"

#include "composition.hpp"
#include "geometry.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QtMath>

namespace Geometry {

namespace {

//! How far a node may sit from the box's edge, as a fraction of it, and the box still count as fitted.
constexpr qreal k_fitsBox = 1e-9;

//! The body @p h belongs to.
Body* bodyFor(Composition& c, const Handle& h)
{
    if (!c.balloon)
        return nullptr;
    for (Body& b : c.balloon->bodies)
        if (b.id == h.element)
            return &b;
    return nullptr;
}

/**
 * \brief Fits @p own to its outline (outlineExtent()), in the units of @p rect, its rectangle: it grows to a
 *        node pulled past it, and shrinks back when the outline comes in again.
 *
 * The nodes are re-measured against the fitted body, and the tail tips move back by as much as the box's
 * corner moved, so nothing moves on the strip but the node being dragged. A box that fits its outline
 * already, or an outline with no width or height, leave it as it is.
 *
 * \return How far the box's top-left moved, in the units it was in.
 */
QPointF fitToOutline(Composition& c, Body& own, const QRectF& rect)
{
    const QRectF extent = outlineExtent(own.shape, rect);
    if (extent.isNull())
        return {};
    const qreal left = extent.left(), top = extent.top(), right = extent.right(), bottom = extent.bottom();
    const bool fits = qAbs(left) <= k_fitsBox && qAbs(top) <= k_fitsBox && qAbs(right - 1.0) <= k_fitsBox
                     && qAbs(bottom - 1.0) <= k_fitsBox;
    if (fits || (right - left) * rect.width() < 1.0 || (bottom - top) * rect.height() < 1.0)
        return {};

    // The fitted body, and the box around it in whole pixels, the stroke's half outside it as
    // balloonRect() takes it off.
    const qreal  inset = c.balloon->style.line.strokeWidth / 2.0;
    const QRectF fitted(rect.left() + left * rect.width(), rect.top() + top * rect.height(),
                       (right - left) * rect.width(), (bottom - top) * rect.height());
    const QPointF corner(fitted.left() - inset, fitted.top() - inset);
    own.size = QSize(qCeil(fitted.width() + 2 * inset), qCeil(fitted.height() + 2 * inset));

    // A handle is an offset from its node, so it only rescales.
    const QRectF  next = balloonRect(*c.balloon, own);
    const QPointF scale(rect.width() / next.width(), rect.height() / next.height());
    for (ShapeNode& n : own.shape.nodes) {
        const QPointF at(rect.left() + n.at.x() * rect.width() - corner.x(),
                         rect.top() + n.at.y() * rect.height() - corner.y());
        n.at  = QPointF((at.x() - next.left()) / next.width(), (at.y() - next.top()) / next.height());
        n.in  = QPointF(n.in.x() * scale.x(), n.in.y() * scale.y());
        n.out = QPointF(n.out.x() * scale.x(), n.out.y() * scale.y());
    }
    for (BalloonTail& t : c.balloon->tails)
        t.tail.tip -= corner;
    return corner;
}

//! A body's outline and edge: several picked nodes move together; a node pulled past the box grows it.
QPointF dragBody(Composition& next, const Handle& h, const QPointF& to, const HandleContext& context)
{
    Body* own = bodyFor(next, h);
    if (!own)
        return {};
    const QRectF rect = balloonRect(*next.balloon, *own);
    const bool   node = partOf<ShapePart>(h) == ShapePart::Vertex;
    if (node && context.picked.size() > 1)
        own->shape = withNodesMoved(own->shape, rect, context.picked, to - h.at);
    else
        own->shape = dragShapeHandle(own->shape, rect, h, to, context.clearance);
    return node ? fitToOutline(next, *own, rect) : QPointF();
}

//! A "+" adds its node at once, and the drag that follows is that node's.
bool addNode(Composition& c, Handle& h)
{
    Body* own = bodyFor(c, h);
    if (!own)
        return false;
    own->shape = withVertexAdded(own->shape, h.index);
    h.part     = int(ShapePart::Vertex);
    h.glyph    = Handle::Glyph::Node;
    h.index    = h.index + 1;
    h.node     = h.index;
    h.press    = nullptr;
    return true;
}

//! The balloon's fill: the fade's cross, over its first body's rectangle.
QPointF dragFill(Composition& next, const Handle& h, const QPointF& to, const HandleContext&)
{
    if (const Body* own = bodyOf(next))
        next.balloon->style.fill.fill = dragFadeHandle(next.balloon->style.fill.fill,
                                                       balloonRect(*next.balloon, *own), h, to);
    return {};
}

//! The words @p h belongs to.
Words* wordsFor(Composition& c, const Handle& h)
{
    for (Words& w : c.words)
        if (w.id == h.element)
            return &w;
    return nullptr;
}

//! The words' text area.
QPointF dragWords(Composition& next, const Handle& h, const QPointF& to, const HandleContext&)
{
    if (Words* w = wordsFor(next, h))
        *w = dragTextAreaHandle(next, *w, h, to);
    return {};
}

//! The words' frame put back: an edge made automatic again, or by its middle, the frame centred in the body.
bool resetWords(Composition& c, const Handle& h)
{
    Words* w = wordsFor(c, h);
    if (!w)
        return false;
    const Words was = *w;
    switch (partOf<TextAreaPart>(h)) {
    case TextAreaPart::Side:
        (h.index == 0 ? w->frame.left : w->frame.right).reset();
        break;
    case TextAreaPart::Top:
        w->frame.top.reset();
        break;
    case TextAreaPart::Bottom:
        w->frame.bottom.reset();
        break;
    case TextAreaPart::Anchor: {
        // The frame back in the middle of the body, its size kept: each axis's two insets shared out evenly, in
        // whole pixels. An axis with both edges automatic is in the middle already.
        const auto centre = [](std::optional<qreal>& a, std::optional<qreal>& b) {
            if (!a && !b)
                return;
            const qreal both = a.value_or(0.0) + b.value_or(0.0);
            a = qreal(qFloor(both / 2.0));
            b = both - *a;
        };
        centre(w->frame.left, w->frame.right);
        centre(w->frame.top, w->frame.bottom);
        break;
    }
    }
    return *w != was;
}

//! @p list with each handle's element and drag.
QList<Handle> owned(QList<Handle> list, int element, Handle::Drag drag)
{
    for (Handle& h : list) {
        h.element = element;
        h.drag    = drag;
    }
    return list;
}

} // namespace

QList<Handle> handlesOf(const Composition& c, const HandleContext& context)
{
    QList<Handle> out;
    if (const Body* own = bodyOf(c)) {
        const QRectF rect = balloonRect(*c.balloon, *own);
        out = owned(shapeHandles(own->shape, rect, context.clearance, context.lastPicked()), own->id, &dragBody);
        for (Handle& h : out)
            if (partOf<ShapePart>(h) == ShapePart::AddVertex)
                h.press = &addNode;
        out.append(owned(fadeHandles(c.balloon->style.fill.fill, rect), c.balloon->id, &dragFill));
    }
    // Every words' frame; the picked words' first, so where two frames lie over each other a press takes theirs.
    QList<const Words*> words;
    for (const Words& w : c.words)
        (w.id == context.words ? words.prepend(&w) : words.append(&w));
    for (const Words* w : std::as_const(words)) {
        QList<Handle> frame = owned(textAreaHandles(c, *w), w->id, &dragWords);
        for (Handle& h : frame)
            h.reset = &resetWords;
        out.append(frame);
    }
    return out;
}

}  // namespace Geometry
