#ifndef STRIPEDIT_ROWGLYPH_HPP
#define STRIPEDIT_ROWGLYPH_HPP

#include <QIcon>
#include <QPalette>
#include <QPixmap>

#include "artifact.hpp"

namespace StripEdit {

//! How wide a row's glyph is, in **points**. The row grows to fit it, which is what makes the object
//! legible rather than a smudge; the list is a column of objects, not of labels.
inline constexpr int k_rowGlyphPx = 24;

/**
 * @brief The object itself, drawn small enough to sit in a row — **a glyph, not a picture**.
 *
 * Built from `artifactSilhouette()`, the same path the scene draws and the SVG writer serialises, so a
 * speech balloon with two tails is distinguishable from a caption box at a glance. There is no rendering
 * here and no library round-trip: the path is already geometry, and asking librsvg for an icon this size
 * would cost the whole filter pipeline to produce something nobody can read anyway.
 *
 * The lettering is left out. At this size it would be a smudge, and the row's *text* is the lettering.
 * A shapeless object — a caption with no balloon — has no silhouette to draw, so it gets **Aa**.
 *
 * The fill is the object's own; the outline is the palette's, which is what keeps a black balloon visible
 * on a dark panel and a white one on a light panel.
 *
 * @param dpr The screen's device pixel ratio. Drawing at one and letting the view scale up is what makes
 *            an icon look soft on a 150% display — so every glyph is drawn at the screen's own density.
 */
[[nodiscard]] QIcon objectGlyph(const Artifact& a, const QPalette& pal, int px = k_rowGlyphPx,
                                qreal dpr = 1.0);

//! Imported artwork is its own glyph: the art itself, scaled down. It has no silhouette to borrow.
[[nodiscard]] QIcon assetGlyph(const QPixmap& art, int px = k_rowGlyphPx, qreal dpr = 1.0);

//! A tail: a curved sliver narrowing to the point it speaks from. Deliberately **not** a V — the tree
//! already draws a chevron next to it, and two arrow-shaped marks in one row read as one control.
[[nodiscard]] QIcon tailGlyph(const QPalette& pal, int px = k_rowGlyphPx, qreal dpr = 1.0);

//! A page of the strip: the sheet the artwork sits on.
[[nodiscard]] QIcon pageGlyph(const QPalette& pal, int px = k_rowGlyphPx, qreal dpr = 1.0);

//! The strip: the pages stacked into the one column everything is composited onto.
[[nodiscard]] QIcon stripGlyph(const QPalette& pal, int px = k_rowGlyphPx, qreal dpr = 1.0);

}  // namespace StripEdit

#endif // STRIPEDIT_ROWGLYPH_HPP
