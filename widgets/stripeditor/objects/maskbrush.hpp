#ifndef STRIPEDIT_MASKBRUSH_HPP
#define STRIPEDIT_MASKBRUSH_HPP

#include <QColor>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>

class QGraphicsPathItem;
class QGraphicsScene;
class QWidget;

namespace StripEdit {

class ObjectController;

/**
 * \brief The mask brush's drag on the strip: the ring under the pointer, the stroke shown while it is drawn, and on
 *        release the area it covered, handed to the objects (ObjectController::paintMask()).
 *
 * The canvas starts, moves and ends it (CanvasInput), as it does a placement. Nothing is merged while the pointer
 * moves: the stroke is drawn as the brush's track, stroked to its size, and merged once, on release.
 */
class MaskBrush : public QObject
{
public:
    //! @p palette: whose highlight colour the ring and the stroke are drawn in, the editor's.
    MaskBrush(QGraphicsScene* scene, ObjectController& objects, const QWidget* palette, QObject* parent);

    void setHide(bool hide) { m_hide = hide; }   //!< The brush hides; the eraser (false) shows.
    void setSize(qreal px);                       //!< Its diameter, in strip pixels.

    //! Armed, the ring follows the pointer over the strip; disarmed, it is hidden and a stroke in flight dropped.
    void setArmed(bool armed);
    void hover(const QPointF& scenePos);   //!< The pointer is at @p scenePos: the ring goes there.
    void leave();                          //!< The pointer left the strip: the ring is hidden.
    /**
     * \brief Drops its items without touching them: called before the scene is cleared, which deletes them. They
     *        are made again when next drawn; a stroke in flight is dropped.
     */
    void forgetItems();

    [[nodiscard]] bool isPainting() const { return !m_track.isEmpty(); }   //!< A stroke is in flight.
    void begin(const QPointF& scenePos);    //!< Starts a stroke at @p scenePos.
    void update(const QPointF& scenePos);   //!< Takes the stroke on to @p scenePos.
    void finish();                          //!< Ends the stroke and hands its area over.

private:
    //! The area the track covers, the brush's size round it; a dab for a click.
    [[nodiscard]] QPainterPath area() const;

    //! The ring, made in the scene when there is none.
    [[nodiscard]] QGraphicsPathItem* ring();
    //! The stroke, made in the scene when there is none.
    [[nodiscard]] QGraphicsPathItem* stroke();

    QGraphicsScene*    m_scene = nullptr;
    ObjectController&  m_objects;
    QColor             m_colour;             //!< The editor's highlight: the ring's, and the stroke's see-through.
    QGraphicsPathItem* m_ring   = nullptr;   //!< The brush's size, round the pointer; none until drawn.
    QGraphicsPathItem* m_stroke = nullptr;   //!< The stroke while it is drawn; none until drawn.
    QPolygonF          m_track;              //!< The pointer's points this stroke, in scene coordinates.
    qreal              m_size  = 0.0;
    bool               m_hide  = true;
    bool               m_armed = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKBRUSH_HPP
