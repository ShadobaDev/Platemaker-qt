#ifndef OVERLAYITEM_H
#define OVERLAYITEM_H

#include <QGraphicsObject>
#include <QPixmap>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include "textartifact.h"

/**
 * @brief One bubble on the strip: draws a TextArtifact, and lets the author move, resize and aim it.
 *
 * Drawn from the **authoring model**, not from the rasterised PNG — so typing updates the strip with no
 * file round-trip, and the preview is the render because both go through paintArtifact().
 *
 * The item's position is the overlay's top-left in scene coordinates, and the scene is the strip at 1:1,
 * so `pos()` converts to the library's placement by subtracting the anchor page's top. That identity is
 * the reason overlays need no coordinate mapping layer of their own.
 *
 * Geometry edits are reported on **mouse release**, not while dragging: the owner persists each one as
 * an undo step, and a per-pixel undo history would be unusable.
 */
class OverlayItem : public QGraphicsObject
{
    Q_OBJECT

public:
    OverlayItem(QString uid, TextArtifact artifact, QGraphicsItem* parent = nullptr);

    [[nodiscard]] const QString&      uid() const { return m_uid; }
    [[nodiscard]] const TextArtifact& artifact() const { return m_artifact; }

    //! Adopts new authoring values and repaints (handles a box change, so it may resize the item).
    void setArtifact(const TextArtifact& a);

    //! How this overlay blends onto the strip — mapped to the matching QPainter composition mode.
    void setBlend(Platemaker::Models::BlendMode blend);

    /**
     * @brief Draws \p pm instead of the artifact — used when no authoring record exists for this overlay.
     *
     * The sidecar can be missing (never written, lost, hand-deleted) while the library's bitmap is
     * perfectly intact. Falling back to that bitmap keeps the bubble visible and movable instead of
     * replacing it with an empty default; it just cannot be re-typed until it is recreated. Pass a null
     * pixmap to go back to drawing the artifact.
     */
    void setFallbackPixmap(const QPixmap& pm);

    //! Greys the item out and stops interaction: its anchor page is not in the strip (see the list).
    void setOrphaned(bool orphaned);
    [[nodiscard]] bool isOrphaned() const { return m_orphaned; }

    QRectF boundingRect() const override;
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    //! A move / resize / tail drag has settled — the owner reads pos() and artifact() and persists them.
    void geometryEdited(const QString& uid);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;

private:
    //! What the press landed on. `Body` falls through to the base class's move handling.
    enum class Grip { None, Body, TopLeft, TopRight, BottomLeft, BottomRight, Tail };

    [[nodiscard]] Grip    gripAt(const QPointF& local) const;
    [[nodiscard]] QRectF  gripRect(Grip g) const;
    [[nodiscard]] QPointF tailPoint() const;

    QString      m_uid;
    TextArtifact m_artifact;
    QPixmap      m_fallback;   //!< Non-null when the authoring record is missing (see setFallbackPixmap).
    Platemaker::Models::BlendMode m_blend = Platemaker::Models::BlendMode::Over;

    Grip    m_active = Grip::None;   //!< Grip being dragged (None = not resizing/aiming).
    QRectF  m_startRect;             //!< Scene rect at press — resizing works against it, not per-delta.
    QPointF m_startScenePos;         //!< Cursor at press, in scene coordinates.
    bool    m_moved    = false;      //!< Whether this press actually changed anything worth reporting.
    bool    m_orphaned = false;
};

#endif // OVERLAYITEM_H
