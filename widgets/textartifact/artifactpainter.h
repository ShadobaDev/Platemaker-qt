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
#include <QSize>
#include <QString>

#include "textartifact.h"

class QPainter;

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

//! Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
[[nodiscard]] QImage renderArtifact(const TextArtifact& a);

//! The box height that fits \p a's text at its current width (its width, and a sane floor, are kept).
[[nodiscard]] QSize fittedBox(const TextArtifact& a);

//! Human-readable label for the artifact list — the first line of text, or the shape's name if empty.
[[nodiscard]] QString artifactLabel(const TextArtifact& a);

#endif // ARTIFACTPAINTER_H
