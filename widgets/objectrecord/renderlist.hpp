/**
 * \file renderlist.hpp
 * \brief What a record draws, in order, once: the list both the painter and the SVG writer play.
 *
 * A record is resolved into **items** (paths filled and stroked, groups, a picture) and the **definitions**
 * they refer to (a pattern, a filter). The painter draws the items (recordpainter.hpp); the SVG writer
 * serialises them (recordsvg.hpp). So which layer is drawn over which, what a line with strokes of its own
 * replaces, how the fill and its texture are split, is decided here and nowhere else.
 *
 * Some of it only an SVG renderer can do: a filter, a mask, a bleed past an edge a filter moves. The items
 * say so, and the painter, which draws no filter, leaves those parts out by the rules written on each field.
 * A kind that needs a filter (a texture, Dry ink) brings it from its own table entry (styles.hpp): nothing
 * here or in the writer names a kind.
 */

#ifndef RENDERLIST_HPP
#define RENDERLIST_HPP

#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

#include "propertygroups.hpp"

struct ObjectRecord;
namespace Geometry {
struct ShadowPaths;
}

namespace Render {

//! Decimal places for a path's coordinates and a drawing's lengths: well under a rendered pixel.
inline constexpr int k_coordinateDecimals = 2;
/**
 * Decimal places for a number a filter is tuned by or the record is read back from: a grain's spin steps by
 * a thousandth, and two places would round 0.035 to another texture. Trailing zeros are trimmed, so a
 * number with two places or fewer is written as with two.
 */
inline constexpr int k_valueDecimals = 4;

/**
 * \brief @p v as an SVG number: fixed, trimmed of trailing zeros, never `-0` and never scientific (SVG has no
 *        such syntax, so `1e-05` would silently be an invalid path).
 */
[[nodiscard]] QString number(qreal v, int decimals = k_coordinateDecimals);

/**
 * \brief One filter primitive (or a child of one, a transfer function): its tag and its attributes, in the
 *        order they are written, values already spelt (number()).
 */
struct Primitive
{
    QString                          tag;
    QList<std::pair<QString, QString>> attrs;
    QList<Primitive>                 children;   //!< Written inside it, on its line.
};

/**
 * \brief A filter: an SVG renderer's alone (librsvg draws it, Qt does not). Its region is generous, as a
 *        displacement moves ink outward and a filter region is a hard clip.
 */
struct Filter
{
    QString          region = QStringLiteral("x=\"-25%\" y=\"-25%\" width=\"150%\" height=\"150%\"");
    QList<Primitive> primitives;
};

//! What a definition is: a Paint's pattern, or a filter.
struct Def
{
    QString               id;
    std::optional<Paint>  pattern;   //!< A pattern's Paint (never a solid one).
    std::optional<Filter> filter;
};

//! What an area or a stroke is painted with: a Paint, its pattern under @p patternId; a solid one by colour.
struct Ink
{
    Paint   paint;
    QString patternId;   //!< Empty: the Paint's colour, even for a pattern's Paint (a fading fill's ground).
};

//! An Ink of plain @p colour.
[[nodiscard]] Ink colourInk(const QColor& colour);

struct Item
{
    enum class Kind { Path, Group, Image };
    Kind kind = Kind::Path;

    /** @name A path: filled, stroked, or both (filled first). @{ */
    QPainterPath       path;
    std::optional<Ink> fill;           //!< None: `fill="none"`.
    bool               nonzero = false;   //!< Filled by the nonzero rule whatever the path's own.
    /**
     * The path is many small marks (a halftone): the painter fills them in pieces through a layer of their
     * own, opaque, given the colour's alpha once, so overlaps do not darken (Painter::fillMarks()).
     */
    bool               marks = false;
    std::optional<Ink> stroke;
    qreal              strokeWidth = 0.0;
    bool               roundCap    = false;   //!< Else the default cap; the join is always round.
    /**
     * Drawn past the path by this much on every side, in the fill's paint, to meet an edge a filter moves
     * (Rough in parts). The painter draws no filter, so it draws no bleed.
     */
    qreal              bleed = 0.0;
    QPainterPath       clip;           //!< Clipped to it, when not empty; written as a clipPath before it.
    QString            clipId;
    /** Only where the filter of the group it is in is drawn: the painter leaves it (Dry ink's unworn core). */
    bool               filteredOnly = false;
    /** @} */

    /** @name A path or a group: a filter defined as @p filter. The painter draws the item without it. @{ */
    QString filter;
    /** @} */

    /** @name A group. @{ */
    QList<Item>  children;
    qreal        opacity = 1.0;
    /** Drawn into a layer of its own and composited once at `opacity`, so its overlapping parts paint as one. */
    bool         layer   = false;
    /**
     * Masked by this path under the filter `maskFilter` (Rough in parts: the fill meets the moved edge). The
     * painter draws neither the filter nor a bleed, so of the group only its marks can reach past the path:
     * it clips those to it, and draws the rest as they are.
     */
    QPainterPath mask;
    QString      maskId;
    QString      maskFilter;
    /** @} */

    /** @name A picture, at the origin. The painter leaves it: the object that shows one draws it itself. @{ */
    QByteArray picture;
    QString    mime;
    QSizeF     size;
    /** @} */
};

//! A record's drawing: what its items refer to, then the items in painting order.
struct List
{
    QList<Def>  defs;
    QList<Item> items;
};

/**
 * \brief Everything @p a draws but its lettering, in painting order, and the definitions it needs:
 *        the balloon's shadow, fill, outline and the line's strokes, under the texture's filter.
 *
 * Empty for a shapeless record and a picture.
 */
[[nodiscard]] List balloon(const ObjectRecord& a, const QPainterPath& silhouette);

/**
 * \brief @p a's lettering, @p text (Geometry::textOutline()) with its @p shadow (Geometry::textShadowOf()):
 *        the shadow, the outline under the letters stroked at twice its width (two draws, not
 *        `paint-order`, which librsvg is sure to render alike), the letters. Empty with no words.
 */
[[nodiscard]] List lettering(const ObjectRecord& a, const QPainterPath& text, const Geometry::ShadowPaths& shadow);

}  // namespace Render

#endif // RENDERLIST_HPP
