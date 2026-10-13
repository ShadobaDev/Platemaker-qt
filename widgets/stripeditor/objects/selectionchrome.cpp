#include "objects/selectionchrome.hpp"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>

#include "recordpainter.hpp"

namespace StripEdit {

namespace {

//! Above every object, the placement rubber band (1000) included: the chrome is never covered.
constexpr qreal k_chromeZ = 1.0e6;
//! An edge grip is offered only when the edge is at least this many grip spans long on screen, so its
//! midpoint square never collides with the corners — the GIMP rule for small boxes.
constexpr qreal k_edgeMinSpans = 3.0;

//! The overflow outline's colour. The palette has no role that means "something is wrong", and the
//! highlight it does have is what the rest of the chrome is drawn in — so this one is said outright.
const QColor k_overflowColour(220, 40, 40);

constexpr Grip k_corners[] = {Grip::TopLeft, Grip::TopRight, Grip::BottomLeft, Grip::BottomRight};
constexpr Grip k_edges[]   = {Grip::Top, Grip::Bottom, Grip::Left, Grip::Right};

//! A square of side @p s centred on @p c.
QRectF square(const QPointF& c, qreal s)
{
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

} // namespace

SelectionChrome::SelectionChrome(const QGraphicsView* view)
    : m_view(view)
{
    setZValue(k_chromeZ);
    // The left button only: a right press is the context menu's, and has to reach the object below.
    setAcceptedMouseButtons(Qt::LeftButton);
}

qreal SelectionChrome::span() const
{
    const qreal zoom = m_view ? m_view->transform().m11() : 1.0;
    return k_gripScreenPx / (zoom > 0.0 ? zoom : 1.0);
}

QList<Object*> SelectionChrome::subjects() const
{
    QList<Object*> out;
    if (!scene())
        return out;
    for (QGraphicsItem* gi : scene()->selectedItems()) {
        auto* o = dynamic_cast<Object*>(gi);
        if (o && o->isVisible() && !o->isOrphaned())
            out.append(o);
    }
    // Topmost first: where two selected objects' grips overlap, the one drawn on top is the one taken.
    std::sort(out.begin(), out.end(),
              [](const Object* a, const Object* b) { return a->zValue() > b->zValue(); });
    return out;
}

QRectF SelectionChrome::gripRect(const Object* o, Grip grip, qreal s) const
{
    // While one of its tails is the subject, the box is not: its grips are not shown, so they do not
    // resize either — what looks selected is what an edit will change.
    if (o->focusedHandle() >= 0)
        return {};

    const QRectF box = o->sceneBox();
    switch (grip) {
    case Grip::TopLeft:     return square(box.topLeft(), s);
    case Grip::TopRight:    return square(box.topRight(), s);
    case Grip::BottomLeft:  return square(box.bottomLeft(), s);
    case Grip::BottomRight: return square(box.bottomRight(), s);
    default:                break;
    }

    // An edge: the whole edge between the two corner squares answers a press, as in GIMP — a far
    // easier target than the square drawn at its middle. Not offered on a picture, whose size is one
    // number, nor on an edge too short on screen to keep its square clear of the corners.
    if (o->keepsAspect())
        return {};
    const bool horizontal = grip == Grip::Top || grip == Grip::Bottom;
    if ((horizontal ? box.width() : box.height()) < k_edgeMinSpans * s)
        return {};
    switch (grip) {
    case Grip::Top:    return QRectF(box.left() + s / 2, box.top() - s / 2, box.width() - s, s);
    case Grip::Bottom: return QRectF(box.left() + s / 2, box.bottom() - s / 2, box.width() - s, s);
    case Grip::Left:   return QRectF(box.left() - s / 2, box.top() + s / 2, s, box.height() - s);
    case Grip::Right:  return QRectF(box.right() - s / 2, box.top() + s / 2, s, box.height() - s);
    default:           return {};
    }
}

QRectF SelectionChrome::handleRect(const Object* o, int index, qreal s) const
{
    return square(o->mapToScene(o->handleAt(index)), s);
}

QRectF SelectionChrome::overflowRect(const Object* o)
{
    const ObjectRecord& r = o->record();
    if (r.text.body.isEmpty() || r.box.isEmpty() || !Painter::textOverflows(r))
        return {};
    // The text area is in the record's box units. A balloon is drawn at exactly those; a picture's
    // lettering is laid out in the record's box and drawn stretched to the picture's (ArtworkObject).
    const QRectF area = Painter::textArea(r);
    const qreal  sx   = o->boxSize().width()  / r.box.width();
    const qreal  sy   = o->boxSize().height() / r.box.height();
    return o->mapRectToScene(QRectF(area.x() * sx, area.y() * sy, area.width() * sx, area.height() * sy));
}

void SelectionChrome::refresh()
{
    const qreal s = span();
    QRectF      next;
    m_overflows.clear();
    for (const Object* o : subjects()) {
        next = next.united(o->sceneBox().adjusted(-s, -s, s, s));
        for (int i = 0; i < o->tipCount(); ++i)
            next = next.united(handleRect(o, i, s).adjusted(-1, -1, 1, 1));
        // Measured here, not in paint(): laying out the lettering on every repaint would be paid on
        // every scroll. This runs when the object reports a change, which is when the answer can move.
        if (const QRectF r = overflowRect(o); !r.isEmpty()) {
            m_overflows.append(r);
            next = next.united(r.adjusted(-1, -1, 1, 1));
        }
    }
    if (next != m_bounds) {
        prepareGeometryChange();
        m_bounds = next;
    }
    update();
}

SelectionChrome::Hit SelectionChrome::hitAt(const QPointF& scenePos) const
{
    if (!m_bounds.contains(scenePos))
        return {};
    const qreal s = span();
    for (Object* o : subjects()) {
        // Handles first: a tail tip can sit near a corner, and aiming is the more specific intent there.
        // Every handle, the hidden ones too — this is the one place a tip inside its balloon is reachable.
        for (int i = 0; i < o->tipCount(); ++i)
            if (handleRect(o, i, s).contains(scenePos))
                return {o, Grip::Handle, i};
        for (const Grip g : k_corners)
            if (gripRect(o, g, s).contains(scenePos))
                return {o, g, -1};
        for (const Grip g : k_edges)
            if (gripRect(o, g, s).contains(scenePos))
                return {o, g, -1};
    }
    return {};
}

QPainterPath SelectionChrome::shape() const
{
    // Only what answers a press, so a press anywhere else reaches the objects below. One winding, so
    // where a handle overlaps a grip the two add rather than cancel.
    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    const qreal s = span();
    for (const Object* o : subjects()) {
        for (int i = 0; i < o->tipCount(); ++i)
            p.addRect(handleRect(o, i, s));
        for (const Grip g : k_corners)
            if (const QRectF r = gripRect(o, g, s); !r.isEmpty())
                p.addRect(r);
        for (const Grip g : k_edges)
            if (const QRectF r = gripRect(o, g, s); !r.isEmpty())
                p.addRect(r);
    }
    return p;
}

void SelectionChrome::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    if (!Object::chromeVisible())
        return;   // a repaint that samples the comic, not the editor (see Object::setChromeVisible())

    // In the palette's highlight colour, cosmetic so it stays 1px at any zoom (no hardcoded colours —
    // the app is themed).
    QPen pen(option->palette.color(QPalette::Highlight));
    pen.setCosmetic(true);
    const QBrush base      = option->palette.color(QPalette::Base);
    const QBrush highlight = option->palette.color(QPalette::Highlight);
    const qreal  s         = span();

    painter->save();
    QList<Object*> objects = subjects();
    std::reverse(objects.begin(), objects.end());   // bottom first, so the topmost's chrome ends on top
    for (const Object* o : std::as_const(objects)) {
        if (o->focusedHandle() < 0) {
            pen.setStyle(Qt::DashLine);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(o->sceneBox());
        }

        pen.setStyle(Qt::SolidLine);
        painter->setPen(pen);
        painter->setBrush(base);
        for (const Grip g : k_corners)
            if (const QRectF r = gripRect(o, g, s); !r.isEmpty())
                painter->drawRect(r);
        // An edge answers along its length but is drawn as one square at its middle.
        for (const Grip g : k_edges)
            if (const QRectF r = gripRect(o, g, s); !r.isEmpty())
                painter->drawRect(square(r.center(), s));

        // The selected tail's handle is drawn hollow, so among several tails the one being edited is
        // visible on the strip as well as in the list.
        for (int i = 0; i < o->tipCount(); ++i) {
            painter->setBrush(i == o->focusedHandle() ? base : highlight);
            painter->drawEllipse(handleRect(o, i, s));
        }
    }

    QPen overflow(k_overflowColour);
    overflow.setCosmetic(true);
    overflow.setStyle(Qt::DashLine);
    overflow.setWidth(2);
    painter->setPen(overflow);
    painter->setBrush(Qt::NoBrush);
    for (const QRectF& r : std::as_const(m_overflows))
        painter->drawRect(r);
    painter->restore();
}

void SelectionChrome::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    const Hit hit = hitAt(e->scenePos());
    if (!hit.object) {
        e->ignore();   // not on a grip after all: let the press reach whatever is below
        return;
    }
    e->accept();
    m_target = hit.object;
    hit.object->beginDrag(hit.grip, hit.handle, e->scenePos());
}

void SelectionChrome::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_target)
        m_target->dragTo(e->scenePos(), e->modifiers());
}

void SelectionChrome::mouseReleaseEvent(QGraphicsSceneMouseEvent*)
{
    if (m_target)
        m_target->endDrag();
    m_target = nullptr;
}

}  // namespace StripEdit
