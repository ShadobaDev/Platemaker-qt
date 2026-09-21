#ifndef STRIPEDIT_OBJECT_H
#define STRIPEDIT_OBJECT_H

#include <QGraphicsObject>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include "textartifact.h"

namespace StripEdit {

/**
 * @brief One thing placed on the strip — whatever kind it is.
 *
 * Everything an author does to an object is the same for every kind of object: select it, move it,
 * drag a corner, mute it, delete it, put it in front of another. All of that lives here, once. What a
 * *kind* of object adds is only what it draws, how big that drawing is, and any extra handle it offers
 * (a bubble's tail tip).
 *
 * This exists because the alternative was tested and failed. There used to be one item class that drew
 * either a bubble or an imported asset depending on whether a pixmap had been handed to it, and the
 * rest of the editor re-derived the same distinction from "is there an authoring record for this uid" —
 * eight separate places. Two of them were written wrong and shipped: one loaded a *default* bubble into
 * the panel for imported artwork, the other stored that default back over the artwork. Both are the
 * same mistake, and neither is expressible once the two kinds are two types.
 *
 * The item's position is the object's top-left in scene coordinates, and the scene is the strip at 1:1,
 * so `pos()` converts to the library's placement by subtracting the anchor page's top.
 *
 * Geometry edits are reported on **mouse release**, not while dragging: the owner persists each one as
 * an undo step, and a per-pixel undo history would be unusable.
 */
class Object : public QGraphicsObject
{
    Q_OBJECT

public:
    //! What an object is. Kept minimal on purpose — it exists for the few places that must ask.
    enum class Kind { Bubble, Asset };

    //! What a press or a hover landed on. `Body` falls through to this class's own move handling.
    enum class Grip { None, Body, TopLeft, TopRight, BottomLeft, BottomRight, Handle };

    explicit Object(QString uid, QGraphicsItem* parent = nullptr);

    [[nodiscard]] const QString& uid() const { return m_uid; }
    [[nodiscard]] virtual Kind    kind() const = 0;
    //! One line naming this object for the object list.
    [[nodiscard]] virtual QString label() const = 0;

    /**
     * @brief The authoring record this object carries — **every kind has one**.
     *
     * A balloon's record is everything about it; a picture's says which file it is and what words are
     * over it. Both were the same accessor declared twice, once per subclass, so every caller that
     * wanted a record had to prove which subclass it was holding first — and the write paths that only
     * proved one of them silently dropped the other.
     *
     * A record here is **never** a stand-in for "this object has none". The map this used to be read
     * from returns a default *speech balloon* for a uid it does not hold, and that one value standing
     * for two different things is the mechanism behind four shipped defects. Each kind keeps its record
     * describing what it actually is, so asking any object is always safe.
     */
    [[nodiscard]] const TextArtifact& artifact() const { return m_artifact; }

    //! Adopts \p a and repaints. What that costs — re-resolving paths, a resize — is the kind's own.
    virtual void setArtifact(const TextArtifact& a) = 0;

    //! How this object blends onto the strip — mapped to the matching QPainter composition mode.
    void setBlend(Platemaker::Models::BlendMode blend);

    //! Greys the object out and stops interaction: its anchor page is not in the strip (see the list).
    void setOrphaned(bool orphaned);
    [[nodiscard]] bool isOrphaned() const { return m_orphaned; }

    //! Whether the drag that just ended was reported through dragging() — so others may have travelled.
    [[nodiscard]] bool reportedDrag() const { return m_dragReported; }

    //! Places handle @p index at @p local and rebuilds — how an owner moves a tail that is not the one
    //! under the mouse. The object itself uses the same path when its own handle is dragged.
    void moveHandle(int index, const QPointF& local);

    //! Where handle @p index sits, in this object's own units. Public for the same reason as moveHandle().
    [[nodiscard]] QPointF handleAt(int index) const { return handlePos(index); }

    //! Marks handle @p index as its tail's — the one selected — or none with -1. Drawn hollow.
    void setFocusedHandle(int index);

    //! What sits under @p scenePos: a corner grip, a tail handle, the body, or nothing of this object.
    //! Public because the cursor is decided in one place now, and that place is not this class.
    [[nodiscard]] Grip gripAtScene(const QPointF& scenePos, int* handleIndex = nullptr) const;

    /**
     * @brief Turns selection chrome off for **every** object, for one repaint.
     *
     * The box, the grips and the tail handles are the editor talking, not the comic. A repaint that has
     * to answer *what does the page look like* — the eyedropper's, which samples one composited pixel —
     * asks for them to be left out, because sampling a balloon that happens to be selected must give its
     * fill and not the highlight colour.
     */
    static void setChromeVisible(bool on);

    /**
     * @brief What this object actually draws, in item coordinates — origin may be negative.
     *
     * The offset between the item's position (the box's top-left) and the artwork's own top-left, which
     * is what the library's record stores. A bubble's tail pointing up or left pushes the artwork above
     * or left of the box; imported artwork has no such offset, because its pixmap starts at the origin.
     */
    [[nodiscard]] QRectF contentBounds() const { return m_bounds; }

    /**
     * @brief The rectangle the corner grips move — the object's own size, before any tail.
     *
     * Not stored here: a bubble's box *is* its artifact's, and holding a second copy is how the two
     * drift apart. Each kind answers from wherever its size actually lives.
     */
    [[nodiscard]] virtual QSizeF boxSize() const = 0;

    QRectF boundingRect() const override;

    /**
     * @brief What is actually clickable: the box, the handles, and the grips while selected.
     *
     * Qt's default is `boundingRect()`, and this class's bounding rect is the drawn extent padded for
     * grips — for a bubble that is one rectangle enclosing the balloon *and* wherever its tail points,
     * most of which is empty. So a click in the blank space beside a tail selected the bubble, and with
     * two that overlap, the upper one's rectangle reached down over the lower one's body and swallowed
     * presses meant for it.
     *
     * The box is the hit area because the box is the object: a balloon is inscribed in it, a text block
     * fills it, artwork is drawn into it. The tail *tips* are added because they are grabbable, and the
     * corner grips while selected, because they sit outside the box and a selected object must stay
     * resizable. The tail's shaft is deliberately left out — it is a thin sliver a long way from
     * anything anyone is aiming at.
     */
    QPainterPath shape() const override;

    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) final;

signals:
    //! A move / resize / handle drag has settled — the owner reads pos() and the object and persists it.
    void geometryEdited(const QString& uid);

    /**
     * @brief The object was pressed — on handle @p handle, or on anything else when it is -1.
     *
     * Emitted after the base class has done its selecting, so the owner can narrow a selection it has
     * already heard about: a press on a tail's handle selects that tail, and a press on the balloon while
     * one of its tails is selected selects the balloon again.
     */
    void pressed(const QString& uid, int handle);

    /**
     * @brief This object is being dragged — @p delta from where the press landed, in scene units.
     *
     * Emitted per mouse-move while a drag is live, *after* this object has placed itself. @p handle is
     * the tail being aimed, or -1 for a move of the body.
     *
     * The object moves itself and nothing else: **who else travels is the selection's business**, and the
     * selection belongs to the controller. An object that reached for its neighbours would need to know
     * what is selected, and a tail of some *other* balloon is not something it could reach at all.
     */
    void dragging(const QString& uid, const QPointF& delta, int handle);

protected:

    //! Draws the object itself, in item coordinates. Chrome and blending are the base class's job.
    virtual void paintContent(QPainter& painter) = 0;

    //! The extent this object draws into, recomputed whenever its geometry changes.
    [[nodiscard]] virtual QRectF computeBounds() const = 0;

    //! Applies a new box size. A kind that has to move other things with it (tails) does that here.
    virtual void setBoxSize(QSizeF size) = 0;

    /**
     * @brief True when a corner drag must scale uniformly rather than distort.
     *
     * Imported artwork says yes: its size travels as a single width fraction and its height follows
     * the artwork's own aspect, so a distorted one is not expressible — which is also what anyone
     * dragging the corner of a logo meant.
     */
    [[nodiscard]] virtual bool keepsAspect() const { return false; }

    //! Extra draggable points in item coordinates — a bubble's tail tips. None by default.
    [[nodiscard]] virtual int     handleCount() const { return 0; }
    [[nodiscard]] virtual QPointF handlePos(int index) const { Q_UNUSED(index) return {}; }
    virtual void                  setHandlePos(int index, const QPointF& local) { Q_UNUSED(index) Q_UNUSED(local) }

    //! Recomputes the drawn extent from computeBounds(), announcing a geometry change if it moved.
    void refreshBounds();

    /**
     * @brief True while a grip is being dragged.
     *
     * A kind that shows a pre-rendered image at rest has to fall back to drawing itself here: the image
     * cannot be re-made per mouse-move, and the paths follow the mouse.
     */
    [[nodiscard]] bool isDragging() const { return m_active != Grip::None; }

    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

    //! Written by each kind's setArtifact(), read by everyone through artifact(). See above.
    TextArtifact m_artifact;

private:
    //! Which grip is under \p local. For Grip::Handle, \p handleIndex receives which one.
    [[nodiscard]] Grip   gripAt(const QPointF& local, int* handleIndex = nullptr) const;
    [[nodiscard]] QRectF gripRect(Grip g) const;
    [[nodiscard]] QRectF handleRect(int index) const;

    QString m_uid;
    QRectF  m_bounds;                //!< Cached content bounds — see refreshBounds().
    Platemaker::Models::BlendMode m_blend = Platemaker::Models::BlendMode::Over;

    Grip    m_active = Grip::None;   //!< Grip being dragged (None = not resizing/aiming).
    int     m_activeHandle = -1;     //!< Which handle is being dragged, while m_active == Grip::Handle.
    QRectF  m_startRect;             //!< Scene rect at press — resizing works against it, not per-delta.
    QPointF m_startScenePos;         //!< Cursor at press, in scene coordinates.
    bool    m_moved    = false;      //!< Whether this press actually changed anything worth reporting.
    bool    m_orphaned = false;
    int     m_focusedHandle = -1;    //!< The selected tail's handle, drawn hollow; -1 for none.

    bool m_dragReported = false;   //!< A drag was live, so the owner may have moved others along.

    static bool s_chromeVisible;     //!< Off while something samples what is drawn. See setChromeVisible().
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECT_H
