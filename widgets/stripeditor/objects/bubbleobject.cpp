#include "objects/bubbleobject.hpp"
#include "painter.hpp"
#include "styles.hpp"
#include "handles.hpp"

#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace StripEdit {

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
    return Geometry::boundsOf(m_composition, m_paths.silhouette, m_paths.texts);
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
    return isTip(index) && m_tailDrawn.value(index, false);
}

int BubbleObject::bodyAt(const QPointF& local) const
{
    if (!m_composition.balloon || m_composition.balloon->bodies.size() < 2)
        return 0;
    const Balloon& b = *m_composition.balloon;
    // The last drawn on top, so tried first.
    for (auto body = b.bodies.crbegin(); body != b.bodies.crend(); ++body)
        if (Geometry::outlineOf(*body, Geometry::balloonRect(b, *body), m_composition.seed).contains(local))
            return body->id;
    return 0;
}

QRectF BubbleObject::bodyBox(int id) const
{
    if (m_composition.balloon)
        for (const Body& b : m_composition.balloon->bodies)
            if (b.id == id)
                return QRectF(b.place.pos, QSizeF(b.size));
    return {};
}

bool BubbleObject::isChain() const
{
    return m_composition.balloon && m_composition.balloon->bodies.size() > 1;
}

QRectF BubbleObject::frameBox() const
{
    return isChain() ? frameOf(m_composition) : Object::frameBox();
}

QPainterPath BubbleObject::hitArea() const
{
    if (!isChain())
        return Object::hitArea();
    // Every bubble's box, and the outline the bridges join them into: a press on a bridge reaches the object.
    QPainterPath p = m_paths.silhouette;
    p.setFillRule(Qt::WindingFill);
    for (const Body& b : m_composition.balloon->bodies)
        p.addRect(QRectF(b.place.pos, QSizeF(b.size)));
    return p;
}

bool BubbleObject::isTip(int index) const
{
    return index >= 0 && index < handleCount() && !m_composition.balloon->tails.at(index).isBridge();
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
    const Body* body = bodyOf(m_composition, pickedBody());
    return body
        && (Geometry::hasShapeHandles(body->shape) || m_composition.balloon->style.fill.fill.kind != Paint::Kind::Solid
            || lettered(m_composition));
}

QPainterPath BubbleObject::editOutline() const
{
    const Body* own = bodyOf(m_composition, pickedBody());
    if (!own)
        return {};
    return Geometry::outlineOf(*own, Geometry::balloonRect(*m_composition.balloon, *own), m_composition.seed);
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
