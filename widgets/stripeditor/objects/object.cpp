#include "objects/object.hpp"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <utility>

namespace StripEdit {

namespace {

//! Smallest box the author can resize to — below this the grips overlap and it stops being draggable.
constexpr int k_minBox = 40;
//! Bounds on a tail tip's hit square in item units, so boundingRect() can reserve a fixed margin and
//! stay valid at any zoom. The chrome draws the handles at a true screen size; this is only the area
//! that answers a press on a tip of an object that is *not* selected.
constexpr qreal k_gripMin = 6.0;
constexpr qreal k_gripMax = 18.0;
//! Room reserved around the drawn extent for a tip's hit square — half of k_gripMax, rounded up.
constexpr qreal k_gripMargin = 10.0;
//! Opacity for unanchored objects — present but not rendering is visually semi-transparent.
constexpr qreal k_unanchoredOpacity = 0.35;

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

//! A tip's hit square in item units for the current zoom, so it stays roughly constant on screen.
qreal gripSpan(const QGraphicsItem* item)
{
    qreal scale = 1.0;
    if (const QGraphicsScene* s = item->scene(); s && !s->views().isEmpty())
        scale = s->views().first()->transform().m11();
    // The item may carry a scale of its own (ObjectController::itemScaleFor); both transforms apply to
    // anything in item units, so both have to be divided back out.
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
    // Selectable but not ItemIsMovable: a press can land on a tail tip, and letting the base class move
    // the item as well would drag the object while the author is aiming. Movement is handled here, in
    // one place, for whichever grip the press actually hit.
    setFlag(ItemIsSelectable, true);
    // The selection chrome follows this object from outside, so it has to hear about every move.
    setFlag(ItemSendsGeometryChanges, true);
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
    update();
}

void Object::refreshBounds()
{
    // The chrome is told even when the extent did not move: a box can change inside the same extent
    // (a tail reaching further than the balloon), and a tail tip can move without changing either.
    emit chromeChanged();
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

QPainterPath Object::shape() const
{
    // Rectangles only, and all of them wound the same way, so the winding rule can be trusted where they
    // overlap — a tip sitting on the box must add to the hit area, not punch a hole in it, which is
    // exactly what the default odd-even rule would do.
    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    p.addRect(QRectF(QPointF(0, 0), boxSize()));

    // A tail tip is grabbable whether or not the object is selected — it is how a bubble is aimed, and
    // it is usually the only part of it out there. Not one hidden inside its balloon: that spot shows
    // nothing, so a press there is a press on the balloon.
    for (int i = 0; i < handleCount(); ++i)
        if (handleDrawn(i))
            p.addRect(handleRect(i));
    return p;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

bool Object::s_chromeVisible = true;

void Object::moveHandle(int index, const QPointF& local)
{
    if (index < 0 || index >= handleCount())
        return;
    setHandlePos(index, local);
    refreshBounds();
    update();
}

void Object::setChromeVisible(bool on)
{
    s_chromeVisible = on;
}

void Object::setShapeEditing(bool on)
{
    if (m_shapeEditing == on)
        return;
    m_shapeEditing = on;
    m_pickedVertex = -1;
    emit chromeChanged();
}

void Object::setPickedVertex(int index)
{
    if (m_pickedVertex == index)
        return;
    m_pickedVertex = index;
    emit chromeChanged();
}

qreal Object::gripSpanHere() const
{
    return gripSpan(this);
}

void Object::setFocusedHandle(int index)
{
    if (m_focusedHandle == index)
        return;
    m_focusedHandle = index;
    emit chromeChanged();
}

void Object::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    // The object only. Its selection chrome is drawn by the chrome item, above every object.
    painter->save();
    if (m_orphaned)
        painter->setOpacity(k_unanchoredOpacity);   // present but not rendering

    painter->setCompositionMode(compositionFor(m_blend));
    paintContent(*painter);
    painter->restore();
}

QVariant Object::itemChange(GraphicsItemChange change, const QVariant& value)
{
    switch (change) {
    case ItemPositionHasChanged:
    case ItemSelectedHasChanged:
    case ItemScaleHasChanged:
    case ItemTransformHasChanged:
    case ItemVisibleHasChanged:
        emit chromeChanged();
        break;
    default:
        break;
    }
    return QGraphicsObject::itemChange(change, value);
}

// ---------------------------------------------------------------------------
// Grips
// ---------------------------------------------------------------------------

QRectF Object::handleRect(int index) const
{
    if (index < 0 || index >= handleCount())
        return {};
    const qreal   s = gripSpan(this);
    const QPointF c = handlePos(index);
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

Object::Grip Object::gripAtScene(const QPointF& scenePos, int* handleIndex) const
{
    return gripAt(mapFromScene(scenePos), handleIndex);
}

Object::Grip Object::gripAt(const QPointF& local, int* handleIndex) const
{
    if (handleIndex)
        *handleIndex = -1;
    // Only the tips that can be seen: the box's grips, and the handles of hidden tips, are the
    // selection chrome's, and a press that reaches the object itself never landed on them.
    for (int i = 0; i < handleCount(); ++i) {
        if (handleDrawn(i) && handleRect(i).contains(local)) {
            if (handleIndex)
                *handleIndex = i;
            return Grip::Handle;
        }
    }
    return Grip::Body;
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void Object::beginDrag(Grip grip, int handle, const QPointF& scenePos)
{
    m_active        = grip;
    m_dragGrip      = grip;
    m_activeHandle  = grip == Grip::Handle || grip == Grip::Shape ? handle : -1;
    m_startRect     = sceneBox();
    m_startScenePos = scenePos;
    m_moved         = grip == Grip::Shape && beginShapeDrag(handle);
    m_dragReported  = false;
    // A shape handle is not a tail: to the owner it is a press on the object, which keeps it selected.
    emit pressed(m_uid, grip == Grip::Handle ? handle : -1);
    if (grip == Grip::Shape)
        emit chromeChanged();   // the pressed handle is drawn as pressed
}

void Object::dragTo(const QPointF& scenePos, Qt::KeyboardModifiers modifiers)
{
    if (m_active == Grip::None)
        return;

    const QPointF delta = scenePos - m_startScenePos;
    m_moved = true;

    if (m_active == Grip::Body) {
        setPos(m_startRect.topLeft() + delta);
        m_dragReported = true;
        emit dragging(m_uid, delta, -1);   // the owner decides whether anything travels with it
        return;
    }

    if (m_active == Grip::Shape) {
        dragShapeTo(scenePos);
        return;
    }

    if (m_active == Grip::Handle) {
        if (m_activeHandle < 0 || m_activeHandle >= handleCount())
            return;
        // Unclamped on purpose: the artwork's extent is computed from where its handles actually point,
        // so a tip well outside the box makes the object bigger rather than being cropped away.
        setHandlePos(m_activeHandle, mapFromScene(scenePos));
        refreshBounds();
        update();
        m_dragReported = true;
        emit dragging(m_uid, delta, m_activeHandle);
        return;
    }

    // A corner or an edge. The maths is in scene units — that is where the pointer is — and the box is
    // in the item's own, which differ by the item's scale (artwork drawn at another size). Snapping to
    // one item unit keeps the box whole and the anchored side exactly where it was.
    const qreal k = scale() > 0.0 ? scale() : 1.0;
    ResizeRule rule;
    rule.keepAspect = keepsAspect() || modifiers.testFlag(Qt::ShiftModifier);
    rule.fromCentre = modifiers.testFlag(Qt::ControlModifier);
    rule.minSize    = k_minBox * k;
    rule.snap       = k;
    const std::optional<QRectF> r = resizedRect(m_startRect, m_active, delta, rule);
    if (!r)
        return;   // refused, not clamped: the box keeps its last good size

    const QSizeF box(qRound(r->width() / k), qRound(r->height() / k));
    // How far the top-left moves, in item units — what a tail tip has to move back by to keep pointing
    // where it pointed.
    const QPointF shift = (pos() - r->topLeft()) / k;
    if (box != boxSize() || !shift.isNull()) {
        setBoxSize(box, shift);
        refreshBounds();
    }
    setPos(r->topLeft());
    update();
}

void Object::endDrag()
{
    const bool report = m_moved && m_active != Grip::None;
    const bool shape  = m_active == Grip::Shape;
    const bool was    = m_active != Grip::None;
    m_active = Grip::None;
    m_moved  = false;
    if (shape)
        emit chromeChanged();   // ...and no longer
    // At rest an object may draw otherwise than mid-drag (a textured balloon shows the library's
    // rasterisation), and a click that moved nothing triggers no other repaint: without this the press's
    // picture stayed.
    if (was)
        update();

    // Only a settled drag is persisted: the owner turns each report into one undo step, and reporting
    // per mouse-move would bury the history under a pixel-by-pixel trail.
    if (report)
        emit geometryCommitted(m_uid);
}

void Object::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    // The right button is the menu's, and a menu acts on what you pointed at: an object that is not in
    // the selection becomes the selection, exactly as it would on a left click. One already selected is
    // left alone, so right-clicking one of several keeps the set the menu is about to act on.
    //
    // **And the press must be accepted.** The scene clears the selection on any press no item took, so
    // handing a right press to the base class — which ignores it — made the scene treat a click on a
    // balloon as a click on nothing: whatever had just been selected was deselected a moment later. The
    // menu itself arrives separately, as a context-menu event, and is unaffected.
    if (!m_orphaned && e->button() == Qt::RightButton) {
        if (!isSelected()) {
            if (scene())
                scene()->clearSelection();
            setSelected(true);
        }
        e->accept();
        return;   // the right button drags nothing, so there is no grip to record
    }

    if (m_orphaned || e->button() != Qt::LeftButton) {
        QGraphicsObject::mousePressEvent(e);
        return;
    }

    // Let the base class run the selection logic (which may select this object), then start the drag.
    QGraphicsObject::mousePressEvent(e);

    int handle = -1;
    const Grip grip = gripAt(e->pos(), &handle);
    e->accept();
    beginDrag(grip, handle, e->scenePos());
}

void Object::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_active == Grip::None) {
        QGraphicsObject::mouseMoveEvent(e);
        return;
    }
    dragTo(e->scenePos(), e->modifiers());
}

void Object::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e)
{
    // The second press of a double-click asks for shape editing; it starts no second drag, so the
    // release that follows has nothing to report.
    if (!m_orphaned && e->button() == Qt::LeftButton) {
        e->accept();
        emit doubleClicked(m_uid);
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(e);
}

void Object::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    // The base class only changes the selection on a release that did not move, and a drag that did
    // not move reports nothing — so the order of the two does not matter.
    endDrag();
    QGraphicsObject::mouseReleaseEvent(e);
}

}  // namespace StripEdit
