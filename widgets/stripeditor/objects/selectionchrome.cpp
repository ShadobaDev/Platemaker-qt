#include "objects/selectionchrome.hpp"

#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <QSvgRenderer>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "geometry.hpp"

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

//! How much of a grip span a "+" is drawn across: smaller than a node, so the two read apart.
constexpr qreal k_plusSpan = 0.7;
//! The radius arrow's length, in grip spans.
constexpr qreal k_arrowSpan = 2.4;
//! The square that answers a press on the radius arrow, in grip spans: wider than the arrow, which is
//! thin across, so it is easy to hit.
constexpr qreal k_arrowHitSpan = 3.2;

//! The square a shape handle answers a press in.
QRectF shapeHandleRect(const QPointF& at, Geometry::ShapeHandle::Role role, qreal s)
{
    return square(at, role == Geometry::ShapeHandle::Role::Radius ? s * k_arrowHitSpan : s);
}

//! The radius handle's drawing, pointing along x, with its FILL and OUTLINE still to be coloured.
const QByteArray& radiusArrowSvg()
{
    static const QByteArray svg = [] {
        QFile f(QStringLiteral(":/icons/handles/radius.svg"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }();
    return svg;
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
    // resize either — what looks selected is what an edit will change. Nor while its shape is edited,
    // when the outline's own handles take their place.
    if (o->focusedHandle() >= 0 || o->shapeEditing())
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

QList<Geometry::ShapeHandle> SelectionChrome::shapeHandles(const Object* o)
{
    return o->shapeEditing() ? o->shapeHandles() : QList<Geometry::ShapeHandle>();
}

QRectF SelectionChrome::overflowRect(const Object* o)
{
    const ObjectRecord& r = o->record();
    if (r.text.body.isEmpty() || r.box.isEmpty() || !Geometry::textOverflows(r))
        return {};
    // The text area is in the record's box units. A balloon is drawn at exactly those; a picture's
    // lettering is laid out in the record's box and drawn stretched to the picture's (ArtworkObject).
    const QRectF area = Geometry::textArea(r);
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
        if (o->shapeEditing())
            next = next.united(o->mapToScene(o->editOutline()).boundingRect().adjusted(-1, -1, 1, 1));
        for (const Geometry::ShapeHandle& h : shapeHandles(o))
            next = next.united(shapeHandleRect(o->mapToScene(h.at), h.role, s).adjusted(-2, -2, 2, 2));
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
        // Then the outline's own, in the order the shape lists them (a node before a "+").
        const QList<Geometry::ShapeHandle> handles = shapeHandles(o);
        for (int i = 0; i < handles.size(); ++i)
            if (shapeHandleRect(o->mapToScene(handles.at(i).at), handles.at(i).role, s).contains(scenePos))
                return {o, Grip::Shape, i};
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
        for (const Geometry::ShapeHandle& h : shapeHandles(o))
            p.addRect(shapeHandleRect(o->mapToScene(h.at), h.role, s));
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
        // Shape editing has a chrome of its own (PLAN-L B27): the outline itself, traced, instead of the
        // dashed box — so the mode is plain at a glance.
        if (o->shapeEditing()) {
            pen.setStyle(Qt::SolidLine);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(o->mapToScene(o->editOutline()));
        } else if (o->focusedHandle() < 0) {
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

        // The outline's handles: a node is a square, unlike a tail's dot, filled when it is the picked
        // one; the radius is an arrow into its corner; a "+" adds a node where it sits.
        using Role = Geometry::ShapeHandle::Role;
        const QList<Geometry::ShapeHandle> handles = shapeHandles(o);
        for (int i = 0; i < handles.size(); ++i) {
            const Geometry::ShapeHandle& h  = handles.at(i);
            const QPointF                at = o->mapToScene(h.at);
            switch (h.role) {
            case Role::Vertex:
                painter->setBrush(h.index == o->pickedVertex() ? highlight : base);
                painter->drawRect(square(at, s));
                break;
            case Role::Radius: {
                // The arrow (icons/handles/radius.svg) in the highlight, edged in the colour that reads
                // on it so it shows on any fill, turned along the corner's diagonal; pressed, the two
                // colours swap.
                const QColor  light   = option->palette.color(QPalette::HighlightedText);
                const bool    pressed = i == o->activeShapeHandle();
                QSvgRenderer& arrow   = pressed ? radiusArrow(light, highlight.color())
                                                : radiusArrow(highlight.color(), light);
                const qreal   len   = s * k_arrowSpan;
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->translate(at);
                painter->rotate(qRadiansToDegrees(std::atan2(h.axis.y(), h.axis.x())));
                arrow.render(painter, QRectF(-len / 2, -len / 2, len, len));
                painter->restore();
                break;
            }
            case Role::AddVertex: {
                const qreal half = s * k_plusSpan / 2;
                painter->drawLine(at - QPointF(half, 0), at + QPointF(half, 0));
                painter->drawLine(at - QPointF(0, half), at + QPointF(0, half));
                break;
            }
            }
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

QSvgRenderer& SelectionChrome::radiusArrow(const QColor& fill, const QColor& edge)
{
    std::unique_ptr<QSvgRenderer>& renderer = m_arrows[fill.name() + edge.name()];
    if (!renderer) {
        QByteArray svg = radiusArrowSvg();
        svg.replace("FILL", fill.name().toLatin1());
        svg.replace("OUTLINE", edge.name().toLatin1());
        renderer = std::make_unique<QSvgRenderer>(svg);
    }
    return *renderer;
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
