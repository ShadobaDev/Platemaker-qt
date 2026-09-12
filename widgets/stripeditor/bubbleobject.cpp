#include "bubbleobject.h"
#include "artifactpainter.h"

#include <QPainter>

#include <utility>

namespace StripEdit {

BubbleObject::BubbleObject(QString uid, TextArtifact artifact, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
    , m_artifact(std::move(artifact))
{
    rebuild();
}

QString BubbleObject::label() const
{
    return artifactLabel(m_artifact);
}

void BubbleObject::rebuild()
{
    // Resolve once, here, and keep the paths: paint() must not rebuild an eleven-circle union or re-lay
    // a text document on every scroll and selection change.
    m_silhouette = artifactSilhouette(m_artifact);
    m_textPath   = artifactTextOutline(m_artifact);
    refreshBounds();
}

QRectF BubbleObject::computeBounds() const
{
    return artifactBoundsOf(m_artifact, m_silhouette, m_textPath);
}

void BubbleObject::setArtifact(const TextArtifact& a)
{
    // Whatever was rasterised described the previous artifact. Showing it now would be showing an edit
    // that has not happened; the owner hands over a fresh one once this one settles.
    if (!(a == m_artifact))
        m_sharp = QImage();
    m_artifact = a;
    // A tail can reach outside the balloon, so the drawn extent moves for more reasons than a resize:
    // aiming one, bending it, or adding a second all change what this object covers.
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
    if (newBox == m_artifact.box)
        return;

    // Keep every tail pointing the same relative way as the balloon changes size, and scale its width
    // with it — otherwise a tail keeps its absolute thickness and swamps a shrinking bubble.
    if (!m_artifact.box.isEmpty()) {
        const qreal sx = double(newBox.width())  / m_artifact.box.width();
        const qreal sy = double(newBox.height()) / m_artifact.box.height();
        for (Tail& t : m_artifact.tails) {
            t.tip = QPointF(t.tip.x() * sx, t.tip.y() * sy);
            t.baseWidth *= (sx + sy) / 2.0;
        }
    }
    m_artifact.box = newBox;
    rebuild();
}

QPointF BubbleObject::handlePos(int index) const
{
    if (index < 0 || index >= int(m_artifact.tails.size()))
        return {};
    return m_artifact.tails.at(index).tip;
}

void BubbleObject::setHandlePos(int index, const QPointF& local)
{
    if (index < 0 || index >= int(m_artifact.tails.size()))
        return;
    m_artifact.tails[index].tip = local;
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
    paintArtifactPaths(painter, m_artifact, m_silhouette, m_textPath);
}

}  // namespace StripEdit
