#include "objects/bubbleobject.hpp"
#include "recordpainter.hpp"

#include <QPainter>

#include <utility>

namespace StripEdit {

BubbleObject::BubbleObject(QString uid, ObjectRecord record, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    m_record = std::move(record);
    rebuild();
}

QString BubbleObject::label() const
{
    return Painter::label(m_record);
}

void BubbleObject::rebuild()
{
    // Resolve once, here, and keep the paths: paint() must not rebuild an eleven-circle union or re-lay
    // a text document on every scroll and selection change.
    m_silhouette = Painter::silhouette(m_record);
    m_textPath   = Painter::textOutline(m_record);
    refreshBounds();
}

QRectF BubbleObject::computeBounds() const
{
    return Painter::boundsOf(m_record, m_silhouette, m_textPath);
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

void BubbleObject::setBoxSize(QSizeF size)
{
    const QSize newBox(qRound(size.width()), qRound(size.height()));
    if (newBox == m_record.box)
        return;

    // Keep every tail pointing the same relative way as the balloon changes size, and scale its width
    // with it — otherwise a tail keeps its absolute thickness and swamps a shrinking bubble.
    if (!m_record.box.isEmpty()) {
        const qreal sx = double(newBox.width())  / m_record.box.width();
        const qreal sy = double(newBox.height()) / m_record.box.height();
        for (Tail& t : m_record.tails.items) {
            t.tip = QPointF(t.tip.x() * sx, t.tip.y() * sy);
            t.baseWidth *= (sx + sy) / 2.0;
        }
    }
    m_record.box = newBox;
    rebuild();
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

void BubbleObject::paintContent(QPainter& painter)
{
    if (!m_sharp.isNull() && !isDragging()) {
        // At rest, and styled: show the library's rendering. Mid-drag the paths are used instead — they
        // follow the mouse, and a rasterisation cannot be produced per mouse-move anyway.
        painter.drawImage(contentBounds().topLeft(), m_sharp);
        return;
    }
    Painter::paintPaths(painter, m_record, m_silhouette, m_textPath);
}

}  // namespace StripEdit
