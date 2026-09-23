/**
 * \file artifactsvg.hpp
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

#ifndef ARTIFACTSVG_HPP
#define ARTIFACTSVG_HPP

#include <QByteArray>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include <vector>

#include "artifact.hpp"

/**
 * @brief The private namespace the editor's parameters live in; ignored by every SVG renderer.
 *
 * **An identifier, not an address.** XML compares a namespace name as a string and never fetches it,
 * so this neither has to resolve nor may ever change once files carry it — it is how a file of ours is
 * told from somebody else's. It points at the repository because that is a name the project actually
 * controls, and the one thing the convention asks is that the name be yours. If the repository is ever
 * renamed this URI stays as it is: a stale link costs the convenience of pasting it into a browser and
 * nothing else.
 *
 * **Versioning.** The `/1` at the end changes only when a file becomes unreadable to an older build —
 * an old reader *should* then fail to recognise it at all, which is exactly what a different namespace
 * achieves. There is no version attribute inside the file: nothing would read it until a released
 * reader meets a newer file. The first compatible revision that needs telling apart adds one, and its
 * absence then means this format.
 */
inline constexpr char k_pmNamespace[] = "https://github.com/ShadobaDev/Platemaker-qt/ns/artifact/1";

/**
 * @brief Serialises \p a to a standalone SVG document.
 *
 * The viewBox is the artifact's box, in strip-scale pixels, so the library rasterises it 1:1 at scale
 * 1.0 and re-renders it sharp at any other scale.
 *
 * @param picture For an **artwork** record only: the bytes of the picture it names. They are embedded
 *                as a data URI rather than referenced, because the library hands the renderer a buffer
 *                with no base path (`vips_svgload_buffer` → librsvg), so a relative href has nothing to
 *                resolve against and would render as nothing. Embedding also keeps the workspace
 *                self-contained, which is the same reason the import copies the file in the first place.
 * @param mime    The picture's media type, e.g. `image/png`.
 *
 * An artwork record with no bytes returns empty: a wrapper that cannot draw its picture is worse than
 * no file at all, since the object would silently render as its lettering alone.
 */
[[nodiscard]] QByteArray artifactToSvg(const Artifact& a, const QByteArray& picture = {},
                                       const QString& mime = {});

/**
 * @brief Reads the `pm:*` parameters back out of \p svg.
 *
 * @param svg The document's bytes.
 * @param ok  Set to false when the file carries no `pm:*` parameters — a hand-drawn or externally
 *            edited asset. That is not an error: the caller keeps it as a flat, still-rendering
 *            overlay rather than replacing it with a default bubble.
 * @return The reconstructed artifact, or a default one when \p ok comes back false.
 */
[[nodiscard]] Artifact artifactFromSvg(const QByteArray& svg, bool* ok = nullptr);

/**
 * @brief Reads one project's authoring records back out of its overlays' SVG assets.
 *
 * This is what replaced the authoring sidecar: the records are not stored a second time, they are read
 * from the files the library already references. Nothing to keep in sync, and nothing to lose
 * separately from the artwork.
 *
 * An overlay whose asset is missing, unreadable, or carries no `pm:*` parameters is simply absent from
 * the result — the caller shows it as a flat, still-rendering overlay.
 * 
 * @param overlays The overlays to read from.
 * @return A map of overlay uid → artifact, for every overlay that carries a readable asset with a `pm:*` group.
 */
[[nodiscard]] ArtifactMap artifactsFromOverlays(
    const std::vector<Platemaker::Models::StripOverlay>& overlays);

#endif // ARTIFACTSVG_HPP
