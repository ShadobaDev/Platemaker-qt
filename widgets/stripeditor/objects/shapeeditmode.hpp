#ifndef STRIPEDIT_SHAPEEDITMODE_HPP
#define STRIPEDIT_SHAPEEDITMODE_HPP

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <functional>
#include <optional>

#include "composition.hpp"
#include "propertygroups.hpp"   // ShapeNode::Type

class QGraphicsScene;

namespace StripEdit {

class Object;
class SelectionChrome;
class SelectionModel;

/**
 * \brief Shape editing: one selected object's outline edited by its handles, with its own input (picking nodes
 *        by a band or the arrows, leaving by a double click) and its own operations (a node's type, its
 *        handles, *Convert to Curve*, deleting nodes).
 *
 * A mode beside the normal one, not the controller's: it asks the controller only for what it shares — the
 * objects, the selection, the scene — and hands its edits back to be committed.
 */
class ShapeEditMode : public QObject
{
    Q_OBJECT

public:
    //! What the mode needs from whoever owns the objects.
    struct Host
    {
        std::function<Object*(const QString& uid)>                              object;   //!< The object of @p uid, or none.
        std::function<const SelectionChrome*()>                                 chrome;   //!< The grips drawn, or none yet.
        std::function<void(const QString& uid)>                                 select;   //!< Selects one object.
        //! Commits @p c as object @p uid's, one history step called @p step, and shows it in OBJECT STATE.
        std::function<void(const QString& uid, const Composition& c, const QString& step)> commit;
    };

    ShapeEditMode(const SelectionModel& selection, QGraphicsScene* scene, Host host, QObject* parent = nullptr);

    /**
     * \brief Shape editing of the one selected object: on or off.
     *
     * On only for one selected object whose shape has handles (Object::canEditShape()); asking for it
     * otherwise does nothing. A double click on the strip leaves it (leaveAt()), and so does the selection
     * becoming anything else (in OBJECT STACK), Esc, or the object losing its handles. While it is on, Delete
     * removes the picked nodes instead of the object (deletePickedNodes()).
     */
    void setOn(bool on);
    [[nodiscard]] bool           isOn() const { return !m_edited.isEmpty(); }
    [[nodiscard]] const QString& edited() const { return m_edited; }   //!< The edited object's uid; empty for none.
    //! Whether shape editing can be switched on for the selection as it is.
    [[nodiscard]] bool canEdit() const;
    //! Whether a node is picked, so Delete would remove it.
    [[nodiscard]] bool hasPickedNode() const;
    //! The type of the Curve's node picked last; none when no Curve's node is picked.
    [[nodiscard]] std::optional<ShapeNode::Type> pickedNodeType() const;
    //! Makes every picked Curve node a @p type node (Geometry::withNodeType()), as one history step.
    void setPickedNodeType(ShapeNode::Type type);
    //! Whether a picked Curve node lacks a handle on a straight side, so *Add handles* has work.
    [[nodiscard]] bool canGrowPickedHandles() const;
    //! Gives every picked Curve node its missing handles (Geometry::withHandlesGrown()), as one history step.
    void growPickedHandles();
    /**
     * \brief Removes the picked nodes, the highest first so the others keep their numbers; a polygon keeps three.
     * \return Whether there were nodes picked: then Delete was theirs, whatever it removed.
     */
    bool deletePickedNodes();

    /**
     * \brief *Convert to Curve*: the one selected balloon's outline, of any other kind, as the Curve that
     *        draws it (Geometry::curveOf()), one history step; then edits its shape, which is what the
     *        conversion is for.
     */
    void convertToCurve();
    //! Whether convertToCurve() has something to do.
    [[nodiscard]] bool canConvertToCurve() const;

    // --- on the strip, driven by CanvasInput --------------------------------------------------------------
    //! Whether a press at @p scenePos is on the edited object: one of its handles or grips, or its body.
    [[nodiscard]] bool onEditedObject(const QPointF& scenePos, const QTransform& deviceTransform) const;
    //! Leaves shape editing for a double click at @p scenePos; on another object, that object is selected.
    void leaveAt(const QPointF& scenePos, const QTransform& deviceTransform);
    //! Picks the edited object's nodes inside @p sceneRect, instead of the pick or, with @p add, added to it.
    //! With none inside, nothing changes.
    void pickNodesIn(const QRectF& sceneRect, bool add);
    //! What an arrow does to the pick (stepPick()).
    enum class PickStep { Move, Extend, Shrink };
    /**
     * \brief Walks the pick one node round the outline, clockwise as seen on screen or not. The furthest
     *        picked node that way is the end of the run of picked nodes that holds the one picked last.
     *
     * Move picks the node past it alone, Extend adds that node, Shrink takes the furthest one away (undoing
     * an Extend the same way) but never the last one left. Nothing picked, nothing moves.
     */
    void stepPick(bool clockwise, PickStep how);

signals:
    //! On or off, the picked nodes, or the edited shape changed: the menu's entries and the handles drawn.
    void changed();

private:
    const SelectionModel& m_selection;
    QGraphicsScene*       m_scene = nullptr;
    Host                  m_host;
    QString               m_edited;   //!< The object whose shape is being edited; empty for none.
};

}  // namespace StripEdit

#endif // STRIPEDIT_SHAPEEDITMODE_HPP
