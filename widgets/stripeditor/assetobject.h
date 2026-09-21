#ifndef STRIPEDIT_ASSETOBJECT_H
#define STRIPEDIT_ASSETOBJECT_H

#include <QPixmap>
#include <QSizeF>
#include <QString>

class QSvgRenderer;

#include "object.h"
#include "textartifact.h"

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
    /**
     * @brief Draws the picture at @p picture — **the file the artist imported**, not the file the
     *        library renders.
     *
     * Those are the same thing until the picture is lettered; after that the overlay points at a
     * generated wrapper that embeds the picture *and* bakes the words into it, and an item built from
     * that would draw the lettering twice — once from the wrapper and once itself. So it is given the
     * picture, and the words stay this class's to draw.
     */
    AssetObject(QString uid, const QString& picture, QGraphicsItem* parent = nullptr);

    //! Points it at a different file. The drawn size is kept: it is the artist's, not the file's.
    void setPicture(const QString& picture);

    [[nodiscard]] Kind    kind() const override { return Kind::Asset; }
    [[nodiscard]] QString label() const override;

    [[nodiscard]] QSizeF boxSize() const override { return m_box; }

    //! The art itself — what a list row wears, since imported artwork has no silhouette to borrow.
    //! A raster of a vector picture at its own size: the canvas draws the vector, this is for icons
    //! and for the one question a picture has to answer about itself, which is how big it is.
    [[nodiscard]] const QPixmap& artwork() const { return m_artwork; }

    /**
     * @brief The record this picture carries: which file it is, and the lettering put over it.
     *
     * Artwork has no geometry of ours, but it may have **words** — a hand-drawn balloon typeset, a
     * sound effect captioned — and the preview has to show them, or the strip stops being what the
     * render will produce. The words are laid out in the record's box, which is the picture's own
     * pixels, and then drawn through the same transform the picture is: they scale with it rather than
     * sliding about on it.
     */
    void setArtifact(const TextArtifact& a) override;

protected:
    void                 paintContent(QPainter& painter) override;
    [[nodiscard]] QRectF computeBounds() const override { return QRectF(QPointF(0, 0), m_box); }
    void                 setBoxSize(QSizeF size) override;
    [[nodiscard]] bool   keepsAspect() const override { return true; }

private:
    //! Reads @p picture into both forms: the renderer when it is vector, the pixmap always — one for
    //! the canvas, one for the row's icon and for the size the picture says it is.
    void loadPicture(const QString& picture);

    /**
     * @brief Makes the record say what this object is: this picture, at its own pixels.
     *
     * The invariant behind `Object::artifact()`. A picture placed before pictures had a record, and one
     * whose size was guessed at import, would otherwise hand back a record that reads as a default
     * *speech balloon* — the value that has caused four shipped defects. Called wherever either half
     * can change: a new file, or a record arriving from the panel. Takes the record rather than
     * working on this object's own, so an arriving one is described **before** it is compared — a
     * record that has to be repaired every time is otherwise a record that never compares equal, and
     * a repaint on every feed.
     */
    void describePicture(TextArtifact& a) const;

    QString      m_picture;   //!< The file, so a feed can tell whether it changed.
    QPixmap      m_artwork;
    /**
     * @brief Set when the picture is vector — and then it, not the pixmap, is what the canvas draws.
     *
     * A rasterised SVG is sharp at one size and soft at every other, which on a canvas that zooms is
     * every size but one. Keeping the renderer costs a parse at load and draws at whatever scale the
     * view is showing.
     */
    QSvgRenderer* m_svg = nullptr;
    //! Drawn size. Seeded from the artwork's own pixels and then owned here, so a re-feed cannot undo a
    //! resize — and on a synced drive the file may still report its previous size just after a write.
    QSizeF  m_box;
};

}  // namespace StripEdit

#endif // STRIPEDIT_ASSETOBJECT_H
