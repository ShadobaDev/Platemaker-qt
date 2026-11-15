#ifndef NAMES_HPP
#define NAMES_HPP

#include <QList>
#include <QString>
#include <QStringView>

#include "propertygroups.hpp"   // the enums these name

/**
 * @file
 * \brief What each kind of shape, fill, edge, line and texture is called: in a file, and on screen.
 *
 * The two are deliberately separate: a persisted name may never change, and a displayed one must be free to.
 * One mapping per kind, used by every persistence path, read from the kind's table entry (shapes.hpp,
 * styles.hpp), so appending a kind cannot be a silent out-of-bounds read on one side and a compiler error on
 * neither.
 */

//! The persisted name of @p s.
[[nodiscard]] const char* shapeName(ShapeProperties::Kind s);

/**
 * \brief What to call \p s on screen, translated — as opposed to shapeName(), which is what it is
 *        called in a file.
 *
 * It lives here rather than in the shape picker because the picker is not the only thing that names a
 * shape — *Convert to ▸* does too, and two lists would drift.
 */
[[nodiscard]] QString shapeTitle(ShapeProperties::Kind s);

/**
 * \brief Every **kind of silhouette**, in a fixed order: what shapeFromName() reads and the tests walk.
 *        The picker offers templates instead (shapes.hpp), several of which may share a kind.
 *
 * `Kind::None` is deliberately **not** in it. It is not a silhouette to choose between; it is the absence of
 * one, which is a different *kind* of object — no fill, no outline, no line style, nothing for a tail to
 * leave from. Choosing it belongs to *Convert to ▸*, and a picker that offered it would let a property
 * control change what the object is.
 */
[[nodiscard]] const QList<ShapeProperties::Kind>& shapeOrder();

//! Parses \p name; anything unrecognised falls back to a Box, so an unknown shape still draws.
[[nodiscard]] ShapeProperties::Kind shapeFromName(QStringView name);

//! The persisted name of a fill, and the way back (unknown: Solid); what to call it on screen.
[[nodiscard]] const char* patternName(Paint::Kind f);
[[nodiscard]] Paint::Kind patternFromName(QStringView name);
[[nodiscard]] QString     patternTitle(Paint::Kind f);

//! The persisted name of an edge, and the way back (unknown: None); what to call it on screen.
[[nodiscard]] const char*           edgeName(ShapeProperties::Edge e);
[[nodiscard]] ShapeProperties::Edge edgeFromName(QStringView name);
[[nodiscard]] QString               edgeTitle(ShapeProperties::Edge e);

//! The persisted name of a line or a texture, and the way back (unknown: Clean, None) — same contract as
//! shapeName().
[[nodiscard]] const char*             lineName(StyleProperties::Line l);
[[nodiscard]] StyleProperties::Line   lineFromName(QStringView name);
[[nodiscard]] const char*             textureName(TextureProperties::Kind t);
[[nodiscard]] TextureProperties::Kind textureFromName(QStringView name);
//! What to call a line or a texture on screen, translated — same contract as shapeTitle().
[[nodiscard]] QString lineTitle(StyleProperties::Line l);
[[nodiscard]] QString textureTitle(TextureProperties::Kind t);

#endif // NAMES_HPP
