#ifndef STRIPEDIT_BUBBLEOBJECT_HPP
#define STRIPEDIT_BUBBLEOBJECT_HPP

#include <QImage>
#include <QPainterPath>

#include "object.hpp"
#include "artifact.hpp"

namespace StripEdit {

/**
 * @brief A bubble Platemaker authored: a \c Artifact drawn from its parameters.
 *
 * Drawn from the **authoring model**, not from the file it will be written to — so typing updates the
 * strip with no round-trip, and the preview is the render because both go through paintArtifact().
 *
 * Its tails are the object's handles: a tail tip is a draggable point that may sit well outside the
 * balloon, which is why the drawn extent is computed rather than taken from the box.
 */
class BubbleObject : public Object
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a new bubble object.
     * @param uid The unique identifier for the object.
     * @param artifact The artifact representing the bubble's properties.
     * @param parent The parent graphics item.
     */
    BubbleObject(QString uid, Artifact artifact, QGraphicsItem* parent = nullptr);

    /**
     * @brief Returns the kind of the object.
     * @return The kind of the object.
     */
    [[nodiscard]] Kind    kind() const override { return Kind::Bubble; }
    /**
     * @brief Returns the label for the object.
     * @return The label for the object.
     */
    [[nodiscard]] QString label() const override;

    /**
     * @brief Adopts new authoring values and repaints (handles a box change, so it may resize the object).
     * @param a The new artifact to set.
     */
    void setArtifact(const Artifact& a) override;

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
     * 
     * @param img The library's rasterisation of this bubble, or a null image to show the local paths.
     */
    void setSharpRaster(const QImage& img);

    /**
     * @brief Returns the size of the bubble's box.
     * @return The size of the bubble's box.
     */
    [[nodiscard]] QSizeF boxSize() const override { return QSizeF(m_artifact.box); }

protected:
    /**
     * @brief Paints the bubble object.
     * @param painter The painter to use for painting.
     */
    void                  paintContent(QPainter& painter) override;
    /**
     * @brief Computes the bounding rectangle of the bubble object.
     * @return The bounding rectangle of the bubble object.
     */
    [[nodiscard]] QRectF  computeBounds() const override;
    /**
     * @brief Sets the size of the bubble's box.
     * @param size The new size.
     */
    void                  setBoxSize(QSizeF size) override;

    /**
     * @brief Returns the number of handles for the bubble object.
     * @return The number of handles for the bubble object.
     */
    [[nodiscard]] int     handleCount() const override { return int(m_artifact.tails.items.size()); }
    /**
     * @brief Returns the position of a handle for the bubble object.
     * @param index The index of the handle.
     * @return The position of the handle.
     */
    [[nodiscard]] QPointF handlePos(int index) const override;
    /**
     * @brief Sets the position of a handle for the bubble object.
     * @param index The index of the handle.
     * @param local The new position of the handle.
     */
    void                  setHandlePos(int index, const QPointF& local) override;

private:
    void rebuild();             //!< Re-resolves the cached paths after the artifact changed, then re-measures the extent.

    QImage       m_sharp;       //!< Library rasterisation shown at rest for a styled bubble (see above).
    QPainterPath m_silhouette;  //!< Cached balloon + tails, so a repaint resolves no geometry.
    QPainterPath m_textPath;    //!< Cached glyph outlines, likewise.
};

}  // namespace StripEdit

#endif // STRIPEDIT_BUBBLEOBJECT_HPP
