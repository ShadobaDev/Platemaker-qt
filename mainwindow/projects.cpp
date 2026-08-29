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
#include "stripviewer.h"

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
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
        // "New from this…" rather than "Duplicate": the copy is a new, unrendered project seeded from
        // this one's inputs + profile, not a byte-for-byte duplicate (no outputs, no output folder).
        connect(menu.addAction(tr("New from this…")), &QAction::triggered, this,
                [this, modelIndex]{ duplicateProject(modelIndex); });
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
        if (QDockWidget *strip = dockForStripViewer(modelIndex))
            strip->setWindowTitle(tr("Strip — %1").arg(name.trimmed()));

        setDirty(true);
        applyWorkspaceToUi();
    });
}

void MainWindow::duplicateProject(int modelIndex)
{
    // Skip if no project is selected or the index is out of bounds
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    // Pre-fill the name prompt with "<source> (copy)" so the user can immediately rename it (e.g. to
    // the publisher this sibling targets) — the driving use-case is one project per publisher.
    const auto& source = m_workspace.projectItems[static_cast<std::size_t>(modelIndex)];
    bool ok;
    const QString name = QInputDialog::getText(
        this, tr("New Project From This"), tr("Project name:"),
        QLineEdit::Normal,
        tr("%1 (copy)").arg(QString::fromStdString(source.name)), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    // The lib seeds a new project from the source: its input files and profile links only — no output
    // directory, no output slices, no render state (the copy's inputs start Pending). It mints the
    // fresh workspace-unique project uid, so nothing collides. Adding a project is not undoable here
    // (matching New / Delete), so just mark dirty and refresh.
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
        .duplicateProject(source, name.trimmed().toStdString());
    setDirty(true);
    applyWorkspaceToUi();
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
    // ...and its strip viewer dock, if open.
    if (QDockWidget *strip = dockForStripViewer(modelIndex)) {
        m_openStripDocks.removeOne(strip);
        strip->deleteLater();
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
    // Strip docks only carry the index as a property (their widget reads nothing project-scoped itself).
    for (QDockWidget *strip : std::as_const(m_openStripDocks)) {
        const int idx = strip->property("projectIndex").toInt();
        if (idx > modelIndex)
            strip->setProperty("projectIndex", idx - 1);
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
    // Free to arrange anywhere but the Action column (Right), matching the workspace dock, so a project
    // can never be tab-combined with Action but can split/tab with Workspace and other projects.
    newDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    const QString cacheDir = workspaceCacheDir();
    auto* projectWidget = new Project(projectIndex, m_workspace, cacheDir, newDock);
    connect(projectWidget, &Project::projectModified, this, [this]{
        setDirty(true);
    });
    connect(projectWidget, &Project::renderToggleRequested,
            this, &MainWindow::onRenderToggle);
    connect(projectWidget, &Project::viewStripRequested,
            this, &MainWindow::openStripViewerDock);
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

    // Default placement on open: tab onto the workspace panel. On first open Qt promotes the area to a
    // tab group; subsequent opens add tabs. From here the dock is free — the user can split it out
    // horizontally or vertically, or float it, and a re-dock lands where it is dropped (no forced
    // re-tabify guard: the allowed-areas restriction already keeps it out of the Action column).
    tabifyDockWidget(ui->dockWidgetWorkspace, newDock);

    m_openProjectDocks.append(newDock);
    newDock->show();
    newDock->raise();

    wireDockTabBars();
}

void MainWindow::wireDockTabBars()
{
    // The workspace, project and strip docks share dock tab bars (QMainWindow creates one per tab
    // group). Make every tab closable and (re)wire close / double-click to the resolvers below, which
    // map a tab back to its dock by title. disconnect(this) first so repeated calls don't stack.
    const auto tabBars = findChildren<QTabBar *>(QString{}, Qt::FindDirectChildrenOnly);
    for (QTabBar *bar : tabBars) {
        bar->setTabsClosable(true);
        bar->disconnect(this);
        connect(bar, &QTabBar::tabCloseRequested,   this, &MainWindow::closeProjectByIndex);
        connect(bar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::toggleProjectFloatState);
    }
}

QDockWidget *MainWindow::dockForTabBarTab(const QTabBar *bar, int index) const
{
    if (!bar || index < 0 || index >= bar->count())
        return nullptr;
    const QString title = bar->tabText(index);
    if (ui->dockWidgetWorkspace->windowTitle() == title)
        return ui->dockWidgetWorkspace;
    for (QDockWidget *dock : m_openProjectDocks)
        if (dock->windowTitle() == title) return dock;
    for (QDockWidget *dock : m_openStripDocks)
        if (dock->windowTitle() == title) return dock;
    return nullptr;
}

void MainWindow::closeProjectByIndex(int index)
{
    // Resolve which dock's tab was closed — the tab bar is shared by the workspace, project and strip
    // docks, so a raw index into m_openProjectDocks would be wrong. Workspace / strip docks hide (the
    // strip reopens via View strip, the workspace via its menu action); a project dock is destroyed.
    QDockWidget *dock = dockForTabBarTab(qobject_cast<QTabBar *>(sender()), index);
    if (!dock) return;
    if (dock == ui->dockWidgetWorkspace || m_openStripDocks.contains(dock)) {
        dock->close();
        return;
    }
    m_openProjectDocks.removeOne(dock);
    dock->deleteLater();
}

void MainWindow::toggleProjectFloatState(int index)
{
    // Float / re-dock whichever dock's tab was double-clicked (resolved from the shared tab bar). This is
    // also how a tabified strip is pulled back out into its own floating window.
    if (QDockWidget *dock = dockForTabBarTab(qobject_cast<QTabBar *>(sender()), index))
        dock->setFloating(!dock->isFloating());
}

// ---------------------------------------------------------------------------
// Strip viewer dock (per-project, floating, custom title bar)
// ---------------------------------------------------------------------------

QDockWidget *MainWindow::dockForStripViewer(int modelIndex) const
{
    for (QDockWidget *dock : m_openStripDocks)
        if (dock->property("projectIndex").toInt() == modelIndex)
            return dock;
    return nullptr;
}

void MainWindow::refreshStripViewer(QDockWidget *dock)
{
    if (!dock) return;
    auto *viewer = qobject_cast<StripViewer *>(dock->widget());
    if (!viewer) return;

    const int idx = dock->property("projectIndex").toInt();
    if (idx < 0 || idx >= static_cast<int>(m_workspace.projectItems.size())) {
        viewer->setSlices({}, workspaceCacheDir());   // project gone → empty state
        return;
    }

    // The strip is the committed output slices reassembled. getOutputImages() is already in strip
    // (slice) order — the render writes them in order — so no sorting is needed. Path building mirrors
    // Project::addOutputImageTile (output directory + fileName). The workspace cache dir feeds the
    // viewer's proxy thumbnails (the render already warmed it for the output tiles).
    const auto &project = m_workspace.projectItems[static_cast<std::size_t>(idx)];
    const QString dir = QString::fromStdString(project.getOutputDirectory());
    QStringList paths;
    for (const auto &of : project.getOutputImages())
        paths << QDir(dir).filePath(QString::fromStdString(of.fileName));
    viewer->setSlices(paths, workspaceCacheDir());
}

void MainWindow::openStripViewerDock(int projectIndex)
{
    if (projectIndex < 0 || projectIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    // Already open → raise and refresh (the outputs may have changed since it was last shown).
    if (QDockWidget *existing = dockForStripViewer(projectIndex)) {
        existing->show();
        existing->raise();
        refreshStripViewer(existing);
        return;
    }

    const QString name = QString::fromStdString(
        m_workspace.projectItems[static_cast<std::size_t>(projectIndex)].name);

    QDockWidget *dock = new QDockWidget(this);
    dock->setWindowTitle(tr("Strip — %1").arg(name));
    dock->setProperty("projectIndex", projectIndex);
    dock->setFeatures(QDockWidget::DockWidgetMovable  |
                      QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetFloatable);
    // Free to dock anywhere but the Action column (Right), like the project/workspace docks — the strip
    // is never tab-combined with Action.
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);

    auto *viewer = new StripViewer(dock);
    // "Render & view": outputs are cheap/regenerable, so this just runs the normal render for the
    // project; onRenderFinished refreshes this viewer when it completes. If the project is already up to
    // date, startRender is a no-op and the already-loaded committed slices stay shown.
    connect(viewer, &StripViewer::renderAndViewRequested, this, [this, projectIndex] {
        (void)startRender(projectIndex);
    });
    dock->setWidget(viewer);

    // Custom title bar: title + minimise / maximise / close. A floating QDockWidget otherwise shows only
    // Qt's own close button, and native min/max on a dock misbehave; a custom bar gives the exact
    // buttons while keeping the dock draggable and dockable (its empty area still drives the dock move).
    // Per the user's model: minimise packs it back into the dock, maximise fills the screen (⇄ restore).
    auto *titleBar = new QWidget(dock);
    auto *tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(6, 2, 2, 2);
    tbLayout->setSpacing(2);
    auto *titleLabel = new QLabel(dock->windowTitle(), titleBar);
    connect(dock, &QDockWidget::windowTitleChanged, titleLabel, &QLabel::setText);
    tbLayout->addWidget(titleLabel, 1);

    const auto addTitleButton = [&](QStyle::StandardPixmap icon, const QString &tip) {
        auto *b = new QToolButton(titleBar);
        b->setIcon(style()->standardIcon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        tbLayout->addWidget(b);
        return b;
    };

    // Minimise → dock it back, tabbed alongside the Workspace / project docks (not as its own area).
    // Clear any maximised state so a later maximise fills the screen rather than restoring an old size.
    connect(addTitleButton(QStyle::SP_TitleBarMinButton, tr("Dock (tab beside Workspace)")),
            &QToolButton::clicked, this, [this, dock] {
        dock->setProperty("stripMaximized", false);
        dock->setFloating(false);
        tabifyDockWidget(ui->dockWidgetWorkspace, dock);
        wireDockTabBars();   // the new tab must be closable + wired
        dock->show();
        dock->raise();
    });

    // Maximise ⇄ restore → fill the screen (float first if docked), toggling back to the previous size.
    connect(addTitleButton(QStyle::SP_TitleBarMaxButton, tr("Maximise to the full screen")),
            &QToolButton::clicked, this, [this, dock] {
        if (!dock->isFloating())
            dock->setFloating(true);
        if (dock->property("stripMaximized").toBool()) {
            dock->setGeometry(dock->property("stripRestoreGeom").toRect());
            dock->setProperty("stripMaximized", false);
        } else {
            dock->setProperty("stripRestoreGeom", dock->geometry());
            const QScreen *scr = dock->screen() ? dock->screen() : screen();
            if (scr)
                dock->setGeometry(scr->availableGeometry());
            dock->setProperty("stripMaximized", true);
        }
    });

    // Close → hide (reopened via View strip).
    connect(addTitleButton(QStyle::SP_TitleBarCloseButton, tr("Close")),
            &QToolButton::clicked, dock, &QDockWidget::close);

    dock->setTitleBarWidget(titleBar);

    // Register it in a dock area first (its home when docked), then float it.
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(true);

    m_openStripDocks.append(dock);
    refreshStripViewer(dock);   // load the current committed slices (also computes the strip width)

    // Size: the output/strip width plus a 100px margin on each side, and 80% of the screen height. Fall
    // back to a typical webtoon width when the project has no output yet (strip width unknown).
    const int stripW = viewer->stripSize().width();
    const int dockW  = (stripW > 0 ? stripW : 800) + 200;
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail  = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
    const int dockH    = static_cast<int>(avail.height() * 0.8);
    dock->resize(dockW, dockH);
    dock->move(avail.center() - QPoint(dockW / 2, dockH / 2));   // centre on the screen

    dock->show();
    dock->raise();
}
