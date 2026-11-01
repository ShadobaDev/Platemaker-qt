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

/**
 * @brief Draws \p a from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, and any shape with text lays out a
 * document and builds glyph outlines — while a repaint happens on every scroll, zoom and selection
 * change. A caller that holds the paths draws through here; paint() is the one-shot form.
 *
 * @param painter The painter to draw into.
 * @param a The record to draw.
 * @param silhouette The silhouette path (Geometry::silhouette()).
 * @param ink The style's own strokes (Geometry::ink()), filled in the stroke colour; empty for most.
 * @param text The text path (Geometry::textOutline()).
 */
void paintPaths(QPainter& painter, const ObjectRecord& a, const QPainterPath& silhouette,
                const QPainterPath& ink, const QPainterPath& text);

/**
 * @brief Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
 * @param a The record to rasterise.
 * @return The rasterised image.
 */
[[nodiscard]] QImage render(const ObjectRecord& a);

/**
 * @brief What @p s fills a balloon with: its colour, or a pattern's tile (Geometry::toneCell()) laid from
 *        the painter's origin, the box's corner, as the SVG's `<pattern>` is.
 */
[[nodiscard]] QBrush fillBrush(const SkinProperties& s);

} // namespace Painter

#endif // RECORDPAINTER_HPP
