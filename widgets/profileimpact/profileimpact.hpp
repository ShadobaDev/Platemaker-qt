#ifndef PROFILEIMPACT_HPP
#define PROFILEIMPACT_HPP

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/workspace.hpp>

#include <QList>
#include <QString>

#include <vector>

class QWidget;

/**
 * \brief What saving a canvas-profile change would put out of sync, asked before the change lands.
 *
 * A canvas profile is rare to change once templates exist and pages are drawn against its margins, so
 * when it does change, the artist is told which pages, in which projects, it reaches — every project of
 * the workspace, not only those with a window open — and may still back out. The verdict is the
 * library's (ProjectItem::detectFileStaleness()), asked about the palette as it would be.
 */

/**
 * \brief One project the change reaches: how many of its pages it newly puts out of sync, by what each
 *        will render with.
 *
 * A profile matches a page by its exact canvas size, so a margin edit does not always mean new margins:
 * in safe-area mode the margins are added to the canvas size, which then no longer fits the pages drawn
 * on the old one.
 */
struct ProfileImpact {
    QString projectName;
    int     newMargins   = 0;   //!< Still fit the same profile, whose margins changed.
    int     otherProfile = 0;   //!< Now fit a different profile.
    int     noProfile    = 0;   //!< Fit no profile any more: rendered without margins.
    int     unknownSize  = 0;   //!< Rendered before page sizes were recorded, so what they fit cannot be told.

    [[nodiscard]] int pages() const { return newMargins + otherProfile + noProfile + unknownSize; }
};

/**
 * \brief Pages each project would newly put out of sync if the workspace's canvas palette became @p palette.
 *
 * Pages already out of sync stay out of the count: the question is about this change. What each page
 * will render with is the library's matcher's answer (Core::CanvasProfileMatcher), the one the render
 * uses. A profile not saved yet has no id; it is judged under a placeholder one, which no page can have
 * recorded.
 *
 * \param ws      The workspace as it is.
 * \param palette The canvas palette as it would be.
 * \return The projects it reaches, in workspace order; empty when it reaches no page.
 */
[[nodiscard]] QList<ProfileImpact> canvasPaletteImpact(const Platemaker::Models::Workspace&                  ws,
                                                       const std::vector<Platemaker::Models::CanvasProfile>& palette);

//! @p palette with @p profile in it: replacing the one of the same id, or appended when it has none yet.
[[nodiscard]] std::vector<Platemaker::Models::CanvasProfile> withProfile(
    std::vector<Platemaker::Models::CanvasProfile> palette, const Platemaker::Models::CanvasProfile& profile);

/**
 * \brief Asks, when @p palette reaches any page, whether to save it anyway.
 *
 * \param parent  The dialog the change is being made in.
 * \param ws      The workspace as it is.
 * \param palette The canvas palette as it would be.
 * \return \c true to go ahead: nothing is reached, or the artist chose to save.
 */
[[nodiscard]] bool confirmCanvasPalette(QWidget* parent, const Platemaker::Models::Workspace& ws,
                                        const std::vector<Platemaker::Models::CanvasProfile>& palette);

#endif // PROFILEIMPACT_HPP
