#ifndef STRIPEDIT_SHAPEEDITOR_HPP
#define STRIPEDIT_SHAPEEDITOR_HPP

#include <QPalette>
#include <QPixmap>

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QButtonGroup;
class QComboBox;
class QFormLayout;

namespace StripEdit {

//! The size every generated bubble thumbnail comes out at — the shape tiles' and the preset combo's.
inline constexpr int k_bubbleThumbW = 34;
inline constexpr int k_bubbleThumbH = 26;

/**
 * @brief A miniature of \p shape, drawn by the very rasteriser that draws the real bubble.
 *
 * Reusing it means a tile cannot misrepresent its shape, and it costs no icon assets: the picker is
 * generated, not drawn by hand. The caller chooses the colours, because a shape tile is UI chrome (it
 * wears the palette) while a preset's tile is a swatch of the preset itself. A shape that speaks
 * (Geometry::shapeSpeaks()) is drawn with a tail, so a tile cannot promise a shape that placing it does
 * not give you.
 */
[[nodiscard]] QPixmap bubbleThumbnail(const ShapeProperties& shape, const QColor& fill,
                                      const QColor& stroke, const QColor& ink);

//! The shape picker's tiles: UI chrome, so they wear the palette rather than three white blobs.
[[nodiscard]] QPixmap shapeThumbnail(const ShapeProperties& shape, const QPalette& pal);

/**
 * @brief The shape picker: one checkable tile per template, laid out like the editor's tool rail, and
 *        the rows of the kind's parameters, on a widget of their own (parameters()).
 *
 * A grid of previews rather than a drop-down, because a bubble shape is a *look* — a name in a list
 * makes you open it to find out what it is. Each tile's icon is produced by the same rasteriser that
 * draws the bubble, so the tile is a true miniature of what placing it gives you. A tile is current
 * while the shape equals its template; a parameter edited off every template leaves none current.
 */
class ShapeEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit ShapeEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Shape; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;

    [[nodiscard]] const ShapeProperties& values() const { return m_values; }

    /**
     * @brief The kind's parameter rows (the corner, the lobes, the edge), apart from the tiles: the panel
     *        lays them out where it wants them (ObjectState: under the sections). Not in this widget's layout.
     */
    [[nodiscard]] QWidget* parameters() const { return m_parameters; }

signals:
    /**
     * @brief A tile was picked, before the changed() it also brings.
     *
     * What the tails follow: picking a speaking shape grows a tail, picking a caption drops it. Editing a
     * parameter is changed() alone, so turning a caption's corners does not give it a tail.
     */
    void picked();

protected:
    //! Re-renders the tiles when the theme flips — they are drawn in the palette's colours.
    void changeEvent(QEvent* e) override;

private:
    void refreshTiles();
    //! Checks the tile whose template the shape is, or none, and shows the kind's rows.
    void syncFromValues();

    QButtonGroup*               m_tiles = nullptr;
    QWidget*                    m_parameters = nullptr;
    QFormLayout*                m_form  = nullptr;
    FieldRows<ShapeProperties>  m_corner;   //!< The corner radius, for a kind that is rounded.
    FieldRows<ShapeProperties>  m_lobes;    //!< A Cloud's lobes and their size.
    QComboBox*                  m_edge = nullptr;   //!< The edge's modulation, any family.
    FieldRows<ShapeProperties>  m_edgeRows;         //!< Its spacing, depth and irregularity.
    ShapeProperties             m_values;
    bool                        m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_SHAPEEDITOR_HPP
