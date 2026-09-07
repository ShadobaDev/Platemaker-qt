/**
 * \file artifactpainter.h
 * \brief What a bubble looks like — the single definition, shared by the preview and the render.
 *
 * Split out of textartifact.h, which was carrying three jobs at once: the model, the drawing and the
 * persistence. This is the half that grows — shapes, tails, style, and the SVG emission the library
 * rasterises — while the model beside it stays a plain struct.
 *
 * Nothing here knows about overlays, projects or the strip. It takes a TextArtifact and draws it at the
 * origin; where that lands is the caller's business.
 */

#ifndef ARTIFACTPAINTER_H
#define ARTIFACTPAINTER_H

#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QString>

#include "textartifact.h"

class QPainter;

// ---------------------------------------------------------------------------
// Geometry — one definition, three consumers
//
// The scene draws it, the SVG writer serialises it, and the library rasterises that SVG. Because all
// three start from these two paths, "what you see" and "what is baked" cannot drift apart.
// ---------------------------------------------------------------------------

//! The balloon (and its tail) as one path, in box coordinates. Empty for a shapeless artifact.
[[nodiscard]] QPainterPath artifactSilhouette(const TextArtifact& a);

/**
 * @brief The laid-out text as **glyph outlines**, positioned in box coordinates.
 *
 * Outlines rather than a string, because that is what lets the library render a bubble with no font
 * stack and no font installed: by the time the artwork leaves here it is pure geometry. The wrapping is
 * still the text document's, so line breaks are exactly what the editor showed. Clipped to the shape's
 * safe area, so an overlong string cannot bleed past the stroke.
 */
[[nodiscard]] QPainterPath artifactTextOutline(const TextArtifact& a);

/**
 * @brief Everything the artifact actually covers, in balloon coordinates — origin may be negative.
 *
 * The balloon, its tails and its text, plus room for the stroke. A tail can point anywhere, so this is
 * computed rather than assumed: it is the size of the rasterised buffer, the SVG's viewBox, and the
 * item's bounding rect, and those three agreeing is what keeps the preview and the render aligned.
 *
 * Snapped to whole pixels, so the buffer and the viewBox describe one rectangle rather than two
 * roundings of it.
 */
[[nodiscard]] QRectF artifactBounds(const TextArtifact& a);

/**
 * @brief How far \p a's style pushes ink beyond the geometry, in balloon pixels.
 *
 * A displacement filter moves pixels *outward* as well as in, so the artwork covers more than its paths
 * do. The bounds — and therefore the buffer and the SVG's filter region — have to allow for it, or the
 * effect is neatly clipped off at the edge it was meant to roughen.
 */
[[nodiscard]] qreal artifactStyleMargin(const TextArtifact& a);

//! Same, from paths already resolved — for a caller that keeps them (see OverlayItem).
[[nodiscard]] QRectF artifactBoundsOf(const TextArtifact& a,
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
 */
void paintArtifact(QPainter& painter, const TextArtifact& a);

/**
 * @brief Draws \p a from paths already resolved, so a repaint costs no geometry.
 *
 * Resolving is not cheap — a thought balloon unions eleven circles, and any shape with text lays out a
 * document and builds glyph outlines — while a repaint happens on every scroll, zoom and selection
 * change. A caller that holds the paths draws through here; paintArtifact() is the one-shot form.
 */
void paintArtifactPaths(QPainter& painter, const TextArtifact& a,
                        const QPainterPath& silhouette, const QPainterPath& text);

//! Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
[[nodiscard]] QImage renderArtifact(const TextArtifact& a);

//! The box height that fits \p a's text at its current width (its width, and a sane floor, are kept).
[[nodiscard]] QSize fittedBox(const TextArtifact& a);

//! Human-readable label for the artifact list — the first line of text, or the shape's name if empty.
[[nodiscard]] QString artifactLabel(const TextArtifact& a);

#endif // ARTIFACTPAINTER_H
