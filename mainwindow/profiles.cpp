#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "project.h"
#include "canvasprofiledialog.h"
#include "managecanvasprofilesdialog.h"
#include "manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"
#include "templatesdialog.h"
#include "renderworker.h"

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QCloseEvent>
#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTabBar>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Canvas profile slots
//
// Profile ids come from the library (Infrastructure::makeUnique*ProfileId), never from this
// file.  They used to be a millisecond timestamp minted here, which meant every profile
// added in one pass of the manage dialog got the *same* id — and a shared id makes the
// second profile unreachable, so it silently dropped out of the "assign profile" list.
// The library draws random ids and rejects any that is already taken.
// ---------------------------------------------------------------------------

void MainWindow::onManageCanvasProfiles()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Open the dialog with a copy of the workspace's profiles. The dialog edits
    // copies, and we only write the results back to the workspace if the user
    // clicks Accept. The dialog preserves templateInfo by id, so we can snapshot
    // it before the dialog and re-attach it afterward (the dialog never edits it).
    ManageCanvasProfilesDialog dlg(this);

    QList<Platemaker::Models::CanvasProfile> profiles(
        m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end());
    dlg.setProfiles(profiles, m_activeCanvasProfileName);

    // Quick-generate from the selected profile. The dialog edits copies, so we
    // write the resulting templateInfo into the LIVE profile (matched by id);
    // it is preserved across the dialog's Accept below (preserve-by-id).
    const QString workspaceDir = QFileInfo(m_workspacePath).absolutePath();
    connect(&dlg, &ManageCanvasProfilesDialog::generateTemplatesRequested,
        this, [this, workspaceDir](const Platemaker::Models::CanvasProfile& selected) {
            // A brand-new profile created in this dialog has no id yet — it isn't
            // in the workspace, so there is nothing stable to attach the template to.
            bool exists = false;
            if (!selected.id.empty())
                for (const auto& p : m_workspace.canvasProfiles())
                    if (p.id == selected.id) { exists = true; break; }

            if (!exists) {
                QMessageBox::information(this, tr("Template"),
                    tr("Save the new profile first (close this dialog), then "
                       "generate its template from the Templates menu."));
                return;
            }

            // Ask before overwriting an existing template. Check the dialog's
            // current (possibly edited) values — `selected` — not the live profile,
            // whose canvas fields are only updated when the dialog is accepted.
            // `selected` carries the preserved templateInfo, so the status reflects
            // any just-made edit (→ Outdated) rather than the stale live state.
            if (!TemplatesDialog::confirmOverwrite(this, selected, workspaceDir))
                return;

            // Render from the dialog's current field values (a copy), then write the
            // resulting template metadata onto the workspace profile through the editor.
            Platemaker::Models::CanvasProfile render = selected;
            QString err;
            if (!TemplatesDialog::generateTemplate(m_workspace, workspaceDir, render, err)) {
                QMessageBox::critical(this, tr("Template"), err);
                return;
            }
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
                .setCanvasProfileTemplateInfo(selected.id, render.templateInfo);
            QMessageBox::information(this, tr("Template"),
                tr("Template generated for \"%1\".")
                    .arg(QString::fromStdString(selected.name)));
        });

    if (dlg.exec() != QDialog::Accepted) return;

    // Hand the whole edited palette to the editor: it mints ids for new profiles, deduplicates,
    // and carries templateInfo from the current profile of the same id (the manage dialog drops
    // that field on its round trip). This replaces the former snapshot / assign / mint / re-attach
    // done by hand here.
    const auto result = dlg.profiles();
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(
        std::vector<Platemaker::Models::CanvasProfile>(result.begin(), result.end()));
    m_activeCanvasProfileName = dlg.activeProfileName();

    setDirty(true);
}
void MainWindow::onNewCanvasProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Prompt for a new profile. The dialog returns a fresh profile without an id or templateInfo; we generate a stable id and leave templateInfo empty.
    CanvasProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    // Ensure the name is unique within the workspace.
    for (const auto& p : m_workspace.canvasProfiles()) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("A canvas profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    // The editor mints a stable id and appends it to the workspace palette.
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).addCanvasProfile(profile);

    // If this is the first profile, make it the active one.
    if (m_workspace.canvasProfiles().size() == 1)
        m_activeCanvasProfileName = QString::fromStdString(profile.name);

    setDirty(true);
}

void MainWindow::onEditActiveCanvasProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Find the active profile by name. The dialog edits a copy, 
    // and we only write the results back to the workspace if the user clicks Accept. 
    // The dialog preserves templateInfo by id, so we can snapshot it before 
    // the dialog and re-attach it afterward (the dialog never edits it).
    // Edit a copy of the palette, then hand it back through the editor (it preserves the ids and
    // carries templateInfo). The vectors are private, so we cannot mutate a live profile in place.
    auto profiles = std::vector<Platemaker::Models::CanvasProfile>(
        m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end());
    const auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const auto& p){ return p.name == m_activeCanvasProfileName.toStdString(); });

    if (it == profiles.end()) {
        QMessageBox::information(this, tr("No Active Profile"),
            tr("No active canvas profile. Use Canvas Profiles → Manage to create one."));
        return;
    }

    CanvasProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    const std::string oldName = it->name;
    const std::string savedId = it->id;
    // CanvasProfileDialog returns a fresh profile without id/templateInfo. Preserve
    // both so project links survive and template staleness is still detectable: if a
    // canvas-affecting field changed, the kept fingerprint won't match → Outdated.
    const auto savedTpl = it->templateInfo;
    *it = dlg.profile();
    it->id           = savedId;
    it->templateInfo = savedTpl;

    if (m_activeCanvasProfileName == QString::fromStdString(oldName))
        m_activeCanvasProfileName = QString::fromStdString(it->name);

    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(std::move(profiles));

    setDirty(true);
}
// ---------------------------------------------------------------------------
// Output profile slots
// ---------------------------------------------------------------------------

void MainWindow::onManageOutputProfiles()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    ManageOutputProfilesDialog dlg(this);

    // Show the user's own profiles plus the code-defined presets in one list. Presets are
    // read-only in the dialog (Edit/Delete disabled) and never persisted; customising one is a
    // Duplicate, which clears its id and yields an ordinary user profile.
    QList<Platemaker::Models::OutputProfile> profiles(
        m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());
    for (const auto& preset : Platemaker::Models::outputProfilePresets())
        profiles.append(preset);
    dlg.setProfiles(profiles, m_activeOutputProfileId);

    // The dialog edits copies, and we only write the results back to the workspace if the user clicks Accept.
    if (dlg.exec() != QDialog::Accepted) return;

    // Hand the edited list to the editor: it drops any preset (presets live in code and are never
    // persisted), mints ids for new profiles, and deduplicates. Replaces the former clear / filter /
    // mint done by hand here.
    const auto dlgProfiles = dlg.profiles();
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(
        std::vector<Platemaker::Models::OutputProfile>(dlgProfiles.begin(), dlgProfiles.end()));
    m_activeOutputProfileId = dlg.activeProfileId();

    setDirty(true);
}

void MainWindow::onNewOutputProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    OutputProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    // Ensure the name is unique within the workspace.
    for (const auto& p : m_workspace.outputProfiles()) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("An output profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    // The editor mints a stable user id (never a preset id) and appends it.
    const std::string newId =
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).addOutputProfile(profile);

    if (m_workspace.outputProfiles().size() == 1)
        m_activeOutputProfileId = QString::fromStdString(newId);

    setDirty(true);
}

void MainWindow::onEditActiveOutputProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Resolve the active profile by id. The dialog edits a copy, and we only write the results
    // back to the workspace if the user clicks Accept.
    const std::string activeId = m_activeOutputProfileId.toStdString();

    // Presets are read-only. This is the *second* way into the editor, next to the Manage
    // dialog's Edit button; leaving it open would make that dialog's guard bypassable with one
    // click from the menu. A preset is never in outputProfiles (code-defined, not persisted), so
    // an active preset id is caught here first.
    if (Platemaker::Models::outputPresetDefById(activeId) != nullptr) {
        const auto preset = Platemaker::Models::resolveOutputProfile(m_workspace, activeId);
        QMessageBox::information(this, tr("Preset profile"),
            tr("\"%1\" is a built-in preset and cannot be edited — it is shared by every "
               "workspace, so it has to mean the same thing everywhere.\n\n"
               "Use Output → Manage → Duplicate to make your own version of it, which you "
               "can then change freely.")
                .arg(preset ? QString::fromStdString(preset->name) : m_activeOutputProfileId));
        return;
    }

    // Edit a copy of the palette, then hand it back through the editor (vectors are private).
    auto profiles = std::vector<Platemaker::Models::OutputProfile>(
        m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());
    const auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const auto& p){ return p.id == activeId; });

    if (it == profiles.end()) {
        QMessageBox::information(this, tr("No Active Profile"),
            tr("No active output profile. Use Output → Manage to create one."));
        return;
    }

    OutputProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    // OutputProfileDialog returns a profile without an id — preserve the stable id so projects
    // that reference this profile keep working. The id is unchanged, so the active profile
    // (tracked by id) needs no update even if the name changed. (An empty id would be minted by
    // replaceOutputProfiles below, but activeId is non-empty here by construction.)
    const std::string savedId = it->id;
    *it = dlg.profile();
    it->id = savedId;

    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(std::move(profiles));

    setDirty(true);
}
