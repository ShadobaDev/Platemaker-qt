/**
 * \file geometry.hpp
 * \brief What a bubble **is**, as shapes on the page: its silhouette, its lettering and the room each
 *        takes — the single definition, shared by the canvas, the SVG and the render.
 *
 * Three consumers start from these paths: the canvas draws them (painter.hpp), the SVG writer
 * serialises them (svg.hpp), and the library rasterises that SVG. Because all three start here,
 * "what you see" and "what is baked" cannot drift apart. Nothing here draws or knows about overlays,
 * projects or the strip: it takes a composition (composition.hpp), or an element of one, and answers in the
 * composition's frame. The drawing and the fit take every words of a composition, each in its own frame; the
 * handles act on one words.
 *
 * Where each part is:
 *  - shapes.cpp: every shape's outline and text area, one table entry each (shapes.hpp);
 *  - silhouette.cpp: the outline with its tails, what it covers, and what a point lands on;
 *  - lettering.cpp: the words set and outlined, and fitting a box to them or them to a box.
 */

#ifndef GEOMETRY_HPP
#define GEOMETRY_HPP

#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include "bezier.hpp"
#include "composition.hpp"
#include "outline.hpp"
#include "shapes.hpp"   // Handle: the text area's arrows
#include "styles.hpp"   // ShadowPaths: the lettering's shadow

namespace Geometry {

// ---------------------------------------------------------------------------
// The silhouette — silhouette.cpp
// ---------------------------------------------------------------------------

//! The balloon (its bodies and tails) as one path, in the composition's frame; @p seed deals its edges.
[[nodiscard]] QPainterPath silhouette(const Balloon& b, quint32 seed);

/**
 * \brief What the silhouette is the union of: the balloon's outline first, then each tail that is drawn,
 *        each whole, overlapping where they join.
 *
 * For a line that follows each part's own edge rather than the joined one (the dry brush's hairs run on
 * past a tail's root along the balloon, and start inside it along the tail).
 */
[[nodiscard]] QList<QPainterPath> silhouetteParts(const Balloon& b, quint32 seed);

/**
 * \brief The outline a line is drawn round (outline.hpp): @p silhouette's rings with their corners, and
 *        silhouetteParts() each with its own, what covers it marked; the ends the tails report placed on both.
 */
[[nodiscard]] OutlineModel outlineModel(const Balloon& b, quint32 seed, const QPainterPath& silhouette);

/**
 * \brief Which of \p b's tails are actually drawn, one entry per tail.
 *
 * A tail whose tip lies inside the balloon draws nothing — there is no room for it to emerge — but it
 * still exists, and its handle still has to be reachable. The editor asks this to know which tips are
 * visible to the eye and which only to the selection chrome.
 */
[[nodiscard]] QList<bool> tailsDrawn(const Balloon& b, quint32 seed);

/**
 * \brief @p t as it is drawn: each anchor (a bridge has two) moved towards its body's middle, to the last place on
 *        that way where the base, square to the tail, fits inside its bubble on the tail's line, so the tail meets
 *        the outline with no notch.
 *
 * An anchor too near the edge for a tail running along it, however the tip, width or shape made it so, starts the
 * tail nearer the middle; turned back, the tail starts at the anchor again: the anchor set is never changed. One
 * where the base fits, or fits nowhere on that way, stays; so does each of a chain of circles, which has no base.
 */
[[nodiscard]] BalloonTail fittedTail(const Balloon& b, const BalloonTail& t, quint32 seed);

//! The body of @p b whose outline holds @p local, the last drawn tried first, by its id; 0 for none.
[[nodiscard]] int bodyAt(const Balloon& b, quint32 seed, const QPointF& local);

//! What an object is made of where a point lands — the three things that carry a colour, and a miss.
enum class Part { None, Fill, Outline, Text };

/**
 * \brief What is drawn at @p local (box coordinates), with @p slack units of forgiveness.
 *
 * The same geometry the scene draws, asked a different question — so a tool that acts on *what you are
 * pointing at* cannot disagree with what is on screen. Answered in painting order, topmost first: the
 * lettering, then the stroke band around the silhouette, then the silhouette's inside. A point outside
 * all three is \c None: the transparent corner of a balloon's box is not the balloon.
 *
 * \param c The composition to check.
 * \param local The point to check, in box coordinates.
 * \param slack Widens the lettering and the stroke band by this much on each side. A 1px outline and a
 *              thin letter are as hard to hit as each other, and the caller knows what a few *screen*
 *              pixels are worth in box units at the current zoom.
 * \return The part of \p c that is drawn at \p local, or \c None if nothing is.
 */
[[nodiscard]] Part partAt(const Composition& c, const QPointF& local, qreal slack);

//! The id of the words whose letters (or their outline) lie at \p local, as partAt() finds them, the last drawn
//! first; 0 for none.
[[nodiscard]] int wordsAt(const Composition& c, const QPointF& local, qreal slack);

/**
 * \brief Everything \p c actually covers, in its frame — origin may be negative.
 *
 * Its frame (frameOf()), the balloon, its tails and its text, plus room for the stroke. A tail can point anywhere, so this is
 * computed rather than assumed: it is the size of the rasterised buffer, the SVG's viewBox, and the
 * item's bounding rect, and those three agreeing is what keeps the preview and the render aligned.
 *
 * Snapped to whole pixels, so the buffer and the viewBox describe one rectangle rather than two
 * roundings of it.
 *
 * \param c The composition to check.
 * \return The rectangle that contains everything \p c draws, in its frame.
 */
[[nodiscard]] QRectF bounds(const Composition& c);

/**
 * \brief How far \p b's style pushes ink beyond the geometry, in balloon pixels.
 *
 * A displacement filter moves pixels *outward* as well as in, so the artwork covers more than its paths
 * do. The bounds — and therefore the buffer and the SVG's filter region — have to allow for it, or the
 * effect is neatly clipped off at the edge it was meant to roughen. Read from the style's recipe
 * (styles.cpp), the same numbers its filter is written from.
 *
 * \param b The balloon to check.
 * \return The extra margin the style adds, in balloon coordinates.
 */
[[nodiscard]] qreal styleMargin(const Balloon& b);

/**
 * \brief The bounds of \p c, given its silhouette and text paths.
 *
 * \param c The composition to check.
 * \param silhouette The silhouette path.
 * \param texts The text paths of its words, in their order (textOutlines()).
 * \return The rectangle that contains everything \p c draws, in its frame.
 */
[[nodiscard]] QRectF boundsOf(const Composition& c, const QPainterPath& silhouette, const QList<QPainterPath>& texts);

// ---------------------------------------------------------------------------
// The lettering — lettering.cpp
// ---------------------------------------------------------------------------

/**
 * \brief The laid-out text as **glyph outlines**, positioned in box coordinates.
 *
 * Outlines rather than a string, because that is what lets the library render a bubble with no font
 * stack and no font installed: by the time the artwork leaves here it is pure geometry. The wrapping is
 * still the text document's, so line breaks are exactly what the editor showed. Centred in the text area,
 * or from its top when taller. Clipped to it on the sides and the top, so an overlong line cannot bleed
 * past the stroke; a balloon's lines are not cut at the bottom, where they go on out of it.
 *
 * \param c The composition @p w is in.
 * \param w The words to set.
 * \return The text outlines, or an empty path if there is no lettering.
 */
[[nodiscard]] QPainterPath textOutline(const Composition& c, const Words& w);

//! textOutline() of every words of \p c, in their order (Composition::words).
[[nodiscard]] QList<QPainterPath> textOutlines(const Composition& c);

/**
 * \brief The letters of a words by the fill they wear: the index of the run (TextRun) that sets it, -1 for the words'
 *        own. Together they are textOutline().
 */
using TextFills = QMap<int, QPainterPath>;

//! textOutline() of \p w by fill (TextFills); empty when no run of \p w sets one, all its letters then the words'.
[[nodiscard]] TextFills textFills(const Composition& c, const Words& w);

//! textFills() of every words of \p c, in their order.
[[nodiscard]] QList<TextFills> textFillsOf(const Composition& c);

/**
 * \brief \p w's shadow, cast by \p text (textOutline()) with its outline: grown by the outline's
 *        width and the shadow's spread together, which is the same as the outlined letters grown by the
 *        spread. Empty with no shadow or no words. Filled under them with shadowPaint() of the lettering's.
 */
[[nodiscard]] ShadowPaths textShadowOf(const Words& w, const QPainterPath& text);

/**
 * \brief The family \p w is actually outlined in when its own is not installed, or empty.
 *
 * `QFont` never fails on a missing family — it substitutes, silently — so an outline built while the font
 * is absent is the same words in another face, and nothing in the composition says so. This names the stand-in
 * (Qt's own choice, as `QFontInfo` reports it), which is what the SVG records as `pm:fontFallback`. Empty
 * when there are no words, no family is asked for, or the family is available — including one a workspace
 * registered for itself.
 */
[[nodiscard]] QString fontFallback(const Words& w);

/**
 * \brief The family an empty `TextProperties::family` is drawn in on this machine — the application's
 *        default font (Segoe UI on Windows 11).
 *
 * An empty family means "the default", and the default is a property of the machine, not of the file: a
 * bubble drawn here in Segoe UI would be re-set in something else on a machine whose default differs, with
 * nothing in the file to tell. So this is what a file records in place of an empty family, and what a
 * preset with no font of its own is taken to mean when matching.
 */
[[nodiscard]] QString defaultFamily();

/**
 * \brief The frame \p w are laid in, in the composition's frame (Words::frame): its seat's inner box (a body's
 *        box less the stroke and the edge's teeth, a picture's whole box), each set edge that far in from it. Words
 *        with no seat, their own box.
 */
[[nodiscard]] QRectF textArea(const Composition& c, const Words& w);

/**
 * \brief Where a block of lettering of size \p block sits for \p w: aligned in the frame by the words' alignment,
 *        across (left, middle, right) and up and down (top, middle, bottom). A block larger than the frame goes
 *        past it away from the side it is aligned to, both ways from the middle. What the drawing, the fit and the
 *        red overflow all read.
 */
[[nodiscard]] QRectF blockRect(const Composition& c, const Words& w, const QSizeF& block);

/**
 * \brief The size of \p w's laid-out lettering — the widest line by the height of all of them.
 *
 * Independent of the box: the lettering is never wrapped, so where the box is and how big does not
 * change it. Empty when there is no text.
 */
[[nodiscard]] QSizeF textBlockSize(const Words& w);

/**
 * \brief A warp (TextProperties::warp) that bends nothing: straight along the flat block's middle line, from the middle
 *        of one side to the other's, across, or down for \p vertical columns. What switching a warp on starts from.
 */
[[nodiscard]] QList<ShapeNode> straightWarp(bool vertical);

//! A block of lettering per words of a composition, in their order (Composition::words); an empty one holds anywhere.
using Blocks = QList<QSizeF>;

//! textBlockSize() of every words of \p c.
[[nodiscard]] Blocks textBlocks(const Composition& c);

/**
 * \brief Whether each words of \p c holds its block of \p blocks, placed (blockRect()), in its frame.
 *
 * Pure geometry — no font is touched — so it is the half of fitting that can be tested anywhere.
 * Half a pixel of slack, so a box rounded to whole pixels does not overflow by its own rounding.
 */
[[nodiscard]] bool fits(const Composition& c, const Blocks& blocks);

//! True when \p w's lettering does not fit their frame in \p c — what the selection shows in red.
[[nodiscard]] bool textOverflows(const Composition& c, const Words& w);

/**
 * \brief Whether each block of \p blocks, placed (blockRect()) for its words, lies inside the outline of the body
 *        it is seated on, kept the stroke and the edge's teeth away from it — what *Fit to text* asks beyond
 *        fits(). True with no outline (a text object, a picture's words).
 */
[[nodiscard]] bool liesInOutline(const Composition& c, const Blocks& blocks);

/**
 * \brief The smallest size (sizeOf()) in which every words holds its block of \p blocks in its frame, and each
 *        block lies inside the outline itself (liesInOutline()).
 *
 * First the smallest **on both axes** whose frames hold the blocks; where a block still crosses the outline (a
 * curved one, or set edges reaching past it), that size grown, its proportions kept, until none does. Neither
 * side goes below the grips' own minimum (40 px), so a short word cannot collapse the shape. An axis no size can
 * make hold the blocks keeps the size it has.
 * Pure geometry, like fits(): fittedBox() is this with the lettering measured.
 */
[[nodiscard]] QSize fitBoxFor(const Composition& c, const Blocks& blocks);

/**
 * \brief The smallest size that holds \p c's lettering, on both axes — *Fit to text*.
 * \param c The composition to fit. Its size is the starting point only.
 * \return The fitted size; the current one when there is no text.
 */
[[nodiscard]] QSize fittedBox(const Composition& c);

/**
 * \brief @p c's balloon fitted to its words' blocks of @p blocks (textBlocks()) where they lie now: *Fit to text*.
 *
 * The rectangle every block on a body lies in becomes the body's seat (inner box), its middle on theirs;
 * where a curved outline would cut a block, the body grows, its proportions kept (liesInOutline()). The blocks keep
 * their places against each other: each frame is measured again from the new seat, an edge past it stops at it
 * (automatic), and the frame is cut down so the block's alignment puts it where it was. Neither side goes below the
 * grips' own minimum (40 px). A chain fits each bubble to its own words, in place: its seat's middle moves onto
 * theirs, so the words stay where they are on the strip. Without a balloon, @p c resized to fitBoxFor().
 */
[[nodiscard]] Composition fitToBlocks(const Composition& c, const Blocks& blocks);
//! @p c with only body @p body fitted to its words (fitToBlocks()): a bubble of a chain, in place.
[[nodiscard]] Composition fitBodyToBlocks(const Composition& c, int body, const Blocks& blocks);

//! The text area's handles (textAreaHandles()), told apart by Handle::part.
enum class TextAreaPart {
    Side,     //!< Side `index` (0 left, 1 right): that edge of the frame.
    Top,      //!< The frame's top.
    Bottom,   //!< The frame's bottom.
    Anchor,   //!< The frame's middle, a handle only: dragged, it moves the whole frame.
};

/**
 * \brief The text area's handles, in balloon coordinates: an arrow on its left edge and one on its right (`Side`,
 *        index 0 and 1), level with its middle, one on its top and one on its bottom (`Top`, `Bottom`), in line with
 *        it, each pointing across its edge. First the middle itself (`Anchor`), so a press where an arrow lies over
 *        it takes the middle. None for words with no seat, which keep their own box.
 */
[[nodiscard]] QList<Handle> textAreaHandles(const Composition& c, const Words& w);

/**
 * \brief \p w with text-area \p handle dragged to \p to (balloon coordinates): textAreaHandles()'
 *        inverse. An edge is set where the pointer is, inside the balloon or past it, never closer to the opposite
 *        edge than two pixels; the middle moves the whole frame by as much as the pointer, every edge set, its
 *        middle kept within the seat. Measured against \p c as it was at the press.
 */
[[nodiscard]] Words dragTextAreaHandle(const Composition& c, const Words& w, const Handle& handle, const QPointF& to);

//! A warp's curve (TextProperties::warp): an open path of two nodes at least.
inline constexpr Bezier::Rules k_warpRules{2, false};

/**
 * \brief The curve \p t is bent along, or would be: its warp, or with none the straight one (straightWarp()),
 *        which edit mode offers so a change to it bends the words.
 */
[[nodiscard]] QList<ShapeNode> warpCurve(const TextProperties& t);

/**
 * \brief Every warped words' curve of \p c, each in its warpBox(), and the dormant straight one of the words
 *        \p marked (their id) when they have none: what edit mode traces besides the outline.
 */
[[nodiscard]] QPainterPath warpCurves(const Composition& c, int marked);

/**
 * \brief The box \p w's warp is in fractions of: their block laid out flat, where it sits as the bent block is
 *        placed (blockRect()), in balloon coordinates. Empty for words with no text.
 */
[[nodiscard]] QRectF warpBox(const Composition& c, const Words& w);

/**
 * \brief The handles of \p w's curve (warpCurve()), in balloon coordinates (Bezier::grips()): told apart by
 *        Handle::part, a Bezier::Part; \p picked is the node whose handles show, -1 for none. Words with no warp
 *        have their dormant straight curve's only when \p marked, so a balloon's other words show nothing.
 */
[[nodiscard]] QList<Handle> warpHandles(const Composition& c, const Words& w, int picked, bool marked);

/**
 * \brief \p w with their warp's \p handle dragged to \p to (balloon coordinates), with the nodes
 *        \p picked when it is one of them: warpHandles()' inverse, measured against \p c as it was at the press.
 *
 * Words with no warp are bent by it from their straight curve; dragged back straight, they have none again. A bend
 * moves the bent block, and its placing moves the flat one the curve is in fractions of: the drag is corrected until
 * the handle is under \p to again.
 */
[[nodiscard]] Words dragWarpHandle(const Composition& c, const Words& w, const Handle& handle, const QPointF& to,
                                   const QList<int>& picked);

/**
 * \brief The point of \p c's lettering that stays put when its box changes, in box coordinates: the point of the
 *        frame its block is aligned to (blockRect()).
 *
 * A box that changes for the lettering's sake (*Fit*, *Convert*) is moved so that this point keeps its place on
 * the strip, which is what keeps the words where the author put them.
 */
[[nodiscard]] QPointF textAnchor(const Composition& c);

}  // namespace Geometry

#endif // GEOMETRY_HPP
