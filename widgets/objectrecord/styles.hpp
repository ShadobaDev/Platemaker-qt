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

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QtGlobal>

#include <functional>

#include "objectrecord.hpp"
#include "shapes.hpp"   // ShapeHandle: the fade's cross

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
    TextureProperties::Kind  kind;   //!< Which texture this entry is; the table is checked against it.
    const char*              name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*              title;  //!< Shown on screen, translated through QObject::tr().
    //! How far the filter moves or spreads the balloon past its geometry, in balloon pixels; nullptr
    //! for None, which is no filter.
    qreal (*reach)(const TextureProperties& t);
};

//! One pattern of a Paint: its names. How it is drawn is the painter's (a brush) and the SVG's (a
//! `<pattern>`), both from one description of its cell (patternCell()).
struct PatternInfo
{
    Paint::Kind kind;    //!< Which pattern this entry is; the table is checked against it.
    const char* name;    //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char* title;   //!< Shown on screen, translated through QObject::tr().
};

//! The entry for @p kind. Every kind up to `Paint::k_lastKind` has one.
[[nodiscard]] const PatternInfo& patternInfo(Paint::Kind kind);

/**
 * @brief A pattern's repeating cell: a square of the Paint's colour with its marks in the second colour —
 *        a dot, a line across, or a line each way — the grid turned by `angle`. One description that both
 *        the painter's brush and the SVG's `<pattern>` are drawn from, so the preview and the output
 *        cannot disagree.
 */
struct PatternCell
{
    qreal        size  = 0.0;   //!< The square's side: the spacing.
    qreal        angle = 0.0;   //!< Degrees, clockwise.
    /**
     * The marks in the square, non-zero filled so where two overlap (a grid's crossing, merging dots)
     * they paint once: a dot spilling past the square brings its neighbours' back in.
     */
    QPainterPath marks;
};

//! @p p's cell; empty marks for a solid Paint.
[[nodiscard]] PatternCell patternCell(const Paint& p);

/**
 * @brief @p p's marks over @p area, each as large as @p tone (0 to 1) says at its place: a halftone that
 *        fades (PLAN-L B37). Geometry, so the strip and the output draw the same path.
 *
 * The grid is patternCell()'s, laid from the box's corner and turned alike, so at a tone of 1 the marks are
 * the pattern's. A dot's area follows the tone; a bar's width does, sampled every @p step along it. Fill
 * it with the non-zero rule in the marks' colour; empty for a solid Paint.
 */
[[nodiscard]] QPainterPath halftoneMarks(const Paint& p, const QRectF& area,
                                         const std::function<qreal(const QPointF&)>& tone, qreal step);

//! @p p's pattern fades by its cross (an arm's start short of its side): drawn as halftoneMarks(), not a tile.
[[nodiscard]] bool fades(const Paint& p);

/**
 * @brief How much of @p p's pattern is left at @p at, 1 to 0, by its fade's cross on @p body (the balloon's
 *        rectangle, balloonRect()): along each arm, whole up to its start and gone past its end, the two
 *        directions combined as a distance. Laid on the balloon, so moving a tail does not move it.
 */
[[nodiscard]] qreal fadeTone(const Paint& p, const QRectF& body, const QPointF& at);

/**
 * @brief The marks of \p a's fill where it fades (PLAN-L P13c), over \p silhouette's extent; empty for a
 *        fill that does not. Drawn in the pattern's colour over the ground, clipped to the silhouette.
 */
[[nodiscard]] QPainterPath fillTone(const ObjectRecord& a, const QPainterPath& silhouette);

/**
 * @brief The fade's cross for @p p on @p body, as shape-editing handles: on each arm a dot where the fade
 *        starts and one where the pattern is gone; none for a solid Paint. At rest every dot is on its
 *        side, which is no fade.
 */
[[nodiscard]] QList<ShapeHandle> fadeHandles(const Paint& p, const QRectF& body);

//! @p p with fade @p handle dragged to @p to (balloon coordinates): fadeHandles()' inverse.
[[nodiscard]] Paint dragFadeHandle(const Paint& p, const QRectF& body, const ShapeHandle& handle, const QPointF& to);

//! The entry for @p kind. Every kind up to `StyleProperties::k_lastLine` has one.
[[nodiscard]] const LineInfo& lineInfo(StyleProperties::Line kind);

//! The entry for @p kind. Every kind up to `TextureProperties::k_lastKind` has one.
[[nodiscard]] const TextureInfo& textureInfo(TextureProperties::Kind kind);

/**
 * @brief @p s made Rough with the Ink look's numbers: a finer and smaller wobble than the Marker's (the
 *        struct's defaults), then a bleed. A built-in preset (PLAN-L B2).
 */
[[nodiscard]] TextureProperties inkRough(TextureProperties t);

/**
 * @brief \p a has an SVG filter anywhere, which only the library draws (PLAN-L M17): a texture, or a
 *        soft shadow. The one question the controller asks to decide how a balloon is previewed.
 */
[[nodiscard]] bool hasFilter(const ObjectRecord& a);

//! \p a's shadow is soft and of one colour: blurred, by an SVG filter.
[[nodiscard]] bool blursShadow(const ObjectRecord& a);

//! \p a's shadow is soft and a halftone: its marks fade over the softness, as geometry (PLAN-L P13).
[[nodiscard]] bool fadesShadow(const ObjectRecord& a);

/**
 * @brief \p a's shadow: \p silhouette grown by its spread and moved by its offset; for one that fades, the
 *        halftone's marks over that. Empty with no shadow. Filled under the balloon with shadowPaint().
 */
[[nodiscard]] QPainterPath shadowOf(const ObjectRecord& a, const QPainterPath& silhouette);

/**
 * @brief What shadowOf() is filled with: the shadow's colour, or its pattern's marks in that colour on no
 *        ground (a shadow is replaced by its halftone, not covered by it); for one that fades, the colour.
 */
[[nodiscard]] Paint shadowPaint(const ObjectRecord& a);

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
