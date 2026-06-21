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
// Project panel slots
// ---------------------------------------------------------------------------

void MainWindow::onNewProject()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"),
            tr("Open or create a workspace first."));
        return;
    }

    bool ok;
    const QString name = QInputDialog::getText(
        this, tr("New Project"), tr("Project name:"),
        QLineEdit::Normal, tr("Chapter 01"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    Platemaker::Models::ProjectItem proj;
    proj.name = name.trimmed().toStdString();
    proj.uuid = "proj-" + QDateTime::currentDateTimeUtc()
                    .toString(Qt::ISODate).toStdString();

    m_workspace.projectItems.push_back(std::move(proj));
    setDirty(true);
    applyWorkspaceToUi();
}

void MainWindow::onProjectDoubleClicked(QListWidgetItem *item)
{
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(m_workspace.projectItems.size()))
        return;
    openProjectDock(index);
}

void MainWindow::onProjectsContextMenu(const QPoint &pos)
{
    if (m_workspacePath.isEmpty()) return;

    QListWidgetItem *item = ui->listWidgetProjects->itemAt(pos);

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
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    auto &proj = m_workspace.projectItems[static_cast<std::size_t>(modelIndex)];
    bool ok;
    const QString name = QInputDialog::getText(
        this, tr("Rename Project"), tr("Project name:"),
        QLineEdit::Normal, QString::fromStdString(proj.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    proj.name = name.trimmed().toStdString();

    // Reflect the new name on the open dock/tab, if any.
    if (QDockWidget *dock = dockForProject(modelIndex))
        dock->setWindowTitle(name.trimmed());

    setDirty(true);
    applyWorkspaceToUi();
}

void MainWindow::removeProject(int modelIndex)
{
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
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

    const QString name = QString::fromStdString(
        m_workspace.projectItems[projectIndex].name);

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
    newDock->setWidget(projectWidget);

    // Track which project is "current" for F5 / Process menu (the raised dock).
    m_activeProjectIndex = projectIndex;
    connect(newDock, &QDockWidget::visibilityChanged, this, [this, newDock](bool visible) {
        if (visible)
            m_activeProjectIndex = newDock->property("projectIndex").toInt();
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
    if (index < 0 || index >= m_openProjectDocks.size()) return;
    QDockWidget *dock = m_openProjectDocks.takeAt(index);
    dock->deleteLater();
}

void MainWindow::toggleProjectFloatState(int index)
{
    if (index < 0 || index >= m_openProjectDocks.size()) return;
    QDockWidget *dock = m_openProjectDocks.at(index);
    // setFloating(false) triggers topLevelChanged → tabifyDockWidget, so no manual re-tabify needed.
    dock->setFloating(!dock->isFloating());
}
