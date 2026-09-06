/**
 * \file artifactsvg.h
 * \brief A bubble as an SVG document — the on-disk format, and the way back from it.
 *
 * The overlay handed to the library **is** this file. There is no separate authoring sidecar and no
 * rasterised PNG: the SVG carries both the resolved artwork every renderer can draw and, in a private
 * namespace every renderer ignores, the parameters the editor needs to re-solve it.
 *
 * That is the Inkscape pattern (`sodipodi:type="star"` beside a real `<path>`), and it buys three
 * things:
 *  - **Any renderer draws it** — librsvg at render time, a browser, Inkscape.
 *  - **The editor re-solves it** from `pm:*` when the box is dragged or the text retyped.
 *  - **A hand-drawn shape is the same file with no `pm:*`** — flat, still rendering, just not
 *    re-typable. One format, no second import path.
 *
 * Text is written as outlines (see artifactTextOutline()), so a rendered bubble needs no font
 * installed; `pm:text` and `pm:font` are what make it editable again on a machine that has one.
 */

#ifndef ARTIFACTSVG_H
#define ARTIFACTSVG_H

#include <QByteArray>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include <vector>

#include "textartifact.h"

//! The private namespace the editor's parameters live in; ignored by every SVG renderer.
inline constexpr char k_pmNamespace[] = "https://platemaker.dev/ns/bubble/1";

/**
 * @brief Serialises \p a to a standalone SVG document.
 *
 * The viewBox is the artifact's box, in strip-scale pixels, so the library rasterises it 1:1 at scale
 * 1.0 and re-renders it sharp at any other scale.
 */
[[nodiscard]] QByteArray artifactToSvg(const TextArtifact& a);

/**
 * @brief Reads the `pm:*` parameters back out of \p svg.
 *
 * @param svg The document's bytes.
 * @param ok  Set to false when the file carries no `pm:*` parameters — a hand-drawn or externally
 *            edited asset. That is not an error: the caller keeps it as a flat, still-rendering
 *            overlay rather than replacing it with a default bubble.
 * @return The reconstructed artifact, or a default one when \p ok comes back false.
 */
[[nodiscard]] TextArtifact artifactFromSvg(const QByteArray& svg, bool* ok = nullptr);

/**
 * @brief Reads one project's authoring records back out of its overlays' SVG assets.
 *
 * This is what replaced the authoring sidecar: the records are not stored a second time, they are read
 * from the files the library already references. Nothing to keep in sync, and nothing to lose
 * separately from the artwork.
 *
 * An overlay whose asset is missing, unreadable, or carries no `pm:*` parameters is simply absent from
 * the result — the caller shows it as a flat, still-rendering overlay.
 */
[[nodiscard]] ArtifactMap artifactsFromOverlays(
    const std::vector<Platemaker::Models::StripOverlay>& overlays);

#endif // ARTIFACTSVG_H
