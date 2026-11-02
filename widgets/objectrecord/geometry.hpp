/**
 * \file geometry.hpp
 * \brief What a bubble **is**, as shapes on the page: its silhouette, its lettering and the room each
 *        takes — the single definition, shared by the canvas, the SVG and the render.
 *
 * Three consumers start from these paths: the canvas draws them (recordpainter.hpp), the SVG writer
 * serialises them (recordsvg.hpp), and the library rasterises that SVG. Because all three start here,
 * "what you see" and "what is baked" cannot drift apart. Nothing here draws or knows about overlays,
 * projects or the strip: it takes an ObjectRecord and answers in box coordinates.
 *
 * Where each part is:
 *  - shapes.cpp: every shape's outline and text area, one table entry each (shapes.hpp);
 *  - silhouette.cpp: the outline with its tails, what it covers, and what a point lands on;
 *  - lettering.cpp: the words set and outlined, and fitting a box to them or them to a box.
 */

#ifndef GEOMETRY_HPP
#define GEOMETRY_HPP

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include "objectrecord.hpp"

namespace Geometry {

// ---------------------------------------------------------------------------
// The silhouette — silhouette.cpp
// ---------------------------------------------------------------------------

//! The balloon (and its tail) as one path, in box coordinates. Empty for a shapeless record.
[[nodiscard]] QPainterPath silhouette(const ObjectRecord& a);

/**
 * @brief What the silhouette is the union of: the balloon's outline first, then each tail that is drawn,
 *        each whole, overlapping where they join. Empty for a shapeless record.
 *
 * For a line that follows each part's own edge rather than the joined one (the dry brush's hairs run on
 * past a tail's root along the balloon, and start inside it along the tail).
 */
[[nodiscard]] QList<QPainterPath> silhouetteParts(const ObjectRecord& a);

/**
 * @brief Which of \p a's tails are actually drawn, one entry per tail.
 *
 * A tail whose tip lies inside the balloon draws nothing — there is no room for it to emerge — but it
 * still exists, and its handle still has to be reachable. The editor asks this to know which tips are
 * visible to the eye and which only to the selection chrome.
 */
[[nodiscard]] QList<bool> tailsDrawn(const ObjectRecord& a);

//! What a record is made of where a point lands — the three things that carry a colour, and a miss.
enum class Part { None, Fill, Outline, Text };

/**
 * @brief What is drawn at @p local (box coordinates), with @p slack units of forgiveness.
 *
 * The same geometry the scene draws, asked a different question — so a tool that acts on *what you are
 * pointing at* cannot disagree with what is on screen. Answered in painting order, topmost first: the
 * lettering, then the stroke band around the silhouette, then the silhouette's inside. A point outside
 * all three is \c None: the transparent corner of a balloon's box is not the balloon.
 *
 * @param a The record to check.
 * @param local The point to check, in box coordinates.
 * @param slack Widens the lettering and the stroke band by this much on each side. A 1px outline and a
 *              thin letter are as hard to hit as each other, and the caller knows what a few *screen*
 *              pixels are worth in box units at the current zoom.
 * @return The part of the record that is drawn at \p local, or \c None if nothing is.
 */
[[nodiscard]] Part partAt(const ObjectRecord& a, const QPointF& local, qreal slack);

/**
 * @brief Everything the record actually covers, in balloon coordinates — origin may be negative.
 *
 * The balloon, its tails and its text, plus room for the stroke. A tail can point anywhere, so this is
 * computed rather than assumed: it is the size of the rasterised buffer, the SVG's viewBox, and the
 * item's bounding rect, and those three agreeing is what keeps the preview and the render aligned.
 *
 * Snapped to whole pixels, so the buffer and the viewBox describe one rectangle rather than two
 * roundings of it.
 *
 * @param a The record to check.
 * @return The rectangle that contains everything the record draws, in box coordinates.
 */
[[nodiscard]] QRectF bounds(const ObjectRecord& a);

/**
 * @brief How far \p a's style pushes ink beyond the geometry, in balloon pixels.
 *
 * A displacement filter moves pixels *outward* as well as in, so the artwork covers more than its paths
 * do. The bounds — and therefore the buffer and the SVG's filter region — have to allow for it, or the
 * effect is neatly clipped off at the edge it was meant to roughen. Read from the style's recipe
 * (styles.cpp), the same numbers its filter is written from.
 *
 * @param a The record to check.
 * @return The extra margin the style adds, in balloon coordinates.
 */
[[nodiscard]] qreal styleMargin(const ObjectRecord& a);

/**
 * @brief The bounds of the record, given its silhouette and text paths.
 *
 * @param a The record to check.
 * @param silhouette The silhouette path.
 * @param text The text path.
 * @return The rectangle that contains everything the record draws, in box coordinates.
 */
[[nodiscard]] QRectF boundsOf(const ObjectRecord& a, const QPainterPath& silhouette, const QPainterPath& text);

// ---------------------------------------------------------------------------
// The lettering — lettering.cpp
// ---------------------------------------------------------------------------

/**
 * @brief The laid-out text as **glyph outlines**, positioned in box coordinates.
 *
 * Outlines rather than a string, because that is what lets the library render a bubble with no font
 * stack and no font installed: by the time the artwork leaves here it is pure geometry. The wrapping is
 * still the text document's, so line breaks are exactly what the editor showed. Clipped to the shape's
 * safe area, so an overlong string cannot bleed past the stroke.
 *
 * @param a The record to check.
 * @return The text outlines, or an empty path if there is no lettering.
 */
[[nodiscard]] QPainterPath textOutline(const ObjectRecord& a);

/**
 * @brief The family \p a's lettering is actually outlined in when its own is not installed, or empty.
 *
 * `QFont` never fails on a missing family — it substitutes, silently — so an outline built while the font
 * is absent is the same words in another face, and nothing in the record says so. This names the stand-in
 * (Qt's own choice, as `QFontInfo` reports it), which is what the SVG records as `pm:fontFallback`. Empty
 * when there are no words, no family is asked for, or the family is available — including one a workspace
 * registered for itself.
 */
[[nodiscard]] QString fontFallback(const ObjectRecord& a);

/**
 * @brief The family an empty `TextProperties::family` is drawn in on this machine — the application's
 *        default font (Segoe UI on Windows 11).
 *
 * An empty family means "the default", and the default is a property of the machine, not of the file: a
 * bubble drawn here in Segoe UI would be re-set in something else on a machine whose default differs, with
 * nothing in the file to tell. So this is what a file records in place of an empty family, and what a
 * preset with no font of its own is taken to mean when matching.
 */
[[nodiscard]] QString defaultFamily();

/**
 * @brief The rectangle \p a's lettering may occupy, in box coordinates — inside the outline, less the
 *        stroke and the padding. A text object's whole box.
 */
[[nodiscard]] QRectF textArea(const ObjectRecord& a);

/**
 * @brief The size of \p a's laid-out lettering — the widest line by the height of all of them.
 *
 * Independent of the box: the lettering is never wrapped, so where the box is and how big does not
 * change it. Empty when there is no text.
 */
[[nodiscard]] QSizeF textBlockSize(const ObjectRecord& a);

/**
 * @brief Whether a block of lettering of size \p block fits \p a's text area.
 *
 * Pure geometry — no font is touched — so it is the half of fitting that can be tested anywhere.
 * Half a pixel of slack, so a box rounded to whole pixels does not overflow by its own rounding.
 */
[[nodiscard]] bool fits(const ObjectRecord& a, const QSizeF& block);

//! True when \p a's lettering does not fit its text area — what the selection shows in red.
[[nodiscard]] bool textOverflows(const ObjectRecord& a);

/**
 * @brief The smallest box, **on both axes**, whose text area holds a block of size \p block.
 *
 * Neither side goes below the grips' own minimum (40 px), so a short word cannot collapse the shape.
 * With a negative padding the text area is larger than the box, and the floor is often where it stops.
 * Pure geometry, like fits(): fittedBox() is this with the lettering measured.
 */
[[nodiscard]] QSize fitBoxFor(const ObjectRecord& a, const QSizeF& block);

/**
 * @brief The smallest box that holds \p a's lettering, on both axes — *Fit to text*.
 * @param a The record to fit. Its box is the starting point only.
 * @return The fitted box size; the current box when there is no text.
 */
[[nodiscard]] QSize fittedBox(const ObjectRecord& a);

/**
 * @brief The largest padding, no higher than \p a's own, at which a block of size \p block fits —
 *        below zero if the lettering has to reach past the outline.
 *
 * The box stays as it is; the padding gives way. It only ever lowers the padding: lettering that
 * already fits keeps the room the author gave it. A text object has no padding to give, so its own
 * value comes back. Pure geometry, like fitBoxFor().
 */
[[nodiscard]] int paddingToFit(const ObjectRecord& a, const QSizeF& block);

//! paddingToFit() with \p a's lettering measured — *Fix padding*.
[[nodiscard]] int fittingPadding(const ObjectRecord& a);

/**
 * @brief The point of \p a's lettering that stays put when its box changes, in box coordinates.
 *
 * Horizontally the side the lines are aligned to — the left edge, the right edge, or the middle — and
 * vertically the middle of the block. A box that changes for the lettering's sake (*Fit*, *Convert*) is
 * moved so that this point keeps its place on the strip, which is what keeps the words where the author
 * put them. Pure geometry given the block's size, like fits().
 */
[[nodiscard]] QPointF textAnchorFor(const ObjectRecord& a, const QSizeF& block);

//! textAnchorFor() with \p a's lettering measured.
[[nodiscard]] QPointF textAnchor(const ObjectRecord& a);

}  // namespace Geometry

#endif // GEOMETRY_HPP
