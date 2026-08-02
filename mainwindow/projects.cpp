#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "project.h"
#include "workspacesnapshotcommand.h"
#include "canvasprofiledialog.h"
#include "managecanvasprofilesdialog.h"
#include "manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"
#include "templatesdialog.h"
#include "renderworker.h"

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

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
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>

#include <algorithm>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Project panel slots
// ---------------------------------------------------------------------------

void MainWindow::onNewProject()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"),
            tr("Open or create a workspace first."));
        return;
    }

    // Prompt for a name, but don't allow empty or whitespace-only names.
    bool ok;
    const QString name = QInputDialog::getText(
        this, tr("New Project"), tr("Project name:"),
        QLineEdit::Normal, tr("Chapter 01"), &ok);

    // Cancelled or empty name → abort
    if (!ok || name.trimmed().isEmpty()) 
        return;

    // Create the project through the library, which mints the workspace-unique project uid — the GUI
    // no longer hand-rolls it. Then refresh the UI to reflect the new project.
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).addProject(name.trimmed().toStdString());
    setDirty(true);
    applyWorkspaceToUi();
}

void MainWindow::onProjectDoubleClicked(QListWidgetItem *item)
{
    // Open the dock for the double-clicked project, or bring it to front if already open.
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(m_workspace.projectItems.size()))
        return;
    openProjectDock(index);
}

void MainWindow::onProjectsContextMenu(const QPoint &pos)
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) return;

    QListWidgetItem *item = ui->listWidgetProjects->itemAt(pos);

    // Show a context menu with options to rename or delete the project, or create a new project.
    QMenu menu(this);
    if (item) {
        const int modelIndex = item->data(Qt::UserRole).toInt();
        connect(menu.addAction(tr("Rename")), &QAction::triggered, this,
                [this, modelIndex]{ renameProject(modelIndex); });
        connect(menu.addAction(tr("Delete")), &QAction::triggered, this,
                [this, modelIndex]{ removeProject(modelIndex); });
        menu.addSeparator();
    }
    connect(menu.addAction(tr("New")), &QAction::triggered,
            this, &MainWindow::onNewProject);

    menu.exec(ui->listWidgetProjects->viewport()->mapToGlobal(pos));
}

void MainWindow::renameProject(int modelIndex)
{
    // Skip if no project is selected or the index is out of bounds
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    // Prompt for a new name, but don't allow empty or whitespace-only names.
    auto &proj = m_workspace.projectItems[static_cast<std::size_t>(modelIndex)];
    bool ok;
    const QString name = QInputDialog::getText(
        this, tr("Rename Project"), tr("Project name:"),
        QLineEdit::Normal, QString::fromStdString(proj.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    // Renaming a project is a workspace-scope edit (the name lives on the workspace timeline, not the
    // project's content timeline) — record it there so Ctrl+Z from the Workspace tab reverts it.
    commitWorkspaceEdit(tr("Rename project"), [&]{
        proj.name = name.trimmed().toStdString();

        // Reflect the new name on the open dock/tab, if any.
        if (QDockWidget *dock = dockForProject(modelIndex))
            dock->setWindowTitle(name.trimmed());

        setDirty(true);
        applyWorkspaceToUi();
    });
}

void MainWindow::removeProject(int modelIndex)
{
    // Block removal if a render is in progress, or if the index is out of bounds.
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }

    // Skip if no project is selected or the index is out of bounds
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    const QString name = QString::fromStdString(
        m_workspace.projectItems[static_cast<std::size_t>(modelIndex)].name);

    if (QMessageBox::question(this, tr("Delete Project"),
            tr("Remove project \"%1\" from the workspace?\n\n"
               "Files on disk are not deleted.").arg(name))
        != QMessageBox::Yes)
        return;

    // Close this project's dock if it is open.
    if (QDockWidget *dock = dockForProject(modelIndex)) {
        m_openProjectDocks.removeOne(dock);
        dock->deleteLater();
    }

    // Erase from the model.
    m_workspace.projectItems.erase(
        m_workspace.projectItems.begin() + modelIndex);

    // Reindex any still-open docks that referenced a higher model index — the
    // vector shifted down by one.
    for (QDockWidget *dock : std::as_const(m_openProjectDocks)) {
        const int idx = dock->property("projectIndex").toInt();
        if (idx > modelIndex) {
            dock->setProperty("projectIndex", idx - 1);
            if (auto *pw = qobject_cast<Project *>(dock->widget()))
                pw->setProjectIndex(idx - 1);
        }
    }

    setDirty(true);
    applyWorkspaceToUi();
}

QDockWidget *MainWindow::dockForProject(int modelIndex) const
{
    // Return the QDockWidget for the project at the given model index, or nullptr if not found.
    for (QDockWidget *dock : m_openProjectDocks)
        if (dock->property("projectIndex").toInt() == modelIndex)
            return dock;
    return nullptr;
}
// ---------------------------------------------------------------------------
// Project dock management
// ---------------------------------------------------------------------------

void MainWindow::openProjectDock(int projectIndex)
{
    // Bring existing dock to front if already open
    for (QDockWidget *dock : std::as_const(m_openProjectDocks)) {
        if (dock->property("projectIndex").toInt() == projectIndex) {
            dock->show();
            dock->raise();
            return;
        }
    }

    // Get the project name for the dock title.
    const QString name = QString::fromStdString(
        m_workspace.projectItems[projectIndex].name);

    // Create a new dock for the project, with a Project widget inside.
    QDockWidget *newDock = new QDockWidget(name, this);
    newDock->setProperty("projectIndex", projectIndex);
    newDock->setFeatures(QDockWidget::DockWidgetMovable  |
                         QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetFloatable);
    const QString cacheDir = QFileInfo(m_workspacePath).absolutePath()
                             + "/.platemaker-cache";
    auto* projectWidget = new Project(projectIndex, m_workspace, cacheDir, newDock);
    connect(projectWidget, &Project::projectModified, this, [this]{
        setDirty(true);
    });
    connect(projectWidget, &Project::renderToggleRequested,
            this, &MainWindow::onRenderToggle);
    // A workspace-level edit made from this dock (canvas-profile content, output format) belongs on
    // the workspace undo timeline — push it there from the before/after snapshots the project sends.
    connect(projectWidget, &Project::workspaceEditCommitted, this,
            [this](const QString& text, const QString& before, const QString& after) {
                m_workspaceUndoStack->push(new WorkspaceSnapshotCommand(this, before, after, text));
            });
    // Keep this dock's palette-derived views (output combo, assigned-canvas list) in sync with
    // workspace-level profile edits made elsewhere. The connection is auto-removed when the widget
    // is destroyed (dock closed), so no manual bookkeeping is needed.
    connect(this, &MainWindow::workspaceProfilesChanged,
            projectWidget, &Project::refreshProfileViews);
    newDock->setWidget(projectWidget);

    // Register this project's undo stack with the group (its destructor auto-removes it when the dock
    // closes). Ctrl+Z / Ctrl+Y target it while this dock's tab is in front (visibilityChanged below).
    m_undoGroup->addStack(projectWidget->undoStack());

    // Track which project is "current" for F5 / Process menu (the raised dock), and make this
    // project's undo stack active while its tab is visible.
    m_activeProjectIndex = projectIndex;
    connect(newDock, &QDockWidget::visibilityChanged, this,
            [this, newDock, projectWidget](bool visible) {
        if (visible) {
            m_activeProjectIndex = newDock->property("projectIndex").toInt();
            m_undoGroup->setActiveStack(projectWidget->undoStack());
        }
    });

    // Always tabify with the workspace panel — keeps the layout in two columns.
    // On first open Qt promotes the area to a tab group; subsequent opens just add tabs.
    tabifyDockWidget(ui->dockWidgetWorkspace, newDock);

    // When Qt re-docks the window (e.g. double-click on floating title bar or drag back),
    // force it back into the tab group instead of landing in a random dock area.
    connect(newDock, &QDockWidget::topLevelChanged, this, [this, newDock](bool floating) {
        if (!floating) {
            tabifyDockWidget(ui->dockWidgetWorkspace, newDock);
            newDock->raise();
        }
    });

    m_openProjectDocks.append(newDock);
    newDock->show();
    newDock->raise();

    // Wire close/float tab bar buttons for the project tab bar
    const auto tabBars = findChildren<QTabBar *>(QString{}, Qt::FindDirectChildrenOnly);
    for (QTabBar *bar : tabBars) {
        bar->setTabsClosable(true);
        bar->disconnect(this);
        connect(bar, &QTabBar::tabCloseRequested,   this, &MainWindow::closeProjectByIndex);
        connect(bar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::toggleProjectFloatState);
    }
}

void MainWindow::closeProjectByIndex(int index)
{
    // Skip if no project is selected or the index is out of bounds
    if (index < 0 || index >= m_openProjectDocks.size()) return;
    QDockWidget *dock = m_openProjectDocks.takeAt(index);
    dock->deleteLater();
}

void MainWindow::toggleProjectFloatState(int index)
{
    // Skip if no project is selected or the index is out of bounds
    if (index < 0 || index >= m_openProjectDocks.size()) return;
    QDockWidget *dock = m_openProjectDocks.at(index);
    // setFloating(false) triggers topLevelChanged → tabifyDockWidget, so no manual re-tabify needed.
    dock->setFloating(!dock->isFloating());
}
