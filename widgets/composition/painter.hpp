/**
 * \file painter.hpp
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
#include <functional>
#include <QImage>
#include <QPainterPath>
#include <QTransform>

#include "geometry.hpp"
#include "names.hpp"
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
    QPainterPath        silhouette;   //!< The balloon and its tails (Geometry::silhouette()).
    QList<QPainterPath> texts;        //!< Each words' lettering as glyph outlines (Geometry::textOutlines()).
    Render::List        balloon;      //!< Render::balloon() of the silhouette.
    Render::List        words;        //!< Render::lettering() of the texts.
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
 * \brief Runs @p draw, which draws @p c, with @p c's mask cut out of it, as the SVG's mask does; with no mask,
 *        straight onto @p painter.
 *
 * Drawn into a layer over @p bounds (in the painter's coordinates), its region then taken away from it: so a
 * blend mode the painter is set to applies to what is left, once.
 */
void paintCut(QPainter& painter, const Composition& c, const QRectF& bounds,
              const std::function<void(QPainter&)>& draw);

/**
 * \brief Mask object @p c's Hued view (Mask::hued): over its drawn box, black where it hides and white where it keeps,
 *        as a layer mask is shown, the whole at @p opacity.
 */
void paintHued(QPainter& painter, const Composition& c, qreal opacity);

/**
 * \brief A mask cutting an object it is not part of: a mask object above it, laid on the object by @p toObject (from
 *        the mask object's frame to the object's).
 */
struct Cut
{
    Mask       mask;
    quint32    seed = 0;
    QTransform toObject;
    QRectF     frame;   //!< What the mask object draws (Geometry::bounds()): kept inside, it hides there but its regions.

    [[nodiscard]] bool operator==(const Cut& o) const
    {
        return mask == o.mask && seed == o.seed && toObject == o.toObject && frame == o.frame;
    }
};

/**
 * \brief Runs @p draw with each of @p cuts taken away from what it draws, as the library's mask role cuts a mask
 *        object out of the overlays below it; with none, straight onto @p painter.
 *
 * Drawn into a layer over @p bounds, as paintCut() is, so a blend mode applies once to what is left.
 */
void paintCuts(QPainter& painter, const QRectF& bounds, const QList<Cut>& cuts,
               const std::function<void(QPainter&)>& draw);

} // namespace Painter

#endif // RECORDPAINTER_HPP
