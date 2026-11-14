/**
 * \file recordpainter.hpp
 * \brief Drawing a bubble on a QPainter — the canvas preview, the row glyphs and the rasterised image.
 *
 * Only the drawing: what is drawn, and in what order, is the composition's render list (renderlist.hpp), which
 * the SVG writer serialises from the same items. This header includes the geometry, so a caller that draws
 * has both.
 *
 * Nothing here knows about overlays, projects or the strip. It takes a composition and draws it at the
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
 * \brief Draws \p c in its frame (frameOf()), on whatever painter is given.
 *
 * The scene preview and the PNG both go through here, so "what you see" and "what is baked" cannot
 * drift apart — they are the same code path over the same numbers.
 *
 * \param painter The painter to draw into.
 * \param c The composition to draw.
 */
void paint(QPainter& painter, const Composition& c);

//! What a composition is drawn from, resolved once: a caller that repaints keeps it (BubbleObject).
struct Paths
{
    QPainterPath silhouette;   //!< The balloon and its tails (Geometry::silhouette()).
    QPainterPath text;         //!< The lettering as glyph outlines (Geometry::textOutline()).
    Render::List balloon;      //!< Render::balloon() of the silhouette.
    Render::List words;        //!< Render::lettering() of the text.
};

//! \p c's paths, resolved.
[[nodiscard]] Paths resolve(const Composition& c);

/**
 * \brief Draws \p c from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, a halftone lays thousands of marks,
 * and any shape with text lays out a document and builds glyph outlines — while a repaint happens on
 * every scroll, zoom and selection change. A caller that holds the paths draws through here; paint() is
 * the one-shot form.
 */
void paintPaths(QPainter& painter, const Composition& c, const Paths& paths);

/**
 * \brief Draws @p w, @p text (Geometry::textOutline()), with its @p shadow
 *        (Geometry::textShadowOf()) and its outline, in the box's coordinates: Render::lettering() played,
 *        over a balloon (paintPaths()) and over a picture alike.
 */
void paintLettering(QPainter& painter, const Words& w, const QPainterPath& text,
                    const Geometry::ShadowPaths& shadow);

/**
 * \brief Rasterises \p c to a transparent ARGB32 image the size of its bounds — what the library composites.
 * \param c The composition to rasterise.
 * \return The rasterised image.
 */
[[nodiscard]] QImage render(const Composition& c);

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

/**
 * @name The record's way in, until it goes (recordentry.cpp): each the form above of compositionOf(a).
 * @{
 */
[[nodiscard]] Paths  resolve(const ObjectRecord& a);
void                 paintPaths(QPainter& painter, const ObjectRecord& a, const Paths& paths);
void                 paintLettering(QPainter& painter, const ObjectRecord& a, const QPainterPath& text,
                                    const Geometry::ShadowPaths& shadow);
[[nodiscard]] QImage render(const ObjectRecord& a);
/** @} */

} // namespace Painter

#endif // RECORDPAINTER_HPP
