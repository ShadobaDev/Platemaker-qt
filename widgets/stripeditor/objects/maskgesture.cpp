#include "objects/maskgesture.hpp"

#include "objects/objectcontroller.hpp"
#include "objects/seethrough.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QPen>
#include <QWidget>

namespace StripEdit {

namespace {

constexpr qreal k_gestureZ     = 1000.0;   //!< Above everything, as a placement's band.
constexpr int   k_previewAlpha = 70;       //!< The area drawn see-through, so what it covers shows.

QGraphicsPathItem* seeThrough(QGraphicsScene* scene)
{
    auto* item = new SeeThrough;
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setZValue(k_gestureZ);
    item->hide();
    scene->addItem(item);
    return item;
}

//! A hairline in @p colour, whatever the zoom, within the path's box (SeeThrough).
QPen hairline(const QColor& colour)
{
    QPen pen(colour, 0.0);
    pen.setCosmetic(true);
    return pen;
}

//! The ring of a brush @p size across, round its centre.
QPainterPath ringOf(qreal size)
{
    QPainterPath ring;
    ring.addEllipse(QPointF(), size / 2.0, size / 2.0);
    return ring;
}

} // namespace

MaskGesture::MaskGesture(QGraphicsScene* scene, ObjectController& objects, const QWidget* palette, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_objects(objects)
    , m_colour(palette->palette().color(QPalette::Highlight))
{
}

QGraphicsPathItem* MaskGesture::ring()
{
    if (!m_ring) {
        m_ring = seeThrough(m_scene);
        m_ring->setPen(hairline(m_colour));
        m_ring->setPath(ringOf(m_size));
    }
    return m_ring;
}

QGraphicsPathItem* MaskGesture::preview()
{
    if (!m_preview) {
        m_preview = seeThrough(m_scene);
        QColor see = m_colour;
        see.setAlpha(k_previewAlpha);
        m_preview->setBrush(see);
    }
    return m_preview;
}

void MaskGesture::forgetItems()
{
    m_ring    = nullptr;
    m_preview = nullptr;
    m_points.clear();
}

void MaskGesture::setStroke(MaskStroke stroke)
{
    if (stroke == m_stroke)
        return;
    cancel();
    m_stroke = stroke;
    if (stroke != MaskStroke::Brush)
        leave();   // only the brush has a ring; the others have the cross for a cursor
}

void MaskGesture::setSize(qreal px)
{
    m_size = px;
    if (m_ring)
        m_ring->setPath(ringOf(px));
}

void MaskGesture::setArmed(bool armed)
{
    m_armed = armed;
    if (armed)
        return;
    cancel();
    leave();
}

void MaskGesture::hover(const QPointF& scenePos)
{
    if (!m_armed || m_stroke != MaskStroke::Brush)
        return;
    if (!m_objects.canPaintMask()) {   // nothing to paint: no ring, the pointer is the cursor
        leave();
        return;
    }
    ring()->setPos(scenePos);
    m_ring->show();
}

void MaskGesture::leave()
{
    if (m_ring)
        m_ring->hide();
}

void MaskGesture::press(const QPointF& scenePos, qreal closeReach)
{
    if (isDrawingPolygon()) {
        if (m_points.size() > 1 && QLineF(m_points.first(), m_pointer).length() <= closeReach) {
            close();
            return;
        }
        m_points.append(m_pointer);   // where the open edge ran, snapped as it was drawn
        show();
        return;
    }
    // A gesture repaints the regions: nodes picked in shape editing would be another region's.
    m_objects.shapeEdit().setOn(false);
    m_points  = {scenePos};
    m_pointer = scenePos;
    m_box     = {};
    preview()->setPen(m_stroke == MaskStroke::Brush ? QPen(Qt::NoPen) : hairline(m_colour));
    show();
}

void MaskGesture::moveTo(const QPointF& scenePos, bool square, bool fromCentre)
{
    if (!isDrawing())
        return;
    switch (m_stroke) {
    case MaskStroke::Brush:
    case MaskStroke::Freehand:
        if (m_points.last() == scenePos)
            return;
        m_points.append(scenePos);
        break;
    case MaskStroke::Rectangle:
    case MaskStroke::Ellipse:
        m_points = {m_points.first(), scenePos};
        m_box    = {square, fromCentre};
        break;
    case MaskStroke::Polygon:
        m_pointer = square ? snappedTo45(m_points.last(), scenePos) : scenePos;
        break;
    }
    show();
}

void MaskGesture::release()
{
    if (isDrawing() && m_stroke != MaskStroke::Polygon)
        finish();
}

void MaskGesture::close()
{
    if (isDrawingPolygon())
        finish();
}

void MaskGesture::undoCorner()
{
    if (!isDrawingPolygon())
        return;
    m_points.removeLast();
    if (m_points.isEmpty())
        cancel();
    else
        show();
}

void MaskGesture::cancel()
{
    m_points.clear();
    if (m_preview)
        m_preview->hide();
}

QPolygonF MaskGesture::drawn() const
{
    if (m_stroke != MaskStroke::Polygon)
        return m_points;
    QPolygonF corners = m_points;
    corners.append(m_pointer);
    return corners;
}

void MaskGesture::show()
{
    QPainterPath shown = maskArea(m_stroke, drawn(), m_size, m_box);
    if (m_stroke == MaskStroke::Polygon || m_stroke == MaskStroke::Freehand)
        if (shown.isEmpty())   // two points: the edge so far, a line with nothing inside yet
            shown.addPolygon(drawn());
    preview()->setPath(shown);
    m_preview->show();
}

void MaskGesture::finish()
{
    // A Polygon closes on its corners: the pointer is where the closing click was, on a corner already.
    const QPainterPath covered = maskArea(m_stroke, m_points, m_size, m_box);
    cancel();
    if (!covered.isEmpty())
        m_objects.paintMask(covered, m_adds);   // last: the feed it causes may clear the scene
}

}  // namespace StripEdit
