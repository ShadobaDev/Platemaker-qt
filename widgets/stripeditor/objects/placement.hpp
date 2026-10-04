#ifndef STRIPEDIT_PLACEMENT_HPP
#define STRIPEDIT_PLACEMENT_HPP

#include <QObject>
#include <QPointF>
#include <QString>

#include <functional>

#include "objectrecord.hpp"

class QGraphicsRectItem;
class QGraphicsScene;
class QWidget;

namespace StripEdit {

class ObjectController;
class StripLayout;

/**
 * @brief A Create tool's drag on empty strip: the rubber band, and the object it places.
 *
 * The canvas starts, moves and ends it (CanvasInput); this draws the band and, on release, asks the
 * ObjectController for the new object — a balloon or lettering built from the prototype TOOL OPTIONS
 * holds, or the picture the Artwork tool is armed with. Which one is the tool's business, not this
 * class's: it is given a picture or it places balloons.
 *
 * Creation itself is the library's: it mints the uid, hashes the asset and dedups identical content, so
 * the owner finishes it and feeds the result back, where it gets selected.
 */
class Placement : public QObject
{
public:
    /**
     * @param objects  Where a finished placement goes, and what a bare click deselects.
     * @param palette  Whose highlight colour the band is drawn in — the editor's.
     */
    Placement(QGraphicsScene* scene, ObjectController& objects, const StripLayout& layout,
              const QWidget* palette, QObject* parent);

    //! What a balloon placement is built from: TOOL OPTIONS' prototype. Without one, nothing is placed.
    void setPrototype(std::function<ObjectRecord()> prototype) { m_prototype = std::move(prototype); }

    //! The picture the next placement puts down; empty, and a placement makes a balloon.
    void setArtwork(const QString& file) { m_placementArtwork = file; }

    [[nodiscard]] bool isPlacing() const { return m_placing; }   //!< A drag is in flight.

    void begin(const QPointF& scenePos);    //!< Starts the drag, with the band at the press point.
    void update(const QPointF& scenePos);   //!< Moves the band's far corner to @p scenePos.
    /**
     * @brief Ends the drag: places the object the band describes, or — for a click — deselects.
     *
     * Only a drag creates one. Letting a bare click create one made every click on the artwork a
     * placement, including the click that just deselects the bubble you finished.
     */
    void finish();

private:
    QGraphicsScene*      m_scene = nullptr;
    ObjectController&    m_objects;
    const StripLayout&   m_layout;
    const QWidget*       m_palette = nullptr;
    std::function<ObjectRecord()> m_prototype;

    QGraphicsRectItem* m_placementRubber = nullptr;   //!< Rubber band while a new bubble is drawn.
    QPointF            m_placementOrigin;             //!< Where that drag started, in scene coordinates.
    QString            m_placementArtwork;            //!< Empty: a placement makes a balloon.
    bool               m_placing = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PLACEMENT_HPP
