#ifndef STRIPEDIT_ARTWORKOBJECT_HPP
#define STRIPEDIT_ARTWORKOBJECT_HPP

#include <QPixmap>
#include <QSizeF>
#include <QString>

class QSvgRenderer;

#include "objects/object.hpp"
#include "composition.hpp"

namespace StripEdit {

/**
 * \brief Reads imported artwork from @p path — **raster or vector, drawn the way it will be placed**.
 *
 * Vector assets go through `QSvgRenderer` explicitly rather than through QPixmap's image plugin: the
 * plugin path depends on qsvg being deployed and gives no control over the size it picks. Raster assets
 * load the ordinary way, so a hand-supplied PNG keeps working.
 *
 * It lives here because it is the same question this class answers — *what does that file look like* —
 * and both the object on the strip and the tool's preview must answer it identically. It was private to
 * the object controller until the Artwork tool needed to show the artist what they picked.
 * 
 * \param path The file to read, which may be a raster or vector image.
 * \return A pixmap of the artwork at its own pixels, or null if the file
 */
[[nodiscard]] QPixmap loadArtwork(const QString& path);

/**
 * \brief Artwork the author made elsewhere, placed as it is: a balloon inked on a tablet, a logo.
 *
 * It carries no authoring parameters, so there is nothing to re-type — and, being its own type, nothing
 * can hand it a default bubble by mistake. That used to be possible: the old single item class returned
 * a default bubble for imported artwork, and two call sites persisted it, replacing the artwork with a
 * blank balloon.
 *
 * Its size is a width and the artwork's own aspect, which is why a corner drag scales it uniformly:
 * the library record stores one width fraction, so a distorted one is not expressible.
 */
class ArtworkObject : public Object
{
    Q_OBJECT

public:
    /**
     * \brief Draws the picture at @p picture — **the file the artist imported**, not the file the
     *        library renders.
     *
     * Those are the same thing until the picture is lettered; after that the overlay points at a
     * generated wrapper that embeds the picture *and* bakes the words into it, and an item built from
     * that would draw the lettering twice — once from the wrapper and once itself. So it is given the
     * picture, and the words stay this class's to draw.
     * 
     * \param uid The unique identifier for this object, which is the same as the composition it carries.
     * \param picture The file the artist imported, which is what this object draws.
     */
    ArtworkObject(QString uid, const QString& picture, QGraphicsItem* parent = nullptr);

    /**
     * \brief Points it at a different file. The drawn size is kept: it is the artist's, not the file's.
     * \param picture The new file to display.
     */
    void setPicture(const QString& picture);

    [[nodiscard]] Kind    kind() const override { return Kind::Artwork; } //!< The only kind of object that carries artwork.
    [[nodiscard]] QString label() const override; //!< The file name, for the object list.

    [[nodiscard]] QSizeF boxSize() const override { return m_box; } //!< The artwork's own pixels, which is what the library draws.

    /**
     * \brief The art itself — what a list row wears, since imported artwork has no silhouette to borrow.
     * \return A reference to the rasterised picture, for icons and for the size the picture says it is.
     */
    [[nodiscard]] const QPixmap& artwork() const { return m_artwork; }

    /**
     * \brief The composition this picture is: which file it is, and the lettering put over it.
     *
     * Artwork has no geometry of ours, but it may have **words** — a hand-drawn balloon typeset, a
     * sound effect captioned — and the preview has to show them, or the strip stops being what the
     * render will produce. The words are laid out in the picture's size, which is its own
     * pixels, and then drawn through the same transform the picture is: they scale with it rather than
     * sliding about on it.
     * 
     * \param c The composition this picture is, which is always safe to ask for.
     */
    void setComposition(const Composition& c) override;

    //! @p c made to say what this object is: this picture, at its own pixels, with words over it (describePicture()).
    [[nodiscard]] Composition described(Composition c) const
    {
        describePicture(c);
        return c;
    }

    //! A picture scales as one number, so it keeps its aspect and is offered no edge grips.
    [[nodiscard]] bool resizesByScale() const override { return true; }

protected:
    /**
     * \brief Draws the picture and any lettering over it, at the artwork's own pixels.
     * \param painter The painter to use for drawing.
     */
    void                 paintContent(QPainter& painter) override;
    /**
     * \brief Computes the bounding rectangle for the object.
     * \return The bounding rectangle.
     */
    [[nodiscard]] QRectF computeBounds() const override { return QRectF(QPointF(0, 0), m_box); }
    /**
     * \brief Sets the size of the object's bounding box.
     * \param size The new size.
     */
    void                 setBoxSize(QSizeF size, const QPointF& handleShift) override;

private:
    /**
     * \brief Reads @p picture into both forms: the renderer when it is vector, the pixmap always — one for
     * the canvas, one for the row's icon and for the size the picture says it is.
     * \param picture The file path of the picture to load.
     */
    void loadPicture(const QString& picture);

    /**
     * \brief Makes the composition say what this object is: this picture, at its own pixels, with words over it.
     *
     * The invariant behind `Object::composition()`. A picture placed before pictures had a composition, and one
     * whose size was guessed at import, would otherwise hand back a composition that reads as a default
     * *speech balloon* — the value that has caused four shipped defects. Called wherever either half
     * can change: a new file, or a composition arriving from the panel. Takes the composition rather than
     * working on this object's own, so an arriving one is described **before** it is compared — a
     * composition that has to be repaired every time is otherwise a composition that never compares equal, and
     * a repaint on every feed.
     * 
     * \param c The composition to describe as this picture, at its own pixels.
     */
    void describePicture(Composition& c) const;

    QString      m_picture;     //!< The file, so a feed can tell whether it changed.
    QPixmap      m_artwork;     //!< The picture at its own pixels, for the row's icon and for the size the picture says it is.
    /**
     * \brief Set when the picture is vector — and then it, not the pixmap, is what the canvas draws.
     *
     * A rasterised SVG is sharp at one size and soft at every other, which on a canvas that zooms is
     * every size but one. Keeping the renderer costs a parse at load and draws at whatever scale the
     * view is showing.
     */
    QSvgRenderer* m_svg = nullptr;
    /**
     * \brief Drawn size. Seeded from the artwork's own pixels and then owned here, so a re-feed cannot undo a resize — and on a synced drive the file may still report its previous size just after a write.
     */
    QSizeF  m_box;
};

}  // namespace StripEdit

#endif // STRIPEDIT_ARTWORKOBJECT_HPP
