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

//! The body @p h belongs to, or the mask's region.
Body* bodyFor(Composition& c, const Handle& h)
{
    if (c.mask && c.mask->region.id == h.element)
        return &c.mask->region;
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
 * corner moved, so nothing moves on the strip but the node being dragged. A bubble of a chain moves itself
 * instead, and nothing else moves. A box that fits its outline
 * already, or an outline with no width or height, leave it as it is.
 *
 * \return How far the object has to move: the box's top-left's move for a lone balloon, none for a chain's.
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
    // A lone balloon's body is at the origin, so the whole composition moves back by the corner (the caller moves
    // the object as much). A bubble of a chain moves itself: the others, the object and the tips stay.
    const bool    chain = c.balloon->bodies.size() > 1;
    const QPointF shift = chain ? QPointF() : corner;
    if (chain)
        own.place.pos = corner;

    // A handle is an offset from its node, so it only rescales.
    const QRectF  next = balloonRect(*c.balloon, own);
    const QPointF scale(rect.width() / next.width(), rect.height() / next.height());
    for (ShapeNode& n : own.shape.nodes) {
        const QPointF at(rect.left() + n.at.x() * rect.width() - shift.x(),
                         rect.top() + n.at.y() * rect.height() - shift.y());
        n.at  = QPointF((at.x() - next.left()) / next.width(), (at.y() - next.top()) / next.height());
        n.in  = QPointF(n.in.x() * scale.x(), n.in.y() * scale.y());
        n.out = QPointF(n.out.x() * scale.x(), n.out.y() * scale.y());
    }
    for (BalloonTail& t : c.balloon->tails)
        t.tail.tip -= shift;
    return shift;
}

//! A body's outline and edge: several picked nodes move together; a node pulled past the box grows it.
QPointF dragBody(Composition& next, const Handle& h, const QPointF& to, const HandleContext& context)
{
    Body* own = bodyFor(next, h);
    if (!own)
        return {};
    const QRectF rect = balloonRect(*next.balloon, *own);
    const bool   node = partOf<ShapePart>(h) == ShapePart::Vertex;
    if (node && context.pickedOf == own->id && context.picked.size() > 1)
        own->shape = withNodesMoved(own->shape, rect, context.picked, to - h.at);
    else
        own->shape = dragShapeHandle(own->shape, rect, h, to, context.clearance);
    return node ? fitToOutline(next, *own, rect) : QPointF();
}

//! The mask's outline, in its own box: several picked nodes move together. Its box stays: a region has no stroke
//! to fit, and a node past its box is drawn there all the same.
QPointF dragMask(Composition& next, const Handle& h, const QPointF& to, const HandleContext& context)
{
    if (!next.mask)
        return {};
    Body&        own  = next.mask->region;
    const QRectF rect(own.place.pos, QSizeF(own.size));
    if (partOf<ShapePart>(h) == ShapePart::Vertex && context.pickedOf == own.id && context.picked.size() > 1)
        own.shape = withNodesMoved(own.shape, rect, context.picked, to - h.at);
    else
        own.shape = dragShapeHandle(own.shape, rect, h, to, context.clearance);
    return {};
}

//! The two ends of a mask's fade (maskFade()), told apart by Handle::part.
enum class MaskFadePart { Start, End };

//! The mask's fade: the end dragged, anywhere, as a fraction of the region's box.
QPointF dragMaskFade(Composition& next, const Handle& h, const QPointF& to, const HandleContext&)
{
    if (!next.mask || !next.mask->fade || next.mask->region.size.isEmpty())
        return {};
    const Body&   own = next.mask->region;
    const QPointF at((to.x() - own.place.pos.x()) / own.size.width(), (to.y() - own.place.pos.y()) / own.size.height());
    QLineF&       fade = *next.mask->fade;
    (partOf<MaskFadePart>(h) == MaskFadePart::Start ? fade.setP1(at) : fade.setP2(at));
    return {};
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

//! The balloon's fill: the fade's cross, over every body (fillBox()).
QPointF dragFill(Composition& next, const Handle& h, const QPointF& to, const HandleContext&)
{
    if (next.balloon && !next.balloon->bodies.isEmpty())
        next.balloon->style.fill.fill = dragFadeHandle(next.balloon->style.fill.fill, fillBox(*next.balloon), h, to);
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

//! The words' warp: several picked nodes move together.
QPointF dragWarp(Composition& next, const Handle& h, const QPointF& to, const HandleContext& context)
{
    if (Words* w = wordsFor(next, h))
        *w = dragWarpHandle(next, *w, h, to, context.pickedOf == w->id ? context.picked : QList<int>());
    return {};
}

//! A "+" on the warp adds its node at once, and the drag that follows is that node's.
bool addWarpNode(Composition& c, Handle& h)
{
    Words* w = wordsFor(c, h);
    if (!w)
        return false;
    w->text.warp = Bezier::withNodeAdded(warpCurve(w->text), k_warpRules, h.index);   // dormant: now bent
    h.part       = int(Bezier::Part::Node);
    h.glyph      = Handle::Glyph::Node;
    h.index      = h.index + 1;
    h.node       = h.index;
    h.press      = nullptr;
    return true;
}

//! The words straightened: their warp gone, by a double-click on its node.
bool resetWarp(Composition& c, const Handle& h)
{
    Words* w = wordsFor(c, h);
    if (!w || w->text.warp.isEmpty())
        return false;
    w->text.warp.clear();
    return true;
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

//! The tail @p h belongs to.
BalloonTail* tailFor(Composition& c, const Handle& h)
{
    if (!c.balloon)
        return nullptr;
    for (BalloonTail& t : c.balloon->tails)
        if (t.id == h.element)
            return &t;
    return nullptr;
}

//! Which handle of tailHandles() is a bridge's far anchor.
constexpr int k_farAnchor = 1;

//! The anchor @p h stands for: the tail's own, or a bridge's far one.
QPointF& anchorOf(BalloonTail& t, const Handle& h)
{
    return h.index == k_farAnchor ? t.toAnchor : t.tail.anchor;
}

//! The box of the body the anchor @p h lies in, which it is a fraction of.
QRectF anchorBox(const Composition& c, const BalloonTail& t, const Handle& h)
{
    const Body* body = bodyOf(c, h.index == k_farAnchor ? t.to : t.root);
    return body ? balloonRect(*c.balloon, *body) : QRectF();
}

//! How many halvings find where a line from the middle of a body leaves its outline: far below a pixel.
constexpr int k_outlineSteps = 24;

/**
 * \brief A tail's anchor, kept inside its body's outline, and where the tail's base still fits (fittedTail()).
 *
 * Dragged out of the outline, it stops where the line from the body's middle to the pointer leaves it: outside, its
 * line would cross the bubble twice. Dragged where the base would stick out (too near the edge for a tail running
 * along it), it stops on the way from the body's middle to the pointer, at the last place the base fits, so a drag
 * along the edge slides it along that limit under the pointer; where the base does not fit in the middle either,
 * nothing holds it back. A body whose middle is outside its own outline (a deep notch) keeps it inside the box only.
 */
QPointF dragAnchor(Composition& next, const Handle& h, const QPointF& to, const HandleContext&)
{
    BalloonTail* t = tailFor(next, h);
    if (!t)
        return {};
    const Body* body = bodyOf(next, h.index == k_farAnchor ? t->to : t->root);
    if (!body)
        return {};
    const QRectF box = balloonRect(*next.balloon, *body);
    if (box.isEmpty())
        return {};
    QPointF            at      = to;
    const QPainterPath outline = outlineOf(*body, box, next.seed);
    if (!outline.contains(box.center()))
        at = QPointF(qBound(box.left(), to.x(), box.right()), qBound(box.top(), to.y(), box.bottom()));
    else if (!outline.contains(at)) {
        QPointF in = box.center(), out = at;
        for (int i = 0; i < k_outlineSteps; ++i) {
            const QPointF mid = (in + out) / 2.0;
            (outline.contains(mid) ? in : out) = mid;
        }
        at = in;
    }
    anchorOf(*t, h) = QPointF((at.x() - box.left()) / box.width(), (at.y() - box.top()) / box.height());
    BalloonTail drawn = fittedTail(*next.balloon, *t, next.seed);
    anchorOf(*t, h)   = anchorOf(drawn, h);   // the other end of a bridge stays as set
    return {};
}

//! A tail's anchor back in the middle of its body.
bool resetAnchor(Composition& c, const Handle& h)
{
    BalloonTail* t = tailFor(c, h);
    if (!t || anchorOf(*t, h) == Tail::k_centre)
        return false;
    anchorOf(*t, h) = Tail::k_centre;
    return true;
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

//! The mask's handles: its outline's, and its fade's two ends when it fades.
QList<Handle> maskHandles(const Composition& c, const HandleContext& context)
{
    const Body&   own = c.mask->region;
    QList<Handle> out = owned(shapeHandles(own.shape, QRectF(own.place.pos, QSizeF(own.size)), context.clearance,
                                           context.lastPicked(own.id)),
                              own.id, &dragMask);
    for (Handle& h : out)
        if (partOf<ShapePart>(h) == ShapePart::AddVertex)
            h.press = &addNode;
    if (const std::optional<QLineF> fade = maskFade(*c.mask)) {
        Handle start;
        start.part   = int(MaskFadePart::Start);
        start.at     = fade->p1();
        start.origin = fade->p1();
        start.glyph  = Handle::Glyph::FadeStart;
        Handle end   = start;
        end.part     = int(MaskFadePart::End);
        end.at       = fade->p2();
        end.start    = fade->p1();
        end.glyph    = Handle::Glyph::FadeEnd;
        if (fade->length() > 0.0)
            end.axis = (fade->p2() - fade->p1()) / fade->length();
        out.append(owned({start, end}, own.id, &dragMaskFade));
    }
    return out;
}

} // namespace

QList<Handle> handlesOf(const Composition& c, const HandleContext& context)
{
    // The mask picked: its handles alone, the object's own lie under it and are not what is edited.
    if (c.mask && context.body != 0 && context.body == c.mask->region.id)
        return maskHandles(c, context);
    QList<Handle> out;
    if (const Body* own = bodyOf(c, context.body)) {
        const QRectF rect = balloonRect(*c.balloon, *own);
        out = owned(shapeHandles(own->shape, rect, context.clearance, context.lastPicked(own->id)), own->id, &dragBody);
        for (Handle& h : out)
            if (partOf<ShapePart>(h) == ShapePart::AddVertex)
                h.press = &addNode;
        out.append(owned(fadeHandles(c.balloon->style.fill.fill, fillBox(*c.balloon)), c.balloon->id, &dragFill));
    }
    // Every words' frame; the picked words' first, so where two frames lie over each other a press takes theirs.
    QList<const Words*> words;
    for (const Words& w : c.words)
        (w.id == context.words ? words.prepend(&w) : words.append(&w));
    for (const Words* w : std::as_const(words)) {
        // The warp's before the frame's: its ends and its middle "+" lie on the frame's arrows and middle.
        // Words with no warp offer their straight curve when marked: a change to it bends them.
        QList<Handle> warp = owned(warpHandles(c, *w, context.lastPicked(w->id), w->id == context.words), w->id,
                                   &dragWarp);
        for (Handle& h : warp) {
            if (partOf<Bezier::Part>(h) == Bezier::Part::AddNode)
                h.press = &addWarpNode;
            if (partOf<Bezier::Part>(h) == Bezier::Part::Node)
                h.reset = &resetWarp;
        }
        out.append(warp);
        QList<Handle> frame = owned(textAreaHandles(c, *w), w->id, &dragWords);
        for (Handle& h : frame)
            h.reset = &resetWords;
        out.append(frame);
    }
    return out;
}

QList<Handle> tailHandles(const Composition& c, int tail)
{
    QList<Handle> out;
    if (!c.balloon)
        return out;
    for (const BalloonTail& set : c.balloon->tails) {
        if (set.id != tail)
            continue;
        const BalloonTail t = fittedTail(*c.balloon, set, c.seed);   // where the tail is seen to start
        for (int i = 0; i <= (t.isBridge() ? k_farAnchor : 0); ++i) {
            Handle h;
            h.index   = i;
            h.element = t.id;
            h.glyph   = Handle::Glyph::Anchor;
            h.drag    = &dragAnchor;
            h.reset   = &resetAnchor;
            const QRectF box = anchorBox(c, t, h);
            if (box.isEmpty())
                continue;
            h.at = anchorAt(box, i == k_farAnchor ? t.toAnchor : t.tail.anchor);
            out.append(h);
        }
    }
    return out;
}

}  // namespace Geometry
