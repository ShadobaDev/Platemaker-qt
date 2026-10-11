#ifndef PROFILEDISTINCTION_HPP
#define PROFILEDISTINCTION_HPP

#include "badge.hpp"

#include <platemaker/models/canvas_profile.hpp>

#include <QList>
#include <QString>

#include <vector>

class QPalette;

/**
 * @brief How a canvas profile is told from others of its name — names may repeat — in every place a
 *        profile is listed: the profile manager, the Templates dialog, a project's profile list and the
 *        list a profile is linked from.
 *
 * One source for all of them, from the library's distinguishProfile(): an identical twin is a
 * *Duplicate*; a profile that differs shows **what** differs, e.g. "2000×8080", because that is what the
 * artist can recognise — an id, a hash or a copy number says nothing about which profile is which.
 * The first differing field is the short form; every one is in the tooltip.
 */

//! The chips for @p cp: *Duplicate* and/or the first difference. None for a unique name.
[[nodiscard]] QList<Badge> profileDistinctionBadges(const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                                    const Platemaker::Models::CanvasProfile&              cp,
                                                    const QPalette&                                       colours);

//! The same as plain text, for a list without chips: "2000×8080", "duplicate", or empty.
[[nodiscard]] QString profileDistinctionText(const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                             const Platemaker::Models::CanvasProfile&              cp);

/**
 * @brief The same for a file name: "_2000x8080" — so the template files of two profiles of one name do
 *        not overwrite each other, and the title a drawing app shows tells them apart too.
 *
 * Empty for a unique name, and for a duplicate with nothing else different: its file would be byte for
 * byte its twin's, so they may share it.
 */
[[nodiscard]] QString profileFileSuffix(const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                        const Platemaker::Models::CanvasProfile&              cp);

#endif // PROFILEDISTINCTION_HPP
