#ifndef STRIPEDIT_OVERLAYITEM_H
#define STRIPEDIT_OVERLAYITEM_H

#include <QGraphicsObject>
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include "textartifact.h"

namespace StripEdit {

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
     * An asset can carry artwork without carrying parameters — drawn elsewhere, or edited outside
     * Platemaker. Drawing the file itself keeps the overlay visible and movable instead of replacing it
     * with an empty default; it just cannot be re-typed. Pass a null pixmap to go back to drawing the
     * artifact.
     */
    void setFallbackPixmap(const QPixmap& pm);

    /**
     * @brief Shows \p img — the library's own rasterisation of this bubble — instead of the local paths.
     *
     * How a styled bubble is previewed. Its style is an SVG filter, so the effect exists only once
     * librsvg has rasterised it; Qt implements neither feTurbulence nor feDisplacementMap and would
     * quietly draw the unfiltered outline. Rather than approximate it, the editor asks the library for
     * the same pixels the render will bake.
     *
     * Dropped whenever the artifact changes, so a stale rendering is never shown as current; the owner
     * supplies a fresh one after the edit has settled and been written. A null image means "draw the
     * paths", which is what an unstyled bubble always does.
     */
    void setSharpRaster(const QImage& img);

    //! Greys the item out and stops interaction: its anchor page is not in the strip (see the list).
    void setOrphaned(bool orphaned);
    [[nodiscard]] bool isOrphaned() const { return m_orphaned; }

    //! True while drawing an asset that carries no authoring parameters (see setFallbackPixmap()).
    [[nodiscard]] bool isFlatAsset() const { return !m_fallback.isNull(); }

    /**
     * @brief What this item actually draws, in item coordinates — origin may be negative.
     *
     * The offset between the item's position (the balloon's top-left) and the artwork's own top-left,
     * which is what `StripOverlay::x/y` records. A tail pointing up or left pushes the artwork above or
     * left of the balloon; a flat asset has no such offset because its pixmap starts at the origin.
     */
    [[nodiscard]] QRectF contentBounds() const { return m_bounds; }

    QRectF boundingRect() const override;
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    //! A move / resize / tail drag has settled — the owner reads pos() and artifact() and persists them.
    void geometryEdited(const QString& uid);

    /**
     * @brief A **flat asset** was resized — the owner rewrites the artwork to \p size.
     *
     * Reported separately from geometryEdited() because there is nowhere else for the size to go: a flat
     * asset has no authoring record, and \c StripOverlay has no width or height — an asset's own pixel
     * dimensions *are* its rendered size. So the size lives in the file, and changing it means rewriting
     * the file.
     */
    void artworkResized(const QString& uid, QSize size);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;

private:
    //! What the press landed on. `Body` falls through to the base class's move handling.
    enum class Grip { None, Body, TopLeft, TopRight, BottomLeft, BottomRight, Tail };

    //! Which grip is under \p local. For Grip::Tail, \p tailIndex receives which tail it belongs to.
    [[nodiscard]] Grip   gripAt(const QPointF& local, int* tailIndex = nullptr) const;
    [[nodiscard]] QRectF gripRect(Grip g) const;
    //! Handle for tail \p i, centred on its tip — which may sit well outside the balloon.
    [[nodiscard]] QRectF tailGripRect(int i) const;

    QString      m_uid;
    TextArtifact m_artifact;
    QImage       m_sharp;      //!< Library rasterisation shown at rest for a styled bubble (see above).
    QPixmap      m_fallback;   //!< Non-null when the authoring record is missing (see setFallbackPixmap).
    QRectF       m_bounds;      //!< Cached content bounds — see refreshBounds().
    QPainterPath m_silhouette;  //!< Cached balloon + tails, so a repaint resolves no geometry.
    QPainterPath m_textPath;    //!< Cached glyph outlines, likewise.
    Platemaker::Models::BlendMode m_blend = Platemaker::Models::BlendMode::Over;

    /**
     * @brief Recomputes \c m_bounds, announcing a geometry change if it moved.
     *
     * Called wherever the artifact or the fallback changes. Deriving the bounds on demand would be
     * correct but ruinous: boundingRect() is called on every paint and every scene index update, while
     * artifactBounds() lays out a text document and builds glyph outlines.
     */
    void refreshBounds();

    Grip    m_active = Grip::None;   //!< Grip being dragged (None = not resizing/aiming).
    int     m_activeTail = -1;       //!< Which tail is being aimed, while m_active == Grip::Tail.
    QRectF  m_startRect;             //!< Scene rect at press — resizing works against it, not per-delta.
    QPointF m_startScenePos;         //!< Cursor at press, in scene coordinates.
    bool    m_moved    = false;      //!< Whether this press actually changed anything worth reporting.
    bool    m_orphaned = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OVERLAYITEM_H
