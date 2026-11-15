#include "objects/bubbleobject.hpp"
#include "recordpainter.hpp"
#include "styles.hpp"
#include "handles.hpp"

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



} // namespace

BubbleObject::BubbleObject(QString uid, Composition composition, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    m_composition = std::move(composition);
    rebuild();
}

QString BubbleObject::label() const
{
    return objectLabel(m_composition);
}

void BubbleObject::rebuild()
{
    // Resolve once, here, and keep the paths: paint() must not rebuild an eleven-circle union or re-lay
    // a text document on every scroll and selection change.
    m_paths     = Painter::resolve(m_composition);
    m_tailDrawn = m_composition.balloon ? Geometry::tailsDrawn(*m_composition.balloon, m_composition.seed)
                                        : QList<bool>();
    refreshBounds();
}

QRectF BubbleObject::computeBounds() const
{
    return Geometry::boundsOf(m_composition, m_paths.silhouette, m_paths.text);
}

void BubbleObject::setComposition(const Composition& c)
{
    // The same values are not an edit: there is nothing to re-resolve, and the rasterisation still
    // describes them. A settled edit is written here a second time, by the commit that follows the
    // preview it was already drawn from, and re-laying an eleven-circle union and a text document for
    // it would be work for a picture that cannot change.
    if (c == m_composition)
        return;

    // Whatever was rasterised described the previous composition. Showing it now would be showing an edit
    // that has not happened; the owner hands over a fresh one once this one settles.
    m_sharp       = QImage();
    m_composition = c;
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
    const QSize oldBox = sizeOf(m_composition);
    if (newBox == oldBox && handleShift.isNull())
        return;

    // **A tip stays where it points.** It is aimed at a speaker, and resizing the balloon does not move
    // the speaker — so the tip keeps its place on the strip, which in the balloon's own units means
    // moving back by however far the box's corner moved. Only the tail's *width* follows the balloon:
    // a tail that kept its thickness would swamp a shrinking bubble.
    const qreal widthScale = oldBox.isEmpty()
        ? 1.0
        : (double(newBox.width()) / oldBox.width() + double(newBox.height()) / oldBox.height()) / 2.0;
    Composition next = m_composition;
    if (next.balloon)
        for (BalloonTail& t : next.balloon->tails) {
            t.tail.tip       += handleShift;
            t.tail.baseWidth *= widthScale;
        }
    resize(next, newBox);
    emit compositionEdited(uid(), next);
}

bool BubbleObject::handleDrawn(int index) const
{
    return m_tailDrawn.value(index, false);
}

int BubbleObject::handleCount() const
{
    return m_composition.balloon ? int(m_composition.balloon->tails.size()) : 0;
}

QPointF BubbleObject::handlePos(int index) const
{
    if (index < 0 || index >= handleCount())
        return {};
    return m_composition.balloon->tails.at(index).tail.tip;
}

void BubbleObject::setHandlePos(int index, const QPointF& local)
{
    if (index < 0 || index >= handleCount())
        return;
    Composition next                    = m_composition;
    next.balloon->tails[index].tail.tip = local;
    emit compositionEdited(uid(), next);
}

bool BubbleObject::canEditShape() const
{
    // A patterned fill has its fade's cross, and words their text area's arrows, so a balloon
    // with no handles of its own is edited for those.
    const Body*  body  = bodyOf(m_composition);
    const Words* words = wordsOf(m_composition);
    return body
        && (Geometry::hasShapeHandles(body->shape) || m_composition.balloon->style.fill.fill.kind != Paint::Kind::Solid
            || (words && !words->text.body.isEmpty()));
}

qreal BubbleObject::radiusClearance() const
{
    return gripSpanHere() * k_radiusDotClearance;
}

Geometry::HandleContext BubbleObject::handleContext() const
{
    return {radiusClearance(), pickedVertices()};
}

QList<Geometry::Handle> BubbleObject::shapeHandles() const
{
    return canEditShape() ? Geometry::handlesOf(m_composition, handleContext()) : QList<Geometry::Handle>();
}

QPainterPath BubbleObject::editOutline() const
{
    const Body* own = bodyOf(m_composition);
    if (!own)
        return {};
    return Geometry::outlineOf(*own, Geometry::balloonRect(*m_composition.balloon, *own), m_composition.seed);
}

bool BubbleObject::beginShapeDrag(int index)
{
    const QList<Geometry::Handle> handles = shapeHandles();
    if (index < 0 || index >= handles.size())
        return false;
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
    const bool nodeItself = m_shapeHandle.glyph == Geometry::Handle::Glyph::Node;
    QList<int> picked     = pickedVertices();
    if (nodeItself && !changed
        && QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        m_shapeDragOff = picked.removeAll(node) > 0;
        if (!m_shapeDragOff)
            picked.append(node);
        setPickedVertices(picked);
    } else if (nodeItself && picked.contains(node)) {
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
    if (m_shapeDragOff || !m_shapeHandle.drag)
        return;   // a Ctrl+press that took its node out of the pick

    // The handle's owner changes its element, and says how far that moved the composition's frame.
    Composition   next  = m_shapeStart;
    const QPointF moved = m_shapeHandle.drag(next, m_shapeHandle, to, handleContext());
    emit compositionEdited(uid(), next);
    setPos(m_shapeStartPos + moved * k);
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
    Painter::paintPaths(painter, m_composition, m_paths);
}

}  // namespace StripEdit
