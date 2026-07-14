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
// Workspace menu slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // Open a file dialog to select a workspace file (JSON) to load. If the user selects a file, load the workspace.
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Workspace"), defaultDialogDir(),
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (!path.isEmpty())
        loadWorkspace(path);
}

void MainWindow::onNewWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // Prompt the user to select a file path for the new workspace. 
    // If the user selects a path, create a new workspace and save it to that path.
    const QString path = QFileDialog::getSaveFileName(
        this, tr("New Workspace"), defaultDialogDir(),
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (path.isEmpty()) return;

    // Create a new workspace with a default output profile and save it to the selected path.
    closeWorkspace();

    // Create a default output profile for the new workspace. This ensures that the workspace has at least one output profile, which is required for rendering.
    Platemaker::Models::OutputProfile defaultOut;   // default output profile for new workspaces
    defaultOut.id          = "op-Webtoon Standard"; // stable id (empty id breaks selection)
    defaultOut.name        = "Webtoon Standard";    // user-visible name
    defaultOut.targetWidth = 800;                   // default target width for output images
    defaultOut.sliceHeight = 1280;                  // default slice height for output images

    // Initialize the new workspace with the default output profile and save it to disk.
    m_workspace = Platemaker::Models::Workspace{};
    m_workspace.outputProfiles.push_back(defaultOut);
    m_workspacePath = path;

    try {
        m_serializer.save(m_workspace, path.toStdString());
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot create workspace:\n%1").arg(e.what()));
        closeWorkspace();
        return;
    }

    // Update the UI to reflect the new workspace,
    // capture a snapshot for change tracking, 
    // and add the new workspace to the recent workspaces list.
    captureSnapshot();
    addToRecentWorkspaces(path);
    applyWorkspaceToUi();
}

void MainWindow::onSave()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) { onSaveAs(); return; }

    // Save the current workspace to disk. If the save operation fails, show an error message to the user.
    try {
        m_serializer.save(m_workspace, m_workspacePath.toStdString());
        captureSnapshot();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot save workspace:\n%1").arg(e.what()));
    }
}

void MainWindow::onSaveAs()
{
    // Prompt the user to select a file path to save the current workspace.
    // If the user selects a path, save the workspace to that path and add it to the recent workspaces list.
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Workspace As"),
        m_workspacePath.isEmpty() ? defaultDialogDir() : m_workspacePath,
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (path.isEmpty()) return;

    // Save the current workspace to the selected path.
    // If the save operation fails, show an error message to the user.
    m_workspacePath = path;
    onSave();
    if (!isWorkspaceModified())   // save succeeded
        addToRecentWorkspaces(path);
}

void MainWindow::onCloseWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    closeWorkspace();
}

void MainWindow::onRevealInExplorer()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) return;

    // Open the system file explorer at the directory containing the current workspace file.
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_workspacePath).absolutePath()));
}
// ---------------------------------------------------------------------------
// Recent workspaces (advisory — purely a convenience, never required)
// ---------------------------------------------------------------------------

QStringList MainWindow::recentWorkspaces() const
{
    // Absent/corrupt settings simply yield an empty list — never an error.
    return QSettings().value(QStringLiteral("recentWorkspaces")).toStringList();
}

void MainWindow::addToRecentWorkspaces(const QString &path)
{
    // Skip if the path is empty
    if (path.isEmpty()) return;

    const QString canonical = QFileInfo(path).absoluteFilePath();

    QStringList list = recentWorkspaces();
    // Case-insensitive de-dupe so the same file can't appear twice on Windows.
    list.removeIf([&](const QString &p){
        return QString::compare(p, canonical, Qt::CaseInsensitive) == 0;
    });
    list.prepend(canonical);
    while (list.size() > k_maxRecentWorkspaces)
        list.removeLast();

    QSettings().setValue(QStringLiteral("recentWorkspaces"), list);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    // Skip if the recent menu is not initialized
    if (!m_recentMenu) return;

    m_recentMenu->clear();

    // If there are no recent workspaces, show a disabled menu item indicating that.
    // Otherwise, populate the menu with the recent workspaces, allowing the user to open them or clear the list.
    const QStringList list = recentWorkspaces();
    if (list.isEmpty()) {
        QAction *none = m_recentMenu->addAction(tr("(No recent workspaces)"));
        none->setEnabled(false);
        return;
    }

    // Populate the recent workspaces menu with the list of recent workspaces,
    // allowing the user to open them or clear the list.
    int n = 1;
    for (const QString &path : list) {
        const QString name = QFileInfo(path).fileName();
        QAction *act = m_recentMenu->addAction(
            tr("&%1  %2").arg(QString::number(n++), name));
        act->setData(path);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, [this, path]{
            openRecentWorkspace(path);
        });
    }

    // Add a separator and a "Clear recent list" action to the recent workspaces menu.
    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear recent list")),
            &QAction::triggered, this, [this]{
        QSettings().remove(QStringLiteral("recentWorkspaces"));
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentWorkspace(const QString &path)
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // If the selected recent workspace file does not exist, show a warning and remove it from the recent list.
    // Otherwise, load the workspace from the selected path.
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Workspace Not Found"),
            tr("The workspace no longer exists:\n%1\n\n"
               "It has been removed from the recent list.").arg(path));
        QStringList list = recentWorkspaces();
        list.removeAll(path);
        QSettings().setValue(QStringLiteral("recentWorkspaces"), list);
        rebuildRecentMenu();
        return;
    }

    // Load the workspace from the selected recent workspace path.
    loadWorkspace(path);
}

QString MainWindow::defaultDialogDir() const
{
    // Return the default directory for file dialogs. 
    // If there are recent workspaces, return the directory of 
    // the most recent one; otherwise, return the user's home directory.
    const QStringList list = recentWorkspaces();
    return list.isEmpty() ? QString{}
                          : QFileInfo(list.first()).absolutePath();
}
