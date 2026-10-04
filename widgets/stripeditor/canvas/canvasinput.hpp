#ifndef STRIPEDIT_CANVASINPUT_HPP
#define STRIPEDIT_CANVASINPUT_HPP

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QString>

class QGraphicsView;
class QMimeData;

namespace StripEdit {

class ObjectController;
class Placement;
struct Tool;

/**
 * @brief What a press, a drag, a drop or a wheel on the strip does — routed by the armed tool.
 *
 * The viewport's event filter. Everything that is the **canvas's** own it does: the middle-button pan
 * under every tool, the cursor (a promise about what a press would do here, so re-decided while
 * hovering), and a Create tool's drag, which goes straight to the objects. What belongs to another region
 * it **reports**: the colour pair is the rail's, zoom is the editor's, and a picture dropped from the
 * TOOL OPTIONS preview or a file manager is placed by whoever owns the import. The Editor connects those.
 *
 * Selecting, moving and dragging a handle are not here: they are what the objects do under every tool,
 * on the presses this lets through.
 */
class CanvasInput : public QObject
{
    Q_OBJECT

public:
    //! Filters @p view's viewport. @p objects answers what is under the pointer; @p placement takes a
    //! Create tool's drag.
    CanvasInput(QGraphicsView* view, ObjectController* objects, Placement* placement, QObject* parent);

    //! The armed tool. Kept as the registry's row, which outlives this.
    void setTool(const Tool& tool);

    /**
     * @brief Re-decides the viewport cursor for the tool and whatever the pointer is over.
     *
     * Called on hover, after a press is released, when the tool changes, when the zoom changes and after
     * a feed — every moment at which either half of *(tool, target)* can have moved, including the ones
     * where the pointer itself did not.
     *
     * **Nothing else sets the viewport cursor.** The view's drag mode still writes one of its own, and
     * `cursorFor()` answers the same cursor in that state so the two agree rather than take turns.
     */
    void updateCursor();

signals:
    void artworkDropped(const QString& file, QPointF scenePos);   //!< A picture, at its own size.
    void sampleRequested(QPointF scenePos, bool secondary);       //!< The colour picker pressed here.
    void colourApplyRequested(QPointF scenePos, bool secondary);  //!< The bucket pressed here.
    void zoomStepRequested(int direction);                        //!< Ctrl+wheel: +1 in, -1 out.

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    //! The picture @p mime carries, by what it *is*: one of the suffixes the import takes. Empty if none.
    [[nodiscard]] static QString droppedArtwork(const QMimeData* mime);

    QGraphicsView*    m_view    = nullptr;
    ObjectController* m_objects   = nullptr;
    Placement*        m_placement = nullptr;
    const Tool*       m_tool    = nullptr;
    QPoint            m_panFrom {-1, -1};      //!< Last middle-button point, viewport coordinates; -1 when none.
    QPoint            m_pointerPos {-1, -1};   //!< Last hovered viewport point, so the cursor can be
                                               //!< re-decided when the pointer has not moved but the
                                               //!< scene under it has.
};

}  // namespace StripEdit

#endif // STRIPEDIT_CANVASINPUT_HPP
