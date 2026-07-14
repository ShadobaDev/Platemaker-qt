#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "project.h"
#include "canvasprofiledialog.h"
#include "managecanvasprofilesdialog.h"
#include "manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"
#include "templatesdialog.h"
#include "renderworker.h"

#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
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
// ---------------------------------------------------------------------------

static QString nowIsoTag()
{
    return QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz");
}

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
        m_workspace.canvasProfiles.begin(), m_workspace.canvasProfiles.end());
    dlg.setProfiles(profiles, m_activeCanvasProfileName);

    // Quick-generate from the selected profile. The dialog edits copies, so we
    // write the resulting templateInfo into the LIVE profile (matched by id);
    // it is preserved across the dialog's Accept below (preserve-by-id).
    const QString workspaceDir = QFileInfo(m_workspacePath).absolutePath();
    connect(&dlg, &ManageCanvasProfilesDialog::generateTemplatesRequested,
        this, [this, workspaceDir](const Platemaker::Models::CanvasProfile& selected) {
            // A brand-new profile created in this dialog has no id yet — it isn't
            // in the workspace, so there is nothing stable to attach the template to.
            Platemaker::Models::CanvasProfile* live = nullptr;
            if (!selected.id.empty())
                for (auto& p : m_workspace.canvasProfiles)
                    if (p.id == selected.id) { live = &p; break; }

            if (!live) {
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

            // Render from the dialog's current field values (a copy), then copy
            // the resulting metadata onto the live profile.
            Platemaker::Models::CanvasProfile render = selected;
            QString err;
            if (!TemplatesDialog::generateTemplate(m_workspace, workspaceDir, render, err)) {
                QMessageBox::critical(this, tr("Template"), err);
                return;
            }
            live->templateInfo = render.templateInfo;
            QMessageBox::information(this, tr("Template"),
                tr("Template generated for \"%1\".")
                    .arg(QString::fromStdString(selected.name)));
        });

    if (dlg.exec() != QDialog::Accepted) return;

    // Snapshot templateInfo by id — the workspace is its source of truth (the
    // manage dialog never edits it, and the quick button may have updated it).
    std::vector<std::pair<std::string, Platemaker::Models::CanvasTemplateInfo>> savedTpl;
    for (const auto& p : m_workspace.canvasProfiles)
        if (!p.templateInfo.path.empty())
            savedTpl.emplace_back(p.id, p.templateInfo);

    const auto result = dlg.profiles();
    m_workspace.canvasProfiles.assign(result.begin(), result.end());
    m_activeCanvasProfileName = dlg.activeProfileName();

    // Assign IDs to any profiles added without one (dialog doesn't generate them)
    for (auto& p : m_workspace.canvasProfiles) {
        if (p.id.empty())
            p.id = "cp-" + nowIsoTag().toStdString();
    }

    // Re-attach template metadata by id (overrides any stale copy carried back).
    for (auto& p : m_workspace.canvasProfiles)
        for (const auto& [id, tpl] : savedTpl)
            if (p.id == id) { p.templateInfo = tpl; break; }

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
    for (const auto& p : m_workspace.canvasProfiles) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("A canvas profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    // Assign a stable id to the new profile and add it to the workspace.
    profile.id = "cp-" + nowIsoTag().toStdString();
    m_workspace.canvasProfiles.push_back(profile);

    // If this is the first profile, make it the active one.
    if (m_workspace.canvasProfiles.size() == 1)
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
    const auto it = std::find_if(
        m_workspace.canvasProfiles.begin(), m_workspace.canvasProfiles.end(),
        [&](const auto& p){ return p.name == m_activeCanvasProfileName.toStdString(); });

    if (it == m_workspace.canvasProfiles.end()) {
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

    QList<Platemaker::Models::OutputProfile> profiles(
        m_workspace.outputProfiles.begin(), m_workspace.outputProfiles.end());
    dlg.setProfiles(profiles, m_activeOutputProfileName);

    // The dialog edits copies, and we only write the results back to the workspace if the user clicks Accept.
    if (dlg.exec() != QDialog::Accepted) return;

    const auto result = dlg.profiles();
    m_workspace.outputProfiles.assign(result.begin(), result.end());
    m_activeOutputProfileName = dlg.activeProfileName();

    // Assign IDs to any profiles added without one (the dialog doesn't generate
    // them); an empty id breaks per-project output-profile selection.
    for (auto& p : m_workspace.outputProfiles)
        if (p.id.empty())
            p.id = "op-" + nowIsoTag().toStdString();

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
    for (const auto& p : m_workspace.outputProfiles) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("An output profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    // Assign a stable id to the new profile and add it to the workspace.
    profile.id = "op-" + nowIsoTag().toStdString();
    m_workspace.outputProfiles.push_back(profile);

    if (m_workspace.outputProfiles.size() == 1)
        m_activeOutputProfileName = QString::fromStdString(profile.name);

    setDirty(true);
}

void MainWindow::onEditActiveOutputProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Find the active profile by name. The dialog edits a copy, and we only write
    // the results back to the workspace if the user clicks Accept. The dialog
    // preserves templateInfo by id, so we can snapshot it before the dialog and
    // re-attach it afterward (the dialog never edits it).
    const auto it = std::find_if(
        m_workspace.outputProfiles.begin(), m_workspace.outputProfiles.end(),
        [&](const auto& p){ return p.name == m_activeOutputProfileName.toStdString(); });

    if (it == m_workspace.outputProfiles.end()) {
        QMessageBox::information(this, tr("No Active Profile"),
            tr("No active output profile. Use Output → Manage to create one."));
        return;
    }

    OutputProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    // OutputProfileDialog returns a profile without an id — preserve the stable
    // id so projects that reference this profile keep working.
    const std::string oldName = it->name;
    const std::string savedId = it->id;
    *it = dlg.profile();
    it->id = savedId.empty() ? ("op-" + it->name) : savedId;

    if (m_activeOutputProfileName == QString::fromStdString(oldName))
        m_activeOutputProfileName = QString::fromStdString(it->name);

    setDirty(true);
}
