/**
 * \file styles.hpp
 * \brief Every line style, one table entry each: its names and its recipe.
 *
 * **Adding a style is a value in `StyleProperties::Kind` and an entry here** (styles.cpp). Everything
 * else asks the table: the file names (styleName), the Style panel's list (styleTitle), the SVG filter
 * (recordsvg.cpp), the room it needs past the geometry (styleMargin) and whether the library has to
 * draw its preview (hasFilter).
 */

#ifndef STYLES_HPP
#define STYLES_HPP

#include <QtGlobal>

#include "objectrecord.hpp"

namespace Geometry {

/**
 * @brief A noise filter's numbers: turbulence, displacing the outline, then optionally a blur.
 *
 * The filter (recordsvg.cpp) and the margin it needs (styleMargin()) both read these, so the room
 * reserved for the effect cannot disagree with the effect. Every length is at amount 1.0, in balloon
 * pixels; the record's `amount` scales it.
 */
struct NoiseRecipe
{
    qreal frequency;     //!< feTurbulence baseFrequency, in balloon pixels: the wobble's grain.
    int   octaves;       //!< feTurbulence numOctaves: how many finer wobbles ride on the first.
    qreal displacement;  //!< feDisplacementMap scale: how far the outline wanders.
    qreal blur;          //!< feGaussianBlur stdDeviation; 0 for none.
    qreal blurReach;     //!< How far the blur spreads past the displaced edge, added to the margin.
};

//! One line style: everything about it that is not the record's own state.
struct StyleInfo
{
    StyleProperties::Kind kind;   //!< Which style this entry is; the table is checked against it.
    const char*           name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*           title;  //!< Shown on screen, translated through QObject::tr().
    /**
     * The SVG filter it is, or nullptr for none. A filter is what Qt cannot draw, so a style with one
     * previews through the library, and only a style with one has an `amount` to scale.
     */
    const NoiseRecipe* noise;
};

//! The entry for @p kind. Every kind up to `StyleProperties::k_lastKind` has one.
[[nodiscard]] const StyleInfo& styleInfo(StyleProperties::Kind kind);

//! \p a's style is an SVG filter, which only the library draws.
[[nodiscard]] bool hasFilter(const ObjectRecord& a);

}  // namespace Geometry

#endif // STYLES_HPP
