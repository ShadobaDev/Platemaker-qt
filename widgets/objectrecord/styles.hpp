/**
 * \file styles.hpp
 * \brief Every line and every texture, one table entry each: their names, a line's strokes of its own
 *        (ink), and the room a style needs past the geometry.
 *
 * **Adding a line is a value in `StyleProperties::Line` and an entry here** (styles.cpp); a texture
 * likewise, and its filter in recordsvg.cpp (styleDefs()). Everything else asks the tables: the file
 * names (lineName, textureName), the Style panel's lists (lineTitle, textureTitle), the ink and whether
 * the solid outline is drawn (the painter, the SVG, the hit test), the room it needs past the geometry
 * (styleMargin) and whether the library has to draw its preview (hasFilter).
 */

#ifndef STYLES_HPP
#define STYLES_HPP

#include <QPainterPath>
#include <QtGlobal>

#include "objectrecord.hpp"

namespace Geometry {

//! One line: everything about it that is not the record's own state.
struct LineInfo
{
    StyleProperties::Line kind;   //!< Which line this entry is; the table is checked against it.
    const char*           name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*           title;  //!< Shown on screen, translated through QObject::tr().
    /**
     * Strokes of its own, drawn in the stroke colour over the fill, or nullptr for none. A line with
     * ink **replaces the solid outline**: its strokes are the edge. Plain paths, so Qt draws them.
     */
    QPainterPath (*ink)(const ObjectRecord& a, const QPainterPath& silhouette);
    //! How far the ink reaches past the silhouette, in balloon pixels. Set whenever `ink` is.
    qreal (*inkReach)(const ObjectRecord& a);
};

//! One texture: an SVG filter (recordsvg.cpp), which only the library draws.
struct TextureInfo
{
    StyleProperties::Texture kind;   //!< Which texture this entry is; the table is checked against it.
    const char*              name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*              title;  //!< Shown on screen, translated through QObject::tr().
    //! How far the filter moves or spreads the balloon past its geometry, in balloon pixels; nullptr
    //! for None, which is no filter.
    qreal (*reach)(const StyleProperties& s);
};

//! The entry for @p kind. Every kind up to `StyleProperties::k_lastLine` has one.
[[nodiscard]] const LineInfo& lineInfo(StyleProperties::Line kind);

//! The entry for @p kind. Every kind up to `StyleProperties::k_lastTexture` has one.
[[nodiscard]] const TextureInfo& textureInfo(StyleProperties::Texture kind);

/**
 * @brief @p s made Rough with the Ink look's numbers: a finer and smaller wobble than the Marker's (the
 *        struct's defaults), then a bleed. A built-in preset (PLAN-L B2).
 */
[[nodiscard]] StyleProperties inkRough(StyleProperties s);

//! \p a has a texture: an SVG filter, which only the library draws.
[[nodiscard]] bool hasFilter(const ObjectRecord& a);

/**
 * @brief \p a's line's own strokes around \p silhouette, in box coordinates; empty for a line without.
 *
 * Filled with the non-zero rule, every stroke wound the same way, so overlapping strokes paint as one.
 */
[[nodiscard]] QPainterPath ink(const ObjectRecord& a, const QPainterPath& silhouette);

//! \p a's line draws the solid outline: every line but one with ink of its own.
[[nodiscard]] bool hasOutline(const ObjectRecord& a);

}  // namespace Geometry

#endif // STYLES_HPP
