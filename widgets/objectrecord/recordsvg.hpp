/**
 * \file recordsvg.hpp
 * \brief A bubble as an SVG document — the asset the library renders — and its content, the record as the
 *        workspace keeps it.
 *
 * The bubble **is** its content (contentOf()): the workspace stores it with the overlay
 * (`StripOverlay::content`), and the SVG is derived from it, written again whenever the content changes. The
 * file carries the resolved artwork every renderer can draw and, in a private namespace every renderer
 * ignores, a copy of the content (`<pm:content>`), which only *Import* reads: an SVG of ours brought into
 * another workspace becomes a bubble again.
 *  - **Any renderer draws it** — librsvg at render time, a browser, Inkscape.
 *  - **A hand-drawn shape is the same file with no `pm:`** — flat, still rendering, just not re-typable.
 *
 * Text is written as outlines (see Geometry::textOutline()), so a rendered bubble needs no font installed;
 * the content's words and font are what make it editable again on a machine that has one.
 */

#ifndef RECORDSVG_HPP
#define RECORDSVG_HPP

#include <QByteArray>
#include <QString>

#include <platemaker/models/processing_steps.hpp>

#include <string>
#include <vector>

#include "objectrecord.hpp"

namespace Svg {

/**
 * \brief The private namespace the editor's parameters live in; ignored by every SVG renderer.
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
 * \brief Serialises \p a to a standalone SVG document.
 *
 * The viewBox is the record's box, in strip-scale pixels, so the library rasterises it 1:1 at scale
 * 1.0 and re-renders it sharp at any other scale.
 *
 * \param picture For an **artwork** record only: the bytes of the picture it names. They are embedded
 *                as a data URI rather than referenced, because the library hands the renderer a buffer
 *                with no base path (`vips_svgload_buffer` → librsvg), so a relative href has nothing to
 *                resolve against and would render as nothing. Embedding also keeps the workspace
 *                self-contained, which is the same reason the import copies the file in the first place.
 * \param mime    The picture's media type, e.g. `image/png`.
 *
 * An artwork record with no bytes returns empty: a wrapper that cannot draw its picture is worse than
 * no file at all, since the object would silently render as its lettering alone.
 */
[[nodiscard]] QByteArray write(const ObjectRecord& a, const QByteArray& picture = {},
                                       const QString& mime = {});

/**
 * \brief The record's content, as the workspace stores it and the SVG copies it: its composition's JSON, compact
 *        (toJson(const Composition&)), the lettering's family the one the outlines were set for (the default's own
 *        name for an empty one).
 */
[[nodiscard]] QString contentOf(const ObjectRecord& a);

/**
 * \brief The record @p content describes (contentOf()); a content in the record's own form, saved before it was a
 *        composition's, is read too, until the first release.
 * \param ok Set to whether @p content is a JSON object; a default record comes back when it is not.
 */
[[nodiscard]] ObjectRecord fromContent(const std::string& content, bool* ok = nullptr);

/**
 * \brief Reads the record back out of \p svg: its `<pm:content>`, or, from a file written before that, its old
 *        `pm:` attributes (read until the first release, which no such file can reach).
 *
 * \param svg The document's bytes.
 * \param ok  Set to false when the file carries neither — a hand-drawn or externally edited asset. That is
 *            not an error: the caller keeps it as a flat, still-rendering overlay rather than replacing it
 *            with a default bubble.
 * \return The reconstructed record, or a default one when \p ok comes back false.
 */
[[nodiscard]] ObjectRecord read(const QByteArray& svg, bool* ok = nullptr);

/**
 * \brief One project's authoring records, from its overlays' content (`StripOverlay::content`).
 *
 * An overlay saved before the content was kept with it is read from its own file (read()), and carries its
 * content from its next edit. One with neither is simply absent from the result — the caller shows it as a
 * flat, still-rendering overlay.
 *
 * \param overlays The overlays to read from.
 * \return A map of overlay uid → record, for every overlay with a content or a file of ours.
 */
[[nodiscard]] ObjectRecord::Map readOverlays(
    const std::vector<Platemaker::Models::StripOverlay>& overlays);

/**
 * \brief The media type of \p file, from its suffix — what a data URI has to declare.
 */
[[nodiscard]] QString pictureMime(const QString& file);

/**
 * \brief Writes \p a into a workspace's `overlays/` directory as an SVG — the file the render composites.
 *
 * \param reusePath The file this overlay already owns, overwritten in place; empty for a new overlay,
 *                  which is then named by content hash. Ignored for a lettered picture, whose wrapper is
 *                  always named by its content (the path may be the picture itself).
 * \return The asset's absolute path, or empty when it could not be written.
 */
[[nodiscard]] QString writeFile(const QString& overlaysDir, const ObjectRecord& a,
                                       const QString& reusePath = {});

/**
 * \brief The family a file's lettering was outlined in instead of its own — `pm:fontFallback` — or empty.
 *
 * Written when the record's family was not installed at the time (Geometry::fontFallback()), so a file
 * baked in a stand-in can be told from one baked in its own font — both carry the same family in their content —
 * and re-set once the font is back. Read the way the recipe is: by namespace, never by prefix.
 */
[[nodiscard]] QString bakedFontFallback(const QByteArray& svg);

} // namespace Svg

#endif // RECORDSVG_HPP
