#include "object.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <utility>

namespace StripEdit {

namespace {

//! Smallest box the author can resize to — below this the grips overlap and it stops being draggable.
constexpr int k_minBox = 40;
//! Grip size in *screen* pixels, converted to scene units against the current zoom (see gripSpan()).
constexpr qreal k_gripScreenPx = 9.0;
//! Bounds on that conversion, so boundingRect() can reserve a fixed margin and stay valid at any zoom.
constexpr qreal k_gripMin = 6.0;
constexpr qreal k_gripMax = 18.0;
//! Room reserved around the box for grips — half of k_gripMax, rounded up.
constexpr qreal k_gripMargin = 10.0;

QPainter::CompositionMode compositionFor(Platemaker::Models::BlendMode b)
{
    using B = Platemaker::Models::BlendMode;
    switch (b) {
    case B::Multiply: return QPainter::CompositionMode_Multiply;
    case B::Screen:   return QPainter::CompositionMode_Screen;
    case B::Overlay:  return QPainter::CompositionMode_Overlay;
    case B::Darken:   return QPainter::CompositionMode_Darken;
    case B::Lighten:  return QPainter::CompositionMode_Lighten;
    case B::Over:     break;
    }
    return QPainter::CompositionMode_SourceOver;
}

//! Grip size in scene units for the current zoom, so it stays roughly constant on screen.
qreal gripSpan(const QGraphicsItem* item)
{
    qreal scale = 1.0;
    if (const QGraphicsScene* s = item->scene(); s && !s->views().isEmpty())
        scale = s->views().first()->transform().m11();
    // The item may carry a scale of its own (ObjectController::itemScaleFor), and a grip is chrome: it
    // has to be the same size on screen whatever the object is drawn at. Both transforms apply to
    // anything the item paints, so both have to be divided back out.
    scale *= item->scale();
    if (scale <= 0.0)
        scale = 1.0;
    return qBound(k_gripMin, k_gripScreenPx / scale, k_gripMax);
}

} // namespace

Object::Object(QString uid, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_uid(std::move(uid))
{
    // Selectable but not ItemIsMovable: a press can land on a resize or handle grip, and letting the
    // base class move the item as well would drag the object while the author is resizing it. Movement
    // is handled here, in one place, for whichever grip the press actually hit.
    setFlag(ItemIsSelectable, true);
    setAcceptHoverEvents(true);
}

void Object::setBlend(Platemaker::Models::BlendMode blend)
{
    if (m_blend == blend)
        return;
    m_blend = blend;
    update();
}

void Object::setOrphaned(bool orphaned)
{
    if (m_orphaned == orphaned)
        return;
    m_orphaned = orphaned;
    setFlag(ItemIsSelectable, !orphaned);
    setAcceptHoverEvents(!orphaned);
    update();
}

void Object::refreshBounds()
{
    const QRectF next = computeBounds();
    if (next == m_bounds)
        return;
    prepareGeometryChange();
    m_bounds = next;
}

QRectF Object::boundingRect() const
{
    // The object's own extent, not its box: local (0,0) is still the box's top-left, so a bubble's tail
    // pointing up or left gives this rect a negative origin. Sharing one bounds computation with the
    // rasteriser and the SVG writer is what keeps all three describing the same rectangle.
    return contentBounds().adjusted(-k_gripMargin, -k_gripMargin, k_gripMargin, k_gripMargin);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void Object::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    painter->save();
    if (m_orphaned)
        painter->setOpacity(0.35);   // present but not rendering — see the object list's "re-anchor"

    painter->setCompositionMode(compositionFor(m_blend));
    paintContent(*painter);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (option->state & QStyle::State_Selected) {
        const QRectF box(QPointF(0, 0), boxSize());

        // Selection chrome in the palette's highlight colour, cosmetic so it stays 1px at any zoom
        // (no hardcoded colours — the app is themed).
        QPen pen(option->palette.color(QPalette::Highlight));
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box);

        pen.setStyle(Qt::SolidLine);
        painter->setPen(pen);
        painter->setBrush(option->palette.color(QPalette::Base));
        for (const Grip g : {Grip::TopLeft, Grip::TopRight, Grip::BottomLeft, Grip::BottomRight})
            painter->drawRect(gripRect(g));

        painter->setBrush(option->palette.color(QPalette::Highlight));
        for (int i = 0; i < handleCount(); ++i)
            painter->drawEllipse(handleRect(i));
    }
    painter->restore();
}

// ---------------------------------------------------------------------------
// Grips
// ---------------------------------------------------------------------------

QRectF Object::gripRect(Grip g) const
{
    const qreal  s = gripSpan(this);
    const QRectF box(QPointF(0, 0), boxSize());

    QPointF c;
    switch (g) {
    case Grip::TopLeft:     c = box.topLeft();     break;
    case Grip::TopRight:    c = box.topRight();    break;
    case Grip::BottomLeft:  c = box.bottomLeft();  break;
    case Grip::BottomRight: c = box.bottomRight(); break;
    default:                return {};
    }
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

QRectF Object::handleRect(int index) const
{
    if (index < 0 || index >= handleCount())
        return {};
    const qreal   s = gripSpan(this);
    const QPointF c = handlePos(index);
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

Object::Grip Object::gripAt(const QPointF& local, int* handleIndex) const
{
    if (handleIndex)
        *handleIndex = -1;
    if (!isSelected())
        return Grip::Body;   // grips only exist on the selected object; a press elsewhere is a move

    // Handles first: a tail tip can sit near a corner, and aiming is the more specific intent there.
    for (int i = 0; i < handleCount(); ++i) {
        if (handleRect(i).contains(local)) {
            if (handleIndex)
                *handleIndex = i;
            return Grip::Handle;
        }
    }
    for (const Grip g : {Grip::TopLeft, Grip::TopRight, Grip::BottomLeft, Grip::BottomRight})
        if (gripRect(g).contains(local))
            return g;
    return Grip::Body;
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void Object::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_orphaned || e->button() != Qt::LeftButton) {
        QGraphicsObject::mousePressEvent(e);
        return;
    }

    // Let the base class run the selection logic (which may select this object), then read the grip —
    // gripAt() depends on being selected, so the order matters.
    QGraphicsObject::mousePressEvent(e);

    m_active        = gripAt(e->pos(), &m_activeHandle);
    m_startRect     = QRectF(pos(), boxSize());
    m_startScenePos = e->scenePos();
    m_moved         = false;
    e->accept();
}

void Object::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_active == Grip::None) {
        QGraphicsObject::mouseMoveEvent(e);
        return;
    }

    const QPointF delta = e->scenePos() - m_startScenePos;
    m_moved = true;

    if (m_active == Grip::Body) {
        setPos(m_startRect.topLeft() + delta);
        return;
    }

    if (m_active == Grip::Handle && m_activeHandle >= 0 && m_activeHandle < handleCount()) {
        // Unclamped on purpose: the artwork's extent is computed from where its handles actually point,
        // so a tip well outside the box makes the object bigger rather than being cropped away.
        setHandlePos(m_activeHandle, e->pos());
        refreshBounds();
        update();
        return;
    }

    // Corner resize: work from the rect at press rather than accumulating per-move deltas, so a fast
    // drag cannot accumulate rounding error.
    QRectF r = m_startRect;
    switch (m_active) {
    case Grip::TopLeft:     r.setTopLeft(r.topLeft() + delta);         break;
    case Grip::TopRight:    r.setTopRight(r.topRight() + delta);       break;
    case Grip::BottomLeft:  r.setBottomLeft(r.bottomLeft() + delta);   break;
    case Grip::BottomRight: r.setBottomRight(r.bottomRight() + delta); break;
    default: break;
    }
    r = r.normalized();
    if (r.width() < k_minBox || r.height() < k_minBox)
        return;   // refuse rather than clamp: clamping makes the box "stick" and jump on the way back

    // The corner opposite the dragged one stays put, because r was built by moving only that one.
    if (keepsAspect() && m_startRect.height() > 0.0 && m_startRect.width() > 0.0) {
        const qreal h = r.width() * (m_startRect.height() / m_startRect.width());
        if (m_active == Grip::TopLeft || m_active == Grip::TopRight)
            r.setTop(r.bottom() - h);
        else
            r.setBottom(r.top() + h);
    }

    const QSizeF newBox(qRound(r.width()), qRound(r.height()));
    if (newBox != boxSize()) {
        setBoxSize(newBox);
        refreshBounds();
    }
    setPos(r.topLeft());
    update();
}

void Object::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    const bool report = m_moved && m_active != Grip::None;
    m_active = Grip::None;
    m_moved  = false;
    QGraphicsObject::mouseReleaseEvent(e);

    // Only a settled drag is persisted: the owner turns each report into one undo step, and reporting
    // per mouse-move would bury the history under a pixel-by-pixel trail.
    if (!report)
        return;
    emit geometryEdited(m_uid);
}

void Object::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    switch (gripAt(e->pos())) {
    case Grip::TopLeft:
    case Grip::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case Grip::TopRight:
    case Grip::BottomLeft:  setCursor(Qt::SizeBDiagCursor); break;
    case Grip::Handle:      setCursor(Qt::CrossCursor);     break;
    default:                setCursor(Qt::SizeAllCursor);   break;
    }
    QGraphicsObject::hoverMoveEvent(e);
}

}  // namespace StripEdit
