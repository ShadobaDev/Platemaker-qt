/**
 * \file recordpainter.hpp
 * \brief Drawing a bubble on a QPainter — the canvas preview, the row glyphs and the rasterised image.
 *
 * Only the drawing: what is drawn, and in what order, is the record's render list (renderlist.hpp), which
 * the SVG writer serialises from the same items. This header includes the geometry, so a caller that draws
 * has both.
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
#include "renderlist.hpp"

class QPainter;

namespace Painter {

/**
 * \brief Draws \p a into the rectangle (0, 0, a.box), on whatever painter is given.
 *
 * The scene preview and the PNG both go through here, so "what you see" and "what is baked" cannot
 * drift apart — they are the same code path over the same numbers.
 *
 * \param painter The painter to draw into.
 * \param a The record to draw.
 */
void paint(QPainter& painter, const ObjectRecord& a);

//! What a record is drawn from, resolved once: a caller that repaints keeps it (BubbleObject).
struct Paths
{
    QPainterPath silhouette;   //!< The balloon and its tails (Geometry::silhouette()).
    QPainterPath text;         //!< The lettering as glyph outlines (Geometry::textOutline()).
    Render::List balloon;      //!< Render::balloon() of the silhouette.
    Render::List words;        //!< Render::lettering() of the text.
};

//! \p a's paths, resolved.
[[nodiscard]] Paths resolve(const ObjectRecord& a);

/**
 * \brief Draws \p a from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, a halftone lays thousands of marks,
 * and any shape with text lays out a document and builds glyph outlines — while a repaint happens on
 * every scroll, zoom and selection change. A caller that holds the paths draws through here; paint() is
 * the one-shot form.
 */
void paintPaths(QPainter& painter, const ObjectRecord& a, const Paths& paths);

/**
 * \brief Draws @p a's lettering, @p text (Geometry::textOutline()), with its @p shadow
 *        (Geometry::textShadowOf()) and its outline, in the box's coordinates: Render::lettering() played,
 *        over a balloon (paintPaths()) and over a picture alike.
 */
void paintLettering(QPainter& painter, const ObjectRecord& a, const QPainterPath& text,
                    const Geometry::ShadowPaths& shadow);

/**
 * \brief Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
 * \param a The record to rasterise.
 * \return The rasterised image.
 */
[[nodiscard]] QImage render(const ObjectRecord& a);

/**
 * \brief What @p p paints with: its colour, or a pattern's tile (Geometry::patternCell()) laid from the
 *        painter's origin, the box's corner, as the SVG's `<pattern>` is.
 */
[[nodiscard]] QBrush paintBrush(const Paint& p);

/**
 * \brief Draws @p items in order. What only an SVG renderer can draw is left out, as each field of
 *        Render::Item says: a filter, a bleed, an item that only a filter needs; a mask clips instead.
 */
void play(QPainter& painter, const QList<Render::Item>& items);

} // namespace Painter

#endif // RECORDPAINTER_HPP
