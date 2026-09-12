#ifndef STRIPEDIT_BUBBLEOBJECT_H
#define STRIPEDIT_BUBBLEOBJECT_H

#include <QImage>
#include <QPainterPath>

#include "object.h"
#include "textartifact.h"

namespace StripEdit {

/**
 * @brief A bubble Platemaker authored: a \c TextArtifact drawn from its parameters.
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
    BubbleObject(QString uid, TextArtifact artifact, QGraphicsItem* parent = nullptr);

    [[nodiscard]] Kind    kind() const override { return Kind::Bubble; }
    [[nodiscard]] QString label() const override;

    [[nodiscard]] const TextArtifact& artifact() const { return m_artifact; }

    //! Adopts new authoring values and repaints (handles a box change, so it may resize the object).
    void setArtifact(const TextArtifact& a);

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

    [[nodiscard]] QSizeF boxSize() const override { return QSizeF(m_artifact.box); }

protected:
    void                  paintContent(QPainter& painter) override;
    [[nodiscard]] QRectF  computeBounds() const override;
    void                  setBoxSize(QSizeF size) override;

    [[nodiscard]] int     handleCount() const override { return int(m_artifact.tails.size()); }
    [[nodiscard]] QPointF handlePos(int index) const override;
    void                  setHandlePos(int index, const QPointF& local) override;

private:
    //! Re-resolves the cached paths after the artifact changed, then re-measures the extent.
    void rebuild();

    TextArtifact m_artifact;
    QImage       m_sharp;       //!< Library rasterisation shown at rest for a styled bubble (see above).
    QPainterPath m_silhouette;  //!< Cached balloon + tails, so a repaint resolves no geometry.
    QPainterPath m_textPath;    //!< Cached glyph outlines, likewise.
};

}  // namespace StripEdit

#endif // STRIPEDIT_BUBBLEOBJECT_H
