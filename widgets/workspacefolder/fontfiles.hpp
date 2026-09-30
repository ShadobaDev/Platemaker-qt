#ifndef FONTFILES_HPP
#define FONTFILES_HPP

/**
 * \file fontfiles.hpp
 * \brief Which files an installed font family is made of — what a package needs to carry a font along.
 *
 * Qt answers "is this family available" but not "from which file": QFontDatabase has no path API. The
 * platform's own font system does, so this asks it directly.
 */

#include <QString>
#include <QStringList>

/**
 * @brief Every file of the installed font family \p family — all its styles, since a bubble's text may be
 *        set in any of them — as absolute paths, each once.
 *
 * Windows: DirectWrite's system collection (which includes fonts installed for the current user), each
 * font's face → its files → the local loader's path. A face Windows synthesises (a bold made by
 * emboldening the regular) is skipped: it has no file of its own.
 *
 * @return Empty when the family is not installed, when its files are not local, or on a platform this
 *         does not know yet — which a caller reports as a font that could not be packed.
 */
[[nodiscard]] QStringList installedFontFiles(const QString& family);

#endif // FONTFILES_HPP
