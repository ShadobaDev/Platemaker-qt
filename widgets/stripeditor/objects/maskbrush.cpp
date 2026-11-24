#include "objects/maskbrush.hpp"

#include "objects/objectcontroller.hpp"
#include "objects/seethrough.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPathStroker>
#include <QPen>
#include <QWidget>

namespace StripEdit {

namespace {

constexpr qreal k_brushZ      = 1000.0;   //!< Above everything, as a placement's band.
constexpr int   k_strokeAlpha = 70;       //!< The stroke drawn see-through, so what it covers shows.

QGraphicsPathItem* seeThrough(QGraphicsScene* scene)
{
    auto* item = new SeeThrough;
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setZValue(k_brushZ);
    item->hide();
    scene->addItem(item);
    return item;
}

//! The ring of a brush @p size across, round its centre.
QPainterPath ringOf(qreal size)
{
    QPainterPath ring;
    ring.addEllipse(QPointF(), size / 2.0, size / 2.0);
    return ring;
}

} // namespace

MaskBrush::MaskBrush(QGraphicsScene* scene, ObjectController& objects, const QWidget* palette, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_objects(objects)
    , m_colour(palette->palette().color(QPalette::Highlight))
{
}

QGraphicsPathItem* MaskBrush::ring()
{
    if (!m_ring) {
        m_ring = seeThrough(m_scene);
        QPen pen(m_colour, 0.0);   // a hairline, within the path's box (SeeThrough)
        pen.setCosmetic(true);
        m_ring->setPen(pen);
        m_ring->setPath(ringOf(m_size));
    }
    return m_ring;
}

QGraphicsPathItem* MaskBrush::stroke()
{
    if (!m_stroke) {
        m_stroke = seeThrough(m_scene);
        QColor see = m_colour;
        see.setAlpha(k_strokeAlpha);
        m_stroke->setPen(Qt::NoPen);
        m_stroke->setBrush(see);
    }
    return m_stroke;
}

void MaskBrush::forgetItems()
{
    m_ring   = nullptr;
    m_stroke = nullptr;
    m_track.clear();
}

void MaskBrush::setSize(qreal px)
{
    m_size = px;
    if (m_ring)
        m_ring->setPath(ringOf(px));
}

void MaskBrush::setArmed(bool armed)
{
    m_armed = armed;
    if (armed)
        return;
    m_track.clear();
    if (m_stroke)
        m_stroke->hide();
    if (m_ring)
        m_ring->hide();
}

void MaskBrush::hover(const QPointF& scenePos)
{
    if (!m_armed)
        return;
    if (!m_objects.canPaintMask()) {   // nothing to paint: no ring, the pointer is the cursor
        leave();
        return;
    }
    ring()->setPos(scenePos);
    m_ring->show();
}

void MaskBrush::leave()
{
    if (m_ring)
        m_ring->hide();
}

void MaskBrush::begin(const QPointF& scenePos)
{
    // A stroke repaints the regions: nodes picked in shape editing would be another region's.
    m_objects.shapeEdit().setOn(false);
    m_track = {scenePos};
    stroke()->setPath(area());
    m_stroke->show();
}

void MaskBrush::update(const QPointF& scenePos)
{
    if (m_track.isEmpty() || m_track.last() == scenePos)
        return;
    m_track.append(scenePos);
    stroke()->setPath(area());
}

void MaskBrush::finish()
{
    const QPainterPath covered = area();
    m_track.clear();
    if (m_stroke)
        m_stroke->hide();
    m_objects.paintMask(covered, m_hide);   // last: the feed it causes may clear the scene
}

QPainterPath MaskBrush::area() const
{
    QPainterPath out;
    if (m_track.isEmpty())
        return out;
    if (m_track.size() == 1) {
        out.addEllipse(m_track.first(), m_size / 2.0, m_size / 2.0);
        return out;
    }
    out.moveTo(m_track.first());
    for (qsizetype i = 1; i < m_track.size(); ++i)
        out.lineTo(m_track[i]);
    QPainterPathStroker stroker;
    stroker.setWidth(m_size);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(out);
}

}  // namespace StripEdit
