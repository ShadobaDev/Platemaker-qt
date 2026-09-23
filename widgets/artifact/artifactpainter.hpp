/**
 * \file artifactpainter.hpp
 * \brief What a bubble looks like — the single definition, shared by the preview and the render.
 *
 * Split out of the record's own header, which was carrying three jobs at once: the model, the drawing and the
 * persistence. This is the half that grows — shapes, tails, style, and the SVG emission the library
 * rasterises — while the model beside it stays a plain struct.
 *
 * Nothing here knows about overlays, projects or the strip. It takes an Artifact and draws it at the
 * origin; where that lands is the caller's business.
 */

#ifndef ARTIFACTPAINTER_HPP
#define ARTIFACTPAINTER_HPP

#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QString>

#include "artifact.hpp"

class QPainter;

// ---------------------------------------------------------------------------
// Geometry — one definition, three consumers
//
// The scene draws it, the SVG writer serialises it, and the library rasterises that SVG. Because all
// three start from these two paths, "what you see" and "what is baked" cannot drift apart.
// ---------------------------------------------------------------------------

//! The balloon (and its tail) as one path, in box coordinates. Empty for a shapeless artifact.
[[nodiscard]] QPainterPath artifactSilhouette(const Artifact& a);

//! What an artifact is made of where a point lands — the three things that carry a colour, and a miss.
enum class ArtifactPart { None, Fill, Outline, Text };

/**
 * @brief What is drawn at @p local (box coordinates), with @p slack units of forgiveness.
 *
 * The same geometry the scene draws, asked a different question — so a tool that acts on *what you are
 * pointing at* cannot disagree with what is on screen. Answered in painting order, topmost first: the
 * lettering, then the stroke band around the silhouette, then the silhouette's inside. A point outside
 * all three is \c None: the transparent corner of a balloon's box is not the balloon.
 *
 * @param a The artifact to check.
 * @param local The point to check, in box coordinates.
 * @param slack Widens the lettering and the stroke band by this much on each side. A 1px outline and a
 *              thin letter are as hard to hit as each other, and the caller knows what a few *screen*
 *              pixels are worth in box units at the current zoom.
 * @return The part of the artifact that is drawn at \p local, or \c None if nothing is.
 */
[[nodiscard]] ArtifactPart artifactPartAt(const Artifact& a, const QPointF& local, qreal slack);

/**
 * @brief The laid-out text as **glyph outlines**, positioned in box coordinates.
 *
 * Outlines rather than a string, because that is what lets the library render a bubble with no font
 * stack and no font installed: by the time the artwork leaves here it is pure geometry. The wrapping is
 * still the text document's, so line breaks are exactly what the editor showed. Clipped to the shape's
 * safe area, so an overlong string cannot bleed past the stroke.
 * 
 * @param a The artifact to check.
 * @return The text outlines, or an empty path if there is no lettering.
 */
[[nodiscard]] QPainterPath artifactTextOutline(const Artifact& a);

/**
 * @brief Everything the artifact actually covers, in balloon coordinates — origin may be negative.
 *
 * The balloon, its tails and its text, plus room for the stroke. A tail can point anywhere, so this is
 * computed rather than assumed: it is the size of the rasterised buffer, the SVG's viewBox, and the
 * item's bounding rect, and those three agreeing is what keeps the preview and the render aligned.
 *
 * Snapped to whole pixels, so the buffer and the viewBox describe one rectangle rather than two
 * roundings of it.
 * 
 * @param a The artifact to check.
 * @return The rectangle that contains everything the artifact draws, in box coordinates.
 */
[[nodiscard]] QRectF artifactBounds(const Artifact& a);

/**
 * @brief How far \p a's style pushes ink beyond the geometry, in balloon pixels.
 *
 * A displacement filter moves pixels *outward* as well as in, so the artwork covers more than its paths
 * do. The bounds — and therefore the buffer and the SVG's filter region — have to allow for it, or the
 * effect is neatly clipped off at the edge it was meant to roughen.
 * 
 * @param a The artifact to check.
 * @return The extra margin the style adds, in balloon coordinates.
 */
[[nodiscard]] qreal artifactStyleMargin(const Artifact& a);

/**
 * @brief The bounds of the artifact, given its silhouette and text paths.
 *
 * @param a The artifact to check.
 * @param silhouette The silhouette path.
 * @param text The text path.
 * @return The rectangle that contains everything the artifact draws, in box coordinates.
 */
[[nodiscard]] QRectF artifactBoundsOf(const Artifact& a,
                                      const QPainterPath& silhouette,
                                      const QPainterPath& text);

// ---------------------------------------------------------------------------
// Rasterising — the single definition of what a bubble looks like
// ---------------------------------------------------------------------------

/**
 * @brief Draws \p a into the rectangle (0, 0, a.box), on whatever painter is given.
 *
 * The scene preview and the PNG both go through here, so "what you see" and "what is baked" cannot
 * drift apart — they are the same code path over the same numbers.
 * 
 * @param painter The painter to draw into.
 * @param a The artifact to draw.
 */
void paintArtifact(QPainter& painter, const Artifact& a);

/**
 * @brief Draws \p a from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, and any shape with text lays out a
 * document and builds glyph outlines — while a repaint happens on every scroll, zoom and selection
 * change. A caller that holds the paths draws through here; paintArtifact() is the one-shot form.
 * 
 * @param painter The painter to draw into.
 * @param a The artifact to draw.
 * @param silhouette The silhouette path.
 * @param text The text path.
 */
void paintArtifactPaths(QPainter& painter, const Artifact& a,
                        const QPainterPath& silhouette, const QPainterPath& text);

/**
 * @brief Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
 * @param a The artifact to rasterise.
 * @return The rasterised image.
 */
[[nodiscard]] QImage renderArtifact(const Artifact& a);

/**
 * @brief The box height that fits \p a's text at its current width (its width, and a sane floor, are kept).
 * @param a The artifact to check.
 * @return The fitted box size.
 */
[[nodiscard]] QSize fittedBox(const Artifact& a);

/**
 *  Human-readable label for the artifact list — the first line of text, or the shape's name if empty.
 *  How much of the lettering names an object in a list. Long enough to tell two lines apart, short
 *  enough to leave room for what else a row carries.
 */
inline constexpr int k_labelChars = 32;

/**
 * @brief Gets the human-readable label for the artifact.
 * @param a The artifact to label.
 * @return The label string.
 */
[[nodiscard]] QString artifactLabel(const Artifact& a);

#endif // ARTIFACTPAINTER_HPP
