#ifndef STRIPEDIT_ASSETOBJECT_H
#define STRIPEDIT_ASSETOBJECT_H

#include <QPixmap>
#include <QSizeF>

#include "object.h"

namespace StripEdit {

/**
 * @brief Reads imported artwork from @p path — **raster or vector, drawn the way it will be placed**.
 *
 * Vector assets go through `QSvgRenderer` explicitly rather than through QPixmap's image plugin: the
 * plugin path depends on qsvg being deployed and gives no control over the size it picks. Raster assets
 * load the ordinary way, so a hand-supplied PNG keeps working.
 *
 * It lives here because it is the same question this class answers — *what does that file look like* —
 * and both the object on the strip and the tool's preview must answer it identically. It was private to
 * the object controller until the Artwork tool needed to show the artist what they picked.
 */
[[nodiscard]] QPixmap loadArtwork(const QString& path);

/**
 * @brief Artwork the author made elsewhere, placed as it is: a balloon inked on a tablet, a logo.
 *
 * It carries no authoring parameters, so there is nothing to re-type — and, being its own type, nothing
 * can hand it a default bubble by mistake. That used to be possible: the old single item class returned
 * a default \c TextArtifact for imported artwork, and two call sites persisted it, replacing the artwork
 * with a blank balloon.
 *
 * Its size is a width and the artwork's own aspect, which is why a corner drag scales it uniformly:
 * the library record stores one width fraction, so a distorted one is not expressible.
 */
class AssetObject : public Object
{
    Q_OBJECT

public:
    AssetObject(QString uid, QPixmap artwork, QGraphicsItem* parent = nullptr);

    [[nodiscard]] Kind    kind() const override { return Kind::Asset; }
    [[nodiscard]] QString label() const override;

    [[nodiscard]] QSizeF boxSize() const override { return m_box; }

    //! The art itself — what a list row wears, since imported artwork has no silhouette to borrow.
    [[nodiscard]] const QPixmap& artwork() const { return m_artwork; }

protected:
    void                 paintContent(QPainter& painter) override;
    [[nodiscard]] QRectF computeBounds() const override { return QRectF(QPointF(0, 0), m_box); }
    void                 setBoxSize(QSizeF size) override;
    [[nodiscard]] bool   keepsAspect() const override { return true; }

private:
    QPixmap m_artwork;
    //! Drawn size. Seeded from the artwork's own pixels and then owned here, so a re-feed cannot undo a
    //! resize — and on a synced drive the file may still report its previous size just after a write.
    QSizeF  m_box;
};

}  // namespace StripEdit

#endif // STRIPEDIT_ASSETOBJECT_H
