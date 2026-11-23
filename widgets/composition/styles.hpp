/**
 * \file styles.hpp
 * \brief Every line and every texture, one table entry each: their names, a line's strokes of its own
 *        (ink), and the room a style needs past the geometry.
 *
 * **Adding a line is a value in `StyleProperties::Line` and an entry here** (styles.cpp), with a section
 * for its numbers if it has any (a struct with fields() and section(), a member of StyleProperties and a
 * line in its sections()); the Line panel builds its rows from the section. A texture likewise, its SVG
 * filter with it. A line reads its numbers, the outline model (outline.hpp) and a seed,
 * never the composition: its corners, the tails' ends and what covers a part are the model's. Everything else
 * asks the tables: the file
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

#include <array>
#include <functional>

#include "composition.hpp"
#include "names.hpp"
#include "outline.hpp"      // OutlineModel: what a line is drawn round
#include "renderlist.hpp"   // Render::Filter: a kind's SVG filter
#include "shapes.hpp"   // Handle: the fade's cross

#include <optional>

namespace Geometry {

//! One line: everything about it that is not the composition's own state.
struct LineInfo
{
    StyleProperties::Line kind;   //!< Which line this entry is; the table is checked against it.
    const char*           name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*           title;  //!< Shown on screen, translated through QObject::tr().
    /**
     * Strokes of its own round @p outline, drawn in the stroke colour over the fill, or nullptr for none:
     * the solid outline (Clean, and Dry ink, which is that outline under a filter). A line with ink
     * **replaces the solid outline**: its strokes are the edge. Plain paths, so Qt draws them. @p seed is
     * the balloon's, so its strokes stay put while it is edited.
     */
    QPainterPath (*ink)(const StyleProperties& s, const OutlineModel& outline, quint32 seed);
    //! How far the ink reaches past the silhouette, in balloon pixels. Set whenever `ink` is.
    qreal (*inkReach)(const StyleProperties& s);
    /**
     * A filter the solid outline is drawn under, its noise seeded by @p seed, or nullptr for none: an SVG
     * renderer's alone, so the painter draws the outline plain (Dry ink: the outline eaten by the paper).
     */
    Render::Filter (*outlineFilter)(const StyleProperties& s, quint32 seed) = nullptr;
    /**
     * The share of the outline's width drawn again, unfiltered, over the filtered one (0..1), so the filter
     * never opens the middle of the line; nullptr for none. Read only with `outlineFilter`.
     */
    qreal (*unworn)(const StyleProperties& s) = nullptr;
    /**
     * The sections of StyleProperties it reads, by Fields::Section::name, whose rows the Line panel shows
     * while it is chosen; a kind that borrows another's (Explosion) lists both.
     */
    Fields::SectionNames sections{};
    //! Of those rows, the ones whose key holds this are not shown: what the kind reads none of; or nullptr.
    const char* hides = nullptr;
    /**
     * Its ink is strokes apart from each other, none with a hole of its own, so the painter may fill them a few
     * at a time (Render::Item::marks): far faster than as one path once there are hundreds.
     */
    bool apart = false;
};

//! One texture: an SVG filter (`filter`), which only the library draws.
struct TextureInfo
{
    TextureProperties::Kind  kind;   //!< Which texture this entry is; the table is checked against it.
    const char*              name;   //!< Persisted, in the JSON and the SVG. **Never changes** once saved.
    const char*              title;  //!< Shown on screen, translated through QObject::tr().
    //! How far the filter moves or spreads the balloon past its geometry, in balloon pixels; nullptr
    //! for None, which is no filter.
    qreal (*reach)(const TextureProperties& t);
    //! The filter itself, noise seeded by @p seed; nullptr for None. Set whenever `reach` is.
    Render::Filter (*filter)(const TextureProperties& t, quint32 seed) = nullptr;
    //! The sections of TextureProperties it reads, whose rows the Texture panel shows while it is chosen.
    Fields::SectionNames sections{};
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

//! One style of tail (TailLook): its names, and what it reads. How it is drawn is silhouette.cpp's.
struct TailStyleInfo
{
    TailLook::Style      kind;
    const char*          name;     //!< Persisted, in a tail's look (toJson(Composition)). **Never changes** once saved.
    const char*          title;    //!< Shown on screen, translated through QObject::tr().
    bool                 tipped;   //!< Whether it ends as its tip says: the Tail panel offers the tip only then.
    //! The sections of TailLook it reads, whose rows the Tail panel shows while it is chosen.
    Fields::SectionNames sections;
};

//! One tip a tail may end in (TailLook::Tip): its names, and the sections it reads.
struct TailTipInfo
{
    TailLook::Tip        kind;
    const char*          name;    //!< Persisted, in a tail's text. **Never changes** once saved.
    const char*          title;   //!< Shown on screen, translated through QObject::tr().
    Fields::SectionNames sections;
};

//! The entry for @p kind. Every kind up to `TailLook::k_lastStyle` has one.
[[nodiscard]] const TailStyleInfo& tailStyleInfo(TailLook::Style kind);

//! The entry for @p kind. Every kind up to `TailLook::k_lastTip` has one.
[[nodiscard]] const TailTipInfo& tailTipInfo(TailLook::Tip kind);

/**
 * \brief A pattern's repeating cell: a square of the Paint's colour with its marks in the second colour —
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
 * \brief @p p's marks over @p area, each as large as @p tone (0 to 1) says at its place: a halftone that
 *        fades. Geometry, so the strip and the output draw the same path.
 *
 * The grid is patternCell()'s, laid from the box's corner and turned alike, so at a tone of 1 the marks are
 * the pattern's. A dot's area follows the tone; a bar's width does, sampled every @p step along it. Fill
 * it with the non-zero rule in the marks' colour; empty for a solid Paint.
 */
[[nodiscard]] QPainterPath halftoneMarks(const Paint& p, const QRectF& area,
                                         const std::function<qreal(const QPointF&)>& tone, qreal step);

/**
 * \brief Part of a smooth mask: an area, and how much it hides there (1 wholly, 0 nothing), evenly or along a
 *        gradient SVG and QPainter both draw. maskShades() gives them; the SVG writes them into its mask, the painter
 *        cuts them from the object.
 *
 * The areas are not cut to the mask's region: the writers draw them without smoothing their edges and cut the
 * whole to the region, smoothed. Where two areas meet their tone is the same, so the unsmoothed edge between them
 * cannot be seen, where two smoothed edges would leave a seam of half coverage twice.
 */
struct MaskShade
{
    enum class Kind { Solid, Linear, Radial };
    QPainterPath area;
    Kind         kind = Kind::Solid;
    qreal        from = 1.0;   //!< Hidden at the gradient's start (Solid: everywhere).
    qreal        to   = 0.0;   //!< ...and at its end, and past it.
    QLineF       line;         //!< Linear: from its start to its end.
    QPointF      centre;       //!< Radial: the ellipses' centre...
    QSizeF       axes;         //!< ...the end's half-axes...
    qreal        start = 0.0;  //!< ...and where the fade starts, as a fraction of them.
};

//! @p mask's tone as its cross: its `fade…` fields, round when its shape is Ellipse.
[[nodiscard]] Paint maskCross(const Mask& mask);

/**
 * \brief How much @p mask hides at @p at inside its region, 1 to 0, smoothly: by its line (Linear), by its cross
 *        (Rectangle, Ellipse; fadeTone()), wholly with no fade. A pattern's marks are not counted.
 */
[[nodiscard]] qreal maskCover(const Mask& mask, const QPointF& at);

/**
 * \brief @p mask drawn smoothly: its region hiding wholly; or along its line; or, by its cross, quarter by
 *        quarter round the cross's centre: a quarter with one arm fading a linear gradient along it; with both, a
 *        Rectangle's split along the line through the two arms' starts and ends into two linear gradients (exact:
 *        the larger way out is the tone), an Ellipse's a radial gradient on the ends' ellipse (exact when both arms
 *        start at the same fraction of their ends, else started at the two fractions' mean).
 */
[[nodiscard]] QList<MaskShade> maskShades(const Mask& mask, quint32 seed);

//! What a mask with a pattern hides (maskMarks()), in the composition's frame.
struct MaskMarks
{
    QPainterPath hidden;   //!< The region where its cross hides anything (fadeExtent()): past it nothing is hidden.
    QPainterPath shown;    //!< The pattern's marks over it, which show what they cover: non-zero filled.
};

/**
 * \brief A mask's pattern: the region hidden where its fade hides anything (before its line's end, or inside its
 *        cross's reach), less the marks of the pattern (halftoneMarks()), which grow from nothing where it hides
 *        wholly until they meet where it hides nothing (maskCover()). Empty for a smooth mask, or one that does not
 *        fade: those are maskShades().
 *
 * What the mask leaves of its object breaks into that pattern: a halftone's dots, a hatch's lines, a
 * cross-hatch's grid. The SVG draws `hidden` black and `shown` white in its mask; the painter takes `hidden` less
 * `shown` away.
 */
[[nodiscard]] MaskMarks maskMarks(const Mask& mask, quint32 seed);

//! @p p's cross fades: an arm's start short of its side, whatever its kind.
[[nodiscard]] bool crossFades(const Paint& p);

//! @p p's pattern fades by its cross (crossFades()): drawn as halftoneMarks(), not a tile.
[[nodiscard]] bool fades(const Paint& p);

/**
 * \brief Where @p p's cross on @p body reaches: up to its arms' ends (@p ends), so its tone is above nothing
 *        inside; or up to their starts, so its tone is whole inside. A rectangle, or, round, four quarters of
 *        ellipses; an arm that does not fade reaches as far as anything is drawn.
 */
[[nodiscard]] QPainterPath fadeExtent(const Paint& p, const QRectF& body, bool ends);

/**
 * \brief How much of @p p's pattern is left at @p at, 1 to 0, by its fade's cross on @p body (the balloon's
 *        rectangle, balloonRect()): along each arm from the cross's centre, whole up to its start and gone
 *        past its end, the two directions combined by the larger (a rectangle, its corners square), or on
 *        ellipses when round; the other way round when reversed. Laid
 *        on the balloon, so moving a tail does not move it.
 */
[[nodiscard]] qreal fadeTone(const Paint& p, const QRectF& body, const QPointF& at);

/**
 * \brief The marks of \p b's fill where it fades, over \p silhouette's extent; empty for a
 *        fill that does not. Drawn in the pattern's colour over the ground, clipped to the silhouette.
 */
[[nodiscard]] QPainterPath fillTone(const Balloon& b, const QPainterPath& silhouette);

//! The fade's handles (fadeHandles()), told apart by Handle::part.
enum class FadePart {
    From,     //!< Side `index` (left, right, top, bottom): where it starts.
    To,       //!< ...and where it ends.
    Centre,   //!< The cross's centre.
};

/**
 * \brief The fade's cross for @p p on @p body, as shape-editing handles: on each arm a dot where the fade
 *        starts and one where it ends, then the centre, whatever @p p's kind (a fill asks only with a pattern).
 *        At rest every dot is on its side, which is no fade, wherever the centre is.
 *
 * The centre can always be pressed: a dot nearer it than @p clearance (balloon pixels) is drawn that far out along
 * its arm, where a press takes it and a drag puts it where it is drawn; an arm with no length (the centre on its
 * side) offers no dots, as it has nothing to drag along.
 */
[[nodiscard]] QList<Handle> fadeHandles(const Paint& p, const QRectF& body, qreal clearance = 0.0);

//! @p p with fade @p handle dragged to @p to (balloon coordinates): fadeHandles()' inverse.
[[nodiscard]] Paint dragFadeHandle(const Paint& p, const QRectF& body, const Handle& handle, const QPointF& to);

//! The entry for @p kind. Every kind up to `StyleProperties::k_lastLine` has one.
[[nodiscard]] const LineInfo& lineInfo(StyleProperties::Line kind);

//! The entry for @p kind. Every kind up to `TextureProperties::k_lastKind` has one.
[[nodiscard]] const TextureInfo& textureInfo(TextureProperties::Kind kind);

/**
 * \brief @p s made Rough with the Ink look's numbers: a finer and smaller wobble than the Marker's (the
 *        struct's defaults), then a bleed. A built-in preset.
 */
[[nodiscard]] TextureProperties inkRough(TextureProperties t);

/**
 * \brief \p c has an SVG filter anywhere, which only the library draws: a texture, a
 *        soft shadow, the balloon's or its letters', or a mask's feather. The one question the controller asks to decide how a
 *        balloon is previewed.
 */
[[nodiscard]] bool hasFilter(const Composition& c);

/**
 * @name A shadow (Shadow) and the Paint it is filled with, whoever casts it: the balloon (its effects) or
 * the lettering. Each also has a form taking the composition, for the balloon's.
 * @{
 */
//! \p s is soft and of one colour: blurred, by an SVG filter.
[[nodiscard]] bool blursShadow(const Shadow& s, const Paint& fill);

//! \p s is soft and a halftone: its marks fade over the softness, as geometry.
[[nodiscard]] bool fadesShadow(const Shadow& s, const Paint& fill);

/**
 * \brief What a shadow is drawn from, with no boolean operation on its caster: the caster moved, and how far
 *        it grows, drawn as a round stroke `2 × grow` wide over it in the same paint.
 *
 * A union of the caster with its stroke would be the grown shape as one path, but a union costs as the
 * square of the outline: a balloon's is quick, a line of lettering's took a second and a half. Drawn
 * together instead, the body and its band are filled opaque and given the shadow's alpha once (a layer on a
 * painter, a group's `opacity` in the SVG), so where they overlap they do not darken twice.
 */
struct ShadowPaths
{
    QPainterPath body;          //!< The caster moved by the offset; for a fading halftone, its marks (grown already).
    qreal        grow = 0.0;    //!< How far the body grows on every side, as a stroke; 0 for none and for marks.

    [[nodiscard]] bool isEmpty() const { return body.isEmpty(); }
};

/**
 * \brief \p s cast by \p caster: ShadowPaths, grown by its spread and moved by its offset; for one that
 *        fades, the halftone's marks over that. Empty with no shadow. Painted with shadowPaint(), or with
 *        shadowLayerPaint() while it grows.
 */
[[nodiscard]] ShadowPaths shadowOf(const Shadow& s, const Paint& fill, const QPainterPath& caster);

/**
 * \brief What a growing shadow's body and band are painted with together: shadowPaint() made opaque, its
 *        alpha given to \p opacity for the layer or group they are drawn in.
 */
[[nodiscard]] Paint shadowLayerPaint(const Shadow& s, const Paint& fill, qreal* opacity);

/**
 * \brief What shadowOf() is filled with: the shadow's colour, or its pattern's marks in that colour on no
 *        ground (a shadow is replaced by its halftone, not covered by it); for one that fades, the colour.
 */
[[nodiscard]] Paint shadowPaint(const Shadow& s, const Paint& fill);

//! How far past its caster \p s reaches on any side: its spread, its offset and its blur. 0 with none.
[[nodiscard]] qreal shadowReach(const Shadow& s);

[[nodiscard]] bool         blursShadow(const Balloon& b);   //!< The balloon's.
[[nodiscard]] bool         fadesShadow(const Balloon& b);   //!< The balloon's.
[[nodiscard]] ShadowPaths  shadowOf(const Balloon& b, const QPainterPath& silhouette);   //!< The balloon's.
[[nodiscard]] Paint        shadowPaint(const Balloon& b);   //!< The balloon's.
/** @} */

/**
 * \brief \p b's line's own strokes around \p silhouette, in box coordinates; empty for a line without.
 *
 * Filled with the non-zero rule, every stroke wound the same way, so overlapping strokes paint as one.
 */
[[nodiscard]] QPainterPath ink(const Balloon& b, quint32 seed, const QPainterPath& silhouette);

//! \p b's line draws the solid outline: every line but one with ink of its own.
[[nodiscard]] bool hasOutline(const Balloon& b);

}  // namespace Geometry

#endif // STYLES_HPP
