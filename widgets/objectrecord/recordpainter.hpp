/**
 * \file recordpainter.hpp
 * \brief Drawing a bubble on a QPainter — the canvas preview, the row glyphs and the rasterised image.
 *
 * Only the drawing: what is drawn — the silhouette, the lettering and the room they take — is the
 * record's geometry (geometry.hpp), which the SVG writer uses too. This header includes it, so a caller
 * that draws has both.
 *
 * Nothing here knows about overlays, projects or the strip. It takes an ObjectRecord and draws it at the
 * origin; where that lands is the caller's business.
 */

#ifndef RECORDPAINTER_HPP
#define RECORDPAINTER_HPP

#include <QBrush>
#include <QImage>
#include <QPainterPath>

#include "geometry.hpp"
#include "objectrecord.hpp"

class QPainter;

namespace Painter {

/**
 * @brief Draws \p a into the rectangle (0, 0, a.box), on whatever painter is given.
 *
 * The scene preview and the PNG both go through here, so "what you see" and "what is baked" cannot
 * drift apart — they are the same code path over the same numbers.
 *
 * @param painter The painter to draw into.
 * @param a The record to draw.
 */
void paint(QPainter& painter, const ObjectRecord& a);

//! What a record is drawn from, resolved once: a caller that repaints keeps it (BubbleObject).
struct Paths
{
    QPainterPath silhouette;   //!< The balloon and its tails (Geometry::silhouette()).
    QPainterPath shadow;       //!< Geometry::shadowOf(); empty with none.
    QPainterPath fillTone;     //!< A fading fill's marks (Geometry::fillTone()); empty for most.
    QPainterPath ink;          //!< The line's own strokes (Geometry::ink()); empty for most.
    QPainterPath text;         //!< The lettering as glyph outlines (Geometry::textOutline()).
};

//! \p a's paths, resolved.
[[nodiscard]] Paths resolve(const ObjectRecord& a);

/**
 * @brief Draws \p a from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, a halftone lays thousands of marks,
 * and any shape with text lays out a document and builds glyph outlines — while a repaint happens on
 * every scroll, zoom and selection change. A caller that holds the paths draws through here; paint() is
 * the one-shot form.
 */
void paintPaths(QPainter& painter, const ObjectRecord& a, const Paths& paths);

/**
 * @brief Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
 * @param a The record to rasterise.
 * @return The rasterised image.
 */
[[nodiscard]] QImage render(const ObjectRecord& a);

/**
 * @brief What @p p paints with: its colour, or a pattern's tile (Geometry::patternCell()) laid from the
 *        painter's origin, the box's corner, as the SVG's `<pattern>` is.
 */
[[nodiscard]] QBrush paintBrush(const Paint& p);

} // namespace Painter

#endif // RECORDPAINTER_HPP
