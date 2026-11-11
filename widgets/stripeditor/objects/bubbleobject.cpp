#include "objects/bubbleobject.hpp"
#include "recordpainter.hpp"
#include "styles.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace StripEdit {

namespace {

//! The radius handle's distance in from its arc, in grip spans: its arrow, a span either side of it,
//! then stays clear of the corner's own node.
constexpr qreal k_radiusDotClearance = 2.2;

//! How far a node may sit from the box's edge, as a fraction of it, and the box still count as fitted.
constexpr qreal k_fitsBox = 1e-9;

/**
 * \brief Fits @p a's box to its outline (Geometry::outlineExtent()), in the units of @p body's box: it
 *        grows to a node pulled past it, and shrinks back when the outline comes in again.
 *
 * The nodes are re-measured against the fitted body, and the tail tips move back by as much as the box's
 * corner moved, so nothing moves on the strip but the node being dragged. A box that fits its outline
 * already, or an outline with no width or height, leave it as it is.
 *
 * \return How far the box's top-left moved, in the units it was in.
 */
QPointF fitToOutline(ObjectRecord& a, const QRectF& body)
{
    const QRectF extent = Geometry::outlineExtent(a.shape, body);
    if (extent.isNull())
        return {};
    const qreal left = extent.left(), top = extent.top(), right = extent.right(), bottom = extent.bottom();
    const bool fits = qAbs(left) <= k_fitsBox && qAbs(top) <= k_fitsBox && qAbs(right - 1.0) <= k_fitsBox
                     && qAbs(bottom - 1.0) <= k_fitsBox;
    if (fits || (right - left) * body.width() < 1.0 || (bottom - top) * body.height() < 1.0)
        return {};

    // The fitted body, and the box around it in whole pixels, the stroke's half outside it as
    // balloonRect() takes it off.
    const qreal  inset = a.style.strokeWidth / 2.0;
    const QRectF fitted(body.left() + left * body.width(), body.top() + top * body.height(),
                       (right - left) * body.width(), (bottom - top) * body.height());
    const QPointF corner(fitted.left() - inset, fitted.top() - inset);
    a.box = QSize(qCeil(fitted.width() + 2 * inset), qCeil(fitted.height() + 2 * inset));

    // A handle is an offset from its node, so it only rescales.
    const QRectF  next = Geometry::balloonRect(a);
    const QPointF scale(body.width() / next.width(), body.height() / next.height());
    for (ShapeNode& n : a.shape.nodes) {
        const QPointF at(body.left() + n.at.x() * body.width() - corner.x(),
                         body.top() + n.at.y() * body.height() - corner.y());
        n.at  = QPointF((at.x() - next.left()) / next.width(), (at.y() - next.top()) / next.height());
        n.in  = QPointF(n.in.x() * scale.x(), n.in.y() * scale.y());
        n.out = QPointF(n.out.x() * scale.x(), n.out.y() * scale.y());
    }
    for (Tail& t : a.tails.items)
        t.tip -= corner;
    return corner;
}

} // namespace

BubbleObject::BubbleObject(QString uid, ObjectRecord record, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    m_record = std::move(record);
    rebuild();
}

QString BubbleObject::label() const
{
    return recordLabel(m_record);
}

void BubbleObject::rebuild()
{
    // Resolve once, here, and keep the paths: paint() must not rebuild an eleven-circle union or re-lay
    // a text document on every scroll and selection change.
    m_paths      = Painter::resolve(m_record);
    m_tailDrawn  = Geometry::tailsDrawn(m_record);
    refreshBounds();
}

QRectF BubbleObject::computeBounds() const
{
    return Geometry::boundsOf(m_record, m_paths.silhouette, m_paths.text);
}

void BubbleObject::setRecord(const ObjectRecord& a)
{
    // The same values are not an edit: there is nothing to re-resolve, and the rasterisation still
    // describes them. A settled edit is written here a second time, by the commit that follows the
    // preview it was already drawn from, and re-laying an eleven-circle union and a text document for
    // it would be work for a picture that cannot change.
    if (a == m_record)
        return;

    // Whatever was rasterised described the previous record. Showing it now would be showing an edit
    // that has not happened; the owner hands over a fresh one once this one settles.
    m_sharp    = QImage();
    m_record = a;
    // A tail can reach outside the balloon, so the drawn extent moves for more reasons than a resize:
    // aiming one, bending it, or adding a second all change what this object covers.
    rebuild();
    update();
}

void BubbleObject::refreshFonts()
{
    m_sharp = QImage();
    rebuild();
    update();
}

void BubbleObject::setSharpRaster(const QImage& img)
{
    if (m_sharp.size() == img.size() && m_sharp.cacheKey() == img.cacheKey())
        return;
    m_sharp = img;
    update();
}

void BubbleObject::setBoxSize(QSizeF size, const QPointF& handleShift)
{
    const QSize newBox(qRound(size.width()), qRound(size.height()));
    if (newBox == m_record.box && handleShift.isNull())
        return;

    // **A tip stays where it points.** It is aimed at a speaker, and resizing the balloon does not move
    // the speaker — so the tip keeps its place on the strip, which in the balloon's own units means
    // moving back by however far the box's corner moved. Only the tail's *width* follows the balloon:
    // a tail that kept its thickness would swamp a shrinking bubble.
    const qreal widthScale = m_record.box.isEmpty()
        ? 1.0
        : (double(newBox.width()) / m_record.box.width() + double(newBox.height()) / m_record.box.height()) / 2.0;
    for (Tail& t : m_record.tails.items) {
        t.tip       += handleShift;
        t.baseWidth *= widthScale;
    }
    m_record.box = newBox;
    rebuild();
}

bool BubbleObject::handleDrawn(int index) const
{
    return m_tailDrawn.value(index, false);
}

QPointF BubbleObject::handlePos(int index) const
{
    if (index < 0 || index >= int(m_record.tails.items.size()))
        return {};
    return m_record.tails.items.at(index).tip;
}

void BubbleObject::setHandlePos(int index, const QPointF& local)
{
    if (index < 0 || index >= int(m_record.tails.items.size()))
        return;
    m_record.tails.items[index].tip = local;
    rebuild();
}

bool BubbleObject::canEditShape() const
{
    // A patterned fill has its fade's cross, and words their text area's arrows, so a balloon
    // with no handles of its own is edited for those.
    return m_record.hasSilhouette()
        && (Geometry::hasShapeHandles(m_record.shape) || m_record.skin.fill.kind != Paint::Kind::Solid
            || !m_record.text.body.isEmpty());
}

qreal BubbleObject::radiusClearance() const
{
    return gripSpanHere() * k_radiusDotClearance;
}

QList<Geometry::ShapeHandle> BubbleObject::shapeHandles() const
{
    if (!canEditShape())
        return {};
    const QRectF                 body = Geometry::balloonRect(m_record);
    QList<Geometry::ShapeHandle> out  = Geometry::shapeHandles(m_record.shape, body, radiusClearance(), pickedVertex());
    // The fill's fade last: where a dot sits on a node or a "+", the outline's own handle is the one pressed.
    out.append(Geometry::fadeHandles(m_record.skin.fill, body));
    // The text area's arrows last of all, whenever there are words.
    if (!m_record.text.body.isEmpty())
        out.append(Geometry::textAreaHandles(m_record));
    return out;
}

QPainterPath BubbleObject::editOutline() const
{
    return Geometry::outlineOf(m_record, Geometry::balloonRect(m_record));
}

bool BubbleObject::beginShapeDrag(int index)
{
    const QList<Geometry::ShapeHandle> handles = shapeHandles();
    if (index < 0 || index >= handles.size())
        return false;
    m_shapeStart    = m_record;
    m_shapeStartPos = pos();
    m_shapeHandle   = handles.at(index);

    using Role   = Geometry::ShapeHandle::Role;
    bool changed = false;
    if (m_shapeHandle.role == Role::AddVertex) {
        // A "+" adds its node at once, and the drag that follows is that node's.
        m_shapeStart.shape = Geometry::withVertexAdded(m_record.shape, m_shapeHandle.index);
        m_shapeHandle      = {Role::Vertex, m_shapeHandle.index + 1, m_shapeHandle.at};
        m_record           = m_shapeStart;
        rebuild();
        update();
        changed = true;
    }
    // A node, or one of its handles, picks that node: its handles show, and Delete or its type act on it.
    // Ctrl+press on a node adds it to the pick, or takes it away (and then drags nothing). A plain press on a
    // node already among several keeps them, so the drag moves them all.
    m_shapeDragOff = false;
    if (!Geometry::picksNode(m_shapeHandle.role)) {
        setPickedVertex(-1);
        return changed;
    }
    const int  node   = m_shapeHandle.index;
    QList<int> picked = pickedVertices();
    if (m_shapeHandle.role == Role::Vertex && !changed
        && QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        m_shapeDragOff = picked.removeAll(node) > 0;
        if (!m_shapeDragOff)
            picked.append(node);
        setPickedVertices(picked);
    } else if (m_shapeHandle.role == Role::Vertex && picked.contains(node)) {
        picked.removeAll(node);
        setPickedVertices(picked << node);   // the one pressed is the one picked last
    } else {
        setPickedVertex(node);
    }
    return changed;
}

void BubbleObject::dragShapeTo(const QPointF& scenePos)
{
    // In the units of the box as it was at the press: a node past the box moves the item, and measuring
    // against where it has moved to would chase the pointer.
    const qreal   k    = scale() > 0.0 ? scale() : 1.0;
    const QPointF to   = (scenePos - m_shapeStartPos) / k;
    const QRectF  body = Geometry::balloonRect(m_shapeStart);
    if (m_shapeDragOff)
        return;   // a Ctrl+press that took its node out of the pick

    using Role        = Geometry::ShapeHandle::Role;
    ObjectRecord next = m_shapeStart;
    if (m_shapeHandle.role == Role::FadeFrom || m_shapeHandle.role == Role::FadeTo
        || m_shapeHandle.role == Role::FadeCentre)
        next.skin.fill = Geometry::dragFadeHandle(next.skin.fill, body, m_shapeHandle, to);
    else if (m_shapeHandle.role == Role::TextSide || m_shapeHandle.role == Role::TextTop)
        next.text = Geometry::dragTextAreaHandle(m_shapeStart, m_shapeHandle, to);
    else if (m_shapeHandle.role == Role::Vertex && pickedVertices().size() > 1)
        next.shape = Geometry::withNodesMoved(next.shape, body, pickedVertices(), to - m_shapeHandle.at);
    else
        next.shape = Geometry::dragShapeHandle(next.shape, body, m_shapeHandle, to, radiusClearance());
    const QPointF moved = m_shapeHandle.role == Role::Vertex ? fitToOutline(next, body) : QPointF();
    m_record = next;
    setPos(m_shapeStartPos + moved * k);
    rebuild();
    update();
}

void BubbleObject::paintContent(QPainter& painter)
{
    // Styled: show the library's rendering, at rest and while the whole object is carried, which moves
    // it and changes nothing it draws. A drag that reshapes it (a tail, a grip, a node) shows the paths
    // instead: they follow the mouse, and a rasterisation cannot be produced per mouse-move anyway.
    if (!m_sharp.isNull() && (!isDragging() || dragGrip() == Grip::Body)) {
        painter.drawImage(contentBounds().topLeft(), m_sharp);
        return;
    }
    Painter::paintPaths(painter, m_record, m_paths);
}

}  // namespace StripEdit
