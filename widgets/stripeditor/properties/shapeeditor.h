#ifndef STRIPEDIT_SHAPEEDITOR_H
#define STRIPEDIT_SHAPEEDITOR_H

#include <QPalette>
#include <QPixmap>

#include "propertygroupeditor.h"

class QButtonGroup;

namespace StripEdit {

//! The size every generated bubble thumbnail comes out at — the shape tiles' and the preset combo's.
inline constexpr int k_bubbleThumbW = 34;
inline constexpr int k_bubbleThumbH = 26;

/**
 * @brief A miniature of \p shape, drawn by the very rasteriser that draws the real bubble.
 *
 * Reusing it means a tile cannot misrepresent its shape, and it costs no icon assets: the picker is
 * generated, not drawn by hand. The caller chooses the colours, because a shape tile is UI chrome (it
 * wears the palette) while a preset's tile is a swatch of the preset itself.
 */
[[nodiscard]] QPixmap bubbleThumbnail(TextArtifact::Shape shape, const QColor& fill,
                                      const QColor& stroke, const QColor& ink);

//! The shape picker's tiles: UI chrome, so they wear the palette rather than three white blobs.
[[nodiscard]] QPixmap shapeThumbnail(TextArtifact::Shape shape, const QPalette& pal);

/**
 * @brief Whether a shape normally speaks.
 *
 * Shared by the tile previews and by picking one, so a tile cannot promise a shape that placing it
 * does not give you.
 */
[[nodiscard]] bool shapeSpeaks(TextArtifact::Shape shape);

/**
 * @brief The shape picker: one checkable tile per shape, laid out like the editor's tool rail.
 *
 * A grid of previews rather than a drop-down, because a bubble shape is a *look* — a name in a list
 * makes you open it to find out what it is. Each tile's icon is produced by the same rasteriser that
 * draws the bubble, so the tile is a true miniature of what placing it gives you.
 */
class ShapeEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit ShapeEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Shape; }

    void bind(const Subjects& subjects) override;
    void applyTo(TextArtifact& target) const override;

    [[nodiscard]] const ShapeProperties& values() const { return m_values; }

protected:
    //! Re-renders the tiles when the theme flips — they are drawn in the palette's colours.
    void changeEvent(QEvent* e) override;

private:
    void refreshTiles();

    QButtonGroup*   m_tiles = nullptr;
    ShapeProperties m_values;
};

}  // namespace StripEdit

#endif // STRIPEDIT_SHAPEEDITOR_H
