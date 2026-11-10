#include "profileimpact.hpp"

#include <platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp>
#include <platemaker/models/output_profile.hpp>

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

#include <algorithm>

using Platemaker::Models::CanvasProfile;
using Platemaker::Models::OutputProfile;
using Platemaker::Models::Workspace;

namespace {

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("ProfileImpact", text, disambiguation, n);
}

//! The id a profile not saved yet is judged under: the library mints real ids on commit, and no page can
//! have recorded this one.
std::string placeholderId(std::size_t i) { return "unsaved-" + std::to_string(i); }

} // namespace

QList<ProfileImpact> canvasPaletteImpact(const Workspace& ws, const std::vector<CanvasProfile>& palette)
{
    std::vector<CanvasProfile> candidate = palette;
    for (std::size_t i = 0; i < candidate.size(); ++i)
        if (candidate[i].id.empty())
            candidate[i].id = placeholderId(i);

    // Only the canvas axis is read, and it is the pages' own, so the output profile does not matter.
    const OutputProfile anyOutput;
    QList<ProfileImpact> impact;
    for (const auto& project : ws.projectItems) {
        const auto before = project.detectFileStaleness(ws.canvasProfiles(), anyOutput);
        const auto after  = project.detectFileStaleness(candidate, anyOutput);
        const Platemaker::Core::CanvasProfileMatcher matcher(candidate, project.canvasProfileIds());

        ProfileImpact p{QString::fromStdString(project.name)};
        const auto& inputs = project.getInputImages();
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            if (!after.inputs[i].canvasProfile || before.inputs[i].canvasProfile)
                continue;
            const auto& page = inputs[i];
            if (page.width <= 0 || page.height <= 0) {
                ++p.unknownSize;
                continue;
            }
            // A page matched only by an unlinked profile renders without one, as the render does it.
            const auto match = matcher.resolveForSize(page.width, page.height);
            if (match.status != Platemaker::Core::ProfileMatchResult::Status::Matched)
                ++p.noProfile;
            else if (match.profile->id == page.canvasProfileId)
                ++p.newMargins;
            else
                ++p.otherProfile;
        }
        if (p.pages() > 0)
            impact.append(p);
    }
    return impact;
}

std::vector<CanvasProfile> withProfile(std::vector<CanvasProfile> palette, const CanvasProfile& profile)
{
    const auto it = profile.id.empty()
        ? palette.end()
        : std::find_if(palette.begin(), palette.end(), [&](const CanvasProfile& p) { return p.id == profile.id; });
    if (it != palette.end())
        *it = profile;
    else
        palette.push_back(profile);
    return palette;
}

bool confirmCanvasPalette(QWidget* parent, const Workspace& ws, const std::vector<CanvasProfile>& palette,
                          PaletteChange change)
{
    const QList<ProfileImpact> impact = canvasPaletteImpact(ws, palette);
    if (impact.isEmpty())
        return true;

    QStringList lines;
    for (const auto& p : impact) {
        lines << p.projectName;
        if (p.noProfile)
            lines << tr("    %n page(s) no longer fit any profile and render without margins", "", p.noProfile);
        if (p.newMargins)
            lines << tr("    %n page(s) keep their profile and render with its new margins", "", p.newMargins);
        if (p.otherProfile)
            lines << tr("    %n page(s) now fit another profile and render with its margins", "", p.otherProfile);
        if (p.unknownSize)
            lines << tr("    %n page(s) were rendered before page sizes were recorded; a re-render settles them",
                        "", p.unknownSize);
    }

    const bool deleting = change == PaletteChange::Delete;
    QMessageBox box(QMessageBox::Question, tr("Pages go out of sync"),
        (deleting ? tr("Deleting the profile changes how already rendered pages come out:")
                  : tr("Saving changes how already rendered pages come out:"))
            + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
            + (deleting ? tr("A profile fits a page only when its canvas size is exactly the page's size. "
                             "Delete anyway?")
                        : tr("A profile fits a page only when its canvas size is exactly the page's size. "
                             "Save anyway?")),
        QMessageBox::Cancel, parent);
    const QAbstractButton* goAhead = deleting ? box.addButton(tr("Delete"), QMessageBox::AcceptRole)
                                              : box.addButton(QMessageBox::Save);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == goAhead;
}
