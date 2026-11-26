#ifndef STRIPEDIT_MASKGESTURE_HPP
#define STRIPEDIT_MASKGESTURE_HPP

#include "objects/maskarea.hpp"

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
 * \brief A mask tool's gesture on the strip: the brush's ring under the pointer, the area shown while it is drawn,
 *        and, once drawn, the area handed to the objects (ObjectController::paintMask()).
 *
 * The canvas starts, moves and ends it (CanvasInput), as it does a placement. Nothing is merged while it is drawn:
 * the area is merged once, on release, or for a Polygon when it is closed.
 */
class MaskGesture : public QObject
{
public:
    //! @p palette: whose highlight colour the ring and the area are drawn in, the editor's.
    MaskGesture(QGraphicsScene* scene, ObjectController& objects, const QWidget* palette, QObject* parent);

    void setStroke(MaskStroke stroke);           //!< How the area is drawn; a gesture in flight is dropped.
    void setAdds(bool adds) { m_adds = adds; }   //!< The area hides more (Add); false, less (Subtract).
    void setSize(qreal px);                      //!< The brush's diameter, in strip pixels.

    //! Armed, the ring follows the pointer over the strip; disarmed, it is hidden and a gesture in flight dropped.
    void setArmed(bool armed);
    void hover(const QPointF& scenePos);   //!< The pointer is at @p scenePos: the ring goes there.
    void leave();                          //!< The pointer left the strip: the ring is hidden.
    /**
     * \brief Drops its items without touching them: called before the scene is cleared, which deletes them. They
     *        are made again when next drawn; a gesture in flight is dropped.
     */
    void forgetItems();

    [[nodiscard]] bool isDrawing() const { return !m_points.isEmpty(); }   //!< A gesture is in flight.
    //! A Polygon is being drawn: it takes the clicks, Enter, Backspace and Esc until it is closed or dropped.
    [[nodiscard]] bool isDrawingPolygon() const { return isDrawing() && m_stroke == MaskStroke::Polygon; }
    /**
     * \brief A press at @p scenePos: starts a gesture, or adds a Polygon's corner. A press within @p closeReach (scene
     *        units) of a Polygon's first corner closes it.
     */
    void press(const QPointF& scenePos, qreal closeReach);
    //! The pointer moved to @p scenePos, with Shift (@p square) and Alt (@p fromCentre) as BoxDraw and a Polygon say.
    void moveTo(const QPointF& scenePos, bool square, bool fromCentre);
    void release();     //!< The button is let go: every gesture but a Polygon ends there and hands its area over.
    void close();       //!< Ends a Polygon (a double click, Enter) and hands its area over.
    void undoCorner();  //!< Takes a Polygon's last corner back (Backspace); the last one taken drops it.
    void cancel();      //!< Drops the gesture in flight (Esc): nothing is painted.

private:
    void finish();   //!< Ends the gesture and hands its area over.
    void show();     //!< The area as drawn so far, shown.
    //! The points the area is taken from: a Polygon's corners and the pointer, the rest as they are.
    [[nodiscard]] QPolygonF drawn() const;

    //! The ring, made in the scene when there is none.
    [[nodiscard]] QGraphicsPathItem* ring();
    //! The area, made in the scene when there is none.
    [[nodiscard]] QGraphicsPathItem* preview();

    QGraphicsScene*    m_scene = nullptr;
    ObjectController&  m_objects;
    QColor             m_colour;              //!< The editor's highlight: the ring's, and the area's see-through.
    QGraphicsPathItem* m_ring    = nullptr;   //!< The brush's size, round the pointer; none until drawn.
    QGraphicsPathItem* m_preview = nullptr;   //!< The area while it is drawn; none until drawn.
    QPolygonF          m_points;              //!< The gesture's points so far, in scene coordinates.
    QPointF            m_pointer;             //!< Where a Polygon's open edge runs to.
    BoxDraw            m_box;                 //!< As the modifiers held at the last move say.
    MaskStroke         m_stroke = MaskStroke::Brush;
    qreal              m_size   = 0.0;
    bool               m_adds   = true;
    bool               m_armed  = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKGESTURE_HPP
