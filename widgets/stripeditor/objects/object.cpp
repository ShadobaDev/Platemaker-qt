#include "objects/object.hpp"
#include "chain.hpp"

#include <QGraphicsScene>
#include <QGuiApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <utility>

namespace StripEdit {

namespace {

//! Bounds on a tail tip's hit square in item units, so boundingRect() can reserve a fixed margin and
//! stay valid at any zoom. The chrome draws the handles at a true screen size; this is only the area
//! that answers a press on a tip of an object that is *not* selected.
constexpr qreal k_gripMin = 6.0;
constexpr qreal k_gripMax = 18.0;
//! Room reserved around the drawn extent for a tip's hit square — half of k_gripMax, rounded up.
constexpr qreal k_gripMargin = 10.0;
//! Opacity for unanchored objects — present but not rendering is visually semi-transparent.
constexpr qreal k_unanchoredOpacity = 0.35;
//! The radius handle's distance in from its arc, in grip spans: its arrow, a span either side of it,
//! then stays clear of the corner's own node.
constexpr qreal k_radiusDotClearance = 2.2;

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

//! How an object in @p b is kept between repaints. What it draws changes far less often than the strip
//! is repainted (a drag, a hover, the chrome), and a halftone or a flash costs tens of milliseconds to
//! draw, so it is drawn once into a pixmap at the view's zoom and that is shown until update() says it
//! changed. Not with a blend mode: the pixmap is drawn over a transparent one, where a multiply has
//! nothing to multiply, and only its result would then be laid over the strip.
QGraphicsItem::CacheMode cacheFor(Platemaker::Models::BlendMode b)
{
    return b == Platemaker::Models::BlendMode::Over ? QGraphicsItem::DeviceCoordinateCache : QGraphicsItem::NoCache;
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
    setCacheMode(cacheFor(m_blend));
}

void Object::setBlend(Platemaker::Models::BlendMode blend)
{
    if (m_blend == blend)
        return;
    m_blend = blend;
    setCacheMode(cacheFor(m_blend));
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
    QPainterPath p = hitArea();
    p.setFillRule(Qt::WindingFill);

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
    m_pickedVertices.clear();
    m_pickedElement = 0;
    emit chromeChanged();
}

void Object::setPickedVertex(int index, int element)
{
    setPickedVertices(index < 0 ? QList<int>{} : QList<int>{index}, element);
}

void Object::setPickedVertices(const QList<int>& indices, int element)
{
    const int owner = indices.isEmpty() ? 0 : element;
    if (m_pickedVertices == indices && m_pickedElement == owner)
        return;
    m_pickedVertices = indices;
    m_pickedElement  = owner;
    emit chromeChanged();
    emit pickedVertexChanged();
}

void Object::pickNodeOf(int index)
{
    const QList<Geometry::Handle> handles = shapeHandles();
    if (index >= 0 && index < handles.size() && handles.at(index).node >= 0)
        setPickedVertex(handles.at(index).node, handles.at(index).element);
}

qreal Object::gripSpanHere() const
{
    return gripSpan(this);
}

Geometry::HandleContext Object::handleContext() const
{
    return {gripSpanHere() * k_radiusDotClearance, pickedVertices(), markedWords(), m_pickedBody, m_pickedElement};
}

void Object::setPickedBody(int id)
{
    if (m_pickedBody == id)
        return;
    m_pickedBody = id;
    setPickedVertex(-1);   // the nodes picked were another body's
    emit chromeChanged();
}

const Mask* Object::pickedMask() const
{
    const std::optional<Mask>& m = m_composition.mask;
    return m && m_pickedBody != 0 && m->id == m_pickedBody ? &*m : nullptr;
}

QRectF Object::chromeBox() const
{
    if (const Mask* m = pickedMask())
        return mapRectToScene(Geometry::maskBox(*m));
    // The bubble picked, or the one the picked words sit on: what the click went through to.
    int seat = m_pickedBody;
    for (const Words& w : m_composition.words)
        if (seat == 0 && m_pickedWords != 0 && w.id == m_pickedWords)
            seat = w.seat;
    const QRectF body = seat != 0 ? bodyBox(seat) : QRectF();
    return mapRectToScene(body.isEmpty() ? frameBox() : body);
}

QPainterPath Object::hitArea() const
{
    QPainterPath p;
    p.addRect(QRectF(QPointF(0, 0), boxSize()));
    return p;
}

void Object::setPickedWords(int id)
{
    if (m_pickedWords == id)
        return;
    m_pickedWords = id;
    emit chromeChanged();
}

int Object::markedWords() const
{
    for (const Words& w : m_composition.words)
        if (w.id == m_pickedWords)
            return w.id;
    return m_composition.words.isEmpty() ? 0 : m_composition.words.first().id;
}

QList<Geometry::Handle> Object::shapeHandles() const
{
    if (m_shapeEditing)
        return canEditShape() ? Geometry::handlesOf(m_composition, handleContext()) : QList<Geometry::Handle>();
    // One tail picked, alone: where it leaves from is its to move.
    if (m_focusedHandles.size() == 1 && m_composition.balloon) {
        const int at = m_focusedHandles.first();
        if (at >= 0 && at < m_composition.balloon->tails.size())
            return Geometry::tailHandles(m_composition, m_composition.balloon->tails.at(at).id);
    }
    return {};
}

bool Object::resetShapeHandle(int index)
{
    const QList<Geometry::Handle> handles = shapeHandles();
    if (index < 0 || index >= handles.size() || !handles.at(index).reset)
        return false;
    Composition next = m_composition;
    if (!handles.at(index).reset(next, handles.at(index)))
        return false;
    emit compositionEdited(m_uid, next);
    emit geometryCommitted(m_uid);
    return true;
}

bool Object::beginShapeDrag(int index)
{
    const QList<Geometry::Handle> handles = shapeHandles();
    if (index < 0 || index >= handles.size()) {
        m_shapeHandle = {};   // no element pressed (beginDrag())
        return false;
    }
    m_shapeStart    = m_composition;
    m_shapeStartPos = pos();
    m_shapeHandle   = handles.at(index);
    // What a press does is the handle's owner's (a "+" adds its node, and the drag that follows is that node's).
    const bool changed = m_shapeHandle.press && m_shapeHandle.press(m_shapeStart, m_shapeHandle);
    if (changed)
        emit compositionEdited(uid(), m_shapeStart);
    // A node, or one of its handles, picks that node: its handles show, and Delete or its type act on it.
    // Ctrl+press on a node adds it to the pick, or takes it away (and then drags nothing). A plain press on a
    // node already among several keeps them, so the drag moves them all.
    m_shapeDragOff = false;
    if (m_shapeHandle.node < 0) {
        setPickedVertex(-1);
        return changed;
    }
    const int  node       = m_shapeHandle.node;
    const int  owner      = m_shapeHandle.element;
    const bool nodeItself = m_shapeHandle.glyph == Geometry::Handle::Glyph::Node;
    // The nodes picked are one element's: a node of another starts a pick of its own.
    QList<int> picked     = m_pickedElement == owner ? pickedVertices() : QList<int>{};
    if (nodeItself && !changed
        && QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        m_shapeDragOff = picked.removeAll(node) > 0;
        if (!m_shapeDragOff)
            picked.append(node);
        setPickedVertices(picked, owner);
    } else if (nodeItself && picked.contains(node)) {
        picked.removeAll(node);
        setPickedVertices(picked << node, owner);   // the one pressed is the one picked last
    } else {
        setPickedVertex(node, owner);
    }
    return changed;
}

void Object::dragShapeTo(const QPointF& scenePos)
{
    if (m_shapeDragOff || !m_shapeHandle.drag)
        return;   // a Ctrl+press that took its node out of the pick
    // In the units of the box as it was at the press: a node past the box moves the item, and measuring against
    // where it has moved to would chase the pointer.
    const qreal   k  = scale() > 0.0 ? scale() : 1.0;
    const QPointF to = (scenePos - m_shapeStartPos) / k;

    // The handle's owner changes its element, and says how far that moved the composition's frame.
    Composition   next  = m_shapeStart;
    const QPointF moved = m_shapeHandle.drag(next, m_shapeHandle, to, handleContext());
    emit compositionEdited(uid(), next);
    setPos(m_shapeStartPos + moved * k);
}

void Object::setFocusedHandles(const QList<int>& indices)
{
    if (m_focusedHandles == indices)
        return;
    m_focusedHandles = indices;
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
    // A corner or an edge of a bubble picked on its own resizes that bubble, from its own box.
    const bool sized = grip != Grip::Body && grip != Grip::Handle && grip != Grip::Shape && grip != Grip::None;
    m_resizeBody    = sized ? m_pickedBody : 0;
    m_startRect     = m_resizeBody != 0 ? chromeBox() : sceneBox();
    if (m_resizeBody != 0)
        m_dragStart = m_composition;
    m_startScenePos = scenePos;
    m_moved         = grip == Grip::Shape && beginShapeDrag(handle);
    m_dragReported  = false;
    // A shape handle is not a tail: to the owner it is a press on the object, which keeps it selected.
    emit pressed(m_uid, grip == Grip::Handle ? handle : -1);
    // A frame's handle picks its words, as a press on their letters does, and only after the press: the owner
    // takes the press as one on the object, and the words picked then are what it is left holding. An object
    // with one words is its words already.
    if (grip == Grip::Shape && m_composition.words.size() > 1
        && std::any_of(m_composition.words.cbegin(), m_composition.words.cend(),
                       [this](const Words& w) { return w.id == m_shapeHandle.element; }))
        emit wordsPressed(m_uid, m_shapeHandle.element);
    if (grip == Grip::Shape)
        emit chromeChanged();   // the pressed handle is drawn as pressed
    // A click goes through: a press in one bubble of a chain already selected picks and moves that bubble; the
    // first press, one on a bridge, or one with Ctrl (which gathers objects) moves the whole object.
    // A press in the picked mask's box moves the mask: it is what was picked.
    const bool through = grip == Grip::Body && m_wasSelected
                      && !(QGuiApplication::keyboardModifiers() & Qt::ControlModifier);
    if (const Mask* m = pickedMask(); through && m && chromeBox().contains(scenePos)) {
        m_dragBody  = m->id;
        m_dragStart = m_composition;
        return;
    }
    m_dragBody = through ? bodyAt(mapFromScene(scenePos)) : 0;
    if (m_dragBody != 0) {
        m_dragStart = m_composition;
        emit bodyPressed(m_uid, m_dragBody);
    }
}

void Object::dragTo(const QPointF& scenePos, Qt::KeyboardModifiers modifiers)
{
    if (m_active == Grip::None)
        return;

    const QPointF delta = scenePos - m_startScenePos;
    m_moved = true;

    if (m_active == Grip::Body && m_dragBody != 0) {
        // The body and the tails leaving it, by the delta in the object's own units.
        Composition   next = m_dragStart;
        const QPointF by   = delta / (scale() > 0.0 ? scale() : 1.0);
        if (next.mask && next.mask->id == m_dragBody) {
            for (Body& r : next.mask->regions)
                r.place.pos += by;
        } else {
            Chain::moveBody(next, m_dragBody, by);
        }
        m_dragReported = true;
        emit compositionEdited(m_uid, next);
        refreshBounds();
        update();
        return;
    }

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
    // in the item's own, which differ by the item's scale (a chapter re-profiled). Snapping to one item
    // unit keeps the box whole and the anchored side exactly where it was.
    const qreal k = scale() > 0.0 ? scale() : 1.0;
    const ResizeRule rule = resizeRule(k, keepsAspect(), modifiers.testFlag(Qt::ShiftModifier),
                                       modifiers.testFlag(Qt::ControlModifier));
    const std::optional<QRectF> r = resizedRect(m_startRect, m_active, delta, rule);
    if (!r)
        return;   // refused, not clamped: the box keeps its last good size

    if (m_resizeBody != 0) {
        // The bubble alone: its place and size in the object's units; the object itself stays where it is.
        Composition  next = m_dragStart;
        const QRectF to(mapFromScene(r->topLeft()), QSizeF(qRound(r->width() / k), qRound(r->height() / k)));
        if (next.mask && next.mask->id == m_resizeBody) {
            resizeMask(*next.mask, to);
        } else {
            Chain::resizeBody(next, m_resizeBody, to);
        }
        m_dragReported = true;
        emit compositionEdited(m_uid, next);
        refreshBounds();
        update();
        return;
    }

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
    m_wasSelected = isSelected();
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
