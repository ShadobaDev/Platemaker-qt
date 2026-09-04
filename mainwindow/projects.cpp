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
#include "docktitlebar.h"

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
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QSize>
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

    // Text & bubbles: the project needs to know where the workspace file lives (its `overlays/` folder
    // is written beside it) and which authoring records are its own. The records travel back here on
    // every edit — including an undo, which restores them alongside the library's project state — so the
    // sidecar written at save time is always current.
    const QString projectUid =
        QString::fromStdString(m_workspace.projectItems[projectIndex].uid);
    projectWidget->setWorkspacePath(m_workspacePath);
    projectWidget->setArtifacts(m_overlayArtifacts.artifacts(projectUid));
    connect(projectWidget, &Project::artifactsChanged, this,
            [this, projectUid, newDock](const ArtifactMap& artifacts) {
        m_overlayArtifacts.setArtifacts(projectUid, artifacts);
        // A record edit changes what a bubble says without moving a page, so the strip has to be told
        // explicitly — the feed signature it guards itself with would not see it.
        if (QDockWidget* strip = dockForStripViewer(newDock->property("projectIndex").toInt()))
            refreshStripViewer(strip);
    });
    connect(projectWidget, &Project::projectModified, this, [this, newDock]{
        setDirty(true);
        // The strip is built from the inputs, so it follows an input / profile edit immediately — no
        // render needed. setPreviewSource() ignores a feed that has not actually changed, so the edits
        // that leave the strip alone (a grade tweak, render bookkeeping) cost nothing here.
        // The index comes from the dock property, which removeProject() re-stamps when the model shifts.
        if (QDockWidget *strip = dockForStripViewer(newDock->property("projectIndex").toInt()))
            refreshStripViewer(strip);
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
    // Same custom title bar as the workspace/strip/action docks (min = dock ⇄ detach, max, close).
    installDockTitleBar(newDock);

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
    // The tab bar is shared by the workspace, project and strip docks, so resolve the dock by title
    // rather than a raw index, then close it the way its kind expects (see closeDock).
    closeDock(dockForTabBarTab(qobject_cast<QTabBar *>(sender()), index));
}

void MainWindow::toggleProjectFloatState(int index)
{
    // Float / re-dock whichever dock's tab was double-clicked (resolved from the shared tab bar). This is
    // also how a tabified strip is pulled back out into its own floating window.
    if (QDockWidget *dock = dockForTabBarTab(qobject_cast<QTabBar *>(sender()), index))
        dock->setFloating(!dock->isFloating());
}

void MainWindow::installDockTitleBar(QDockWidget *dock)
{
    // The shared visual/maximise part lives in DockTitleBar; wire the dock-specific minimise and close
    // behaviour here. Qt hides the bar while the dock is tabified (the tab stands in), so its buttons
    // show only while the dock floats or is docked alone.
    auto *bar = new DockTitleBar(dock, dock);

    // Minimise → toggle dock ⇄ detach. Docking tabs it beside the Workspace, except the Workspace dock
    // (the tab anchor) and the Action column (Right-only, never combined) which simply re-dock.
    connect(bar, &DockTitleBar::minimiseClicked, this, [this, dock] {
        if (dock->isFloating()) {
            dock->setFloating(false);
            if (dock != ui->dockWidgetWorkspace && dock != ui->dockWidgetAction) {
                tabifyDockWidget(ui->dockWidgetWorkspace, dock);
                wireDockTabBars();
            }
            dock->show();
            dock->raise();
        } else {
            dock->setFloating(true);
        }
    });

    connect(bar, &DockTitleBar::closeClicked, this, [this, dock] { closeDock(dock); });

    dock->setTitleBarWidget(bar);
}

void MainWindow::closeDock(QDockWidget *dock)
{
    if (!dock) return;
    // The Workspace, Action and strip docks hide (reopened via their menu action / View strip); a
    // project dock is destroyed (reopened from the project list).
    if (dock == ui->dockWidgetWorkspace || dock == ui->dockWidgetAction || m_openStripDocks.contains(dock)) {
        dock->close();
        return;
    }
    m_openProjectDocks.removeOne(dock);
    dock->deleteLater();
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
        viewer->setPreviewSource({}, {}, {}, {}, workspaceCacheDir());   // project gone → empty state
        return;
    }

    // The strip is built from the project's INPUT pages, not from the rendered output: the viewer has to
    // work before the first render, and a grade previewed on the committed output would be applied on
    // top of the one the render already baked in. Everything the library needs to put a page through its
    // page domain goes across: the inputs in strip order, the resolved output profile (target width +
    // slice height), and the canvas profiles that decide margins. The workspace cache dir feeds the
    // viewer's proxy thumbnails — already warm, the Input tab's tiles use the same cache for the same
    // files.
    const auto &project = m_workspace.projectItems[static_cast<std::size_t>(idx)];
    viewer->setPreviewSource(project.inputsInOrder(),
                             resolveOutputProfileFor(project),
                             m_workspace.canvasProfiles(),
                             project.canvasProfileIds(),
                             workspaceCacheDir());

    // Feed the Grade panel + live preview. The strip's pixels are ungraded by construction, so this
    // previews cleanly whether or not a render has happened, and a render does not change the view.
    viewer->setColourCorrection(project.colourCorrection);

    // Text & bubbles: the library's placements plus the GUI's authoring records for them. The viewer
    // resolves each overlay's page anchor against the layout it just built — the same arithmetic the
    // render does — so a bubble previews exactly where it will be baked.
    viewer->setOverlaySource(project.getStripOverlays(),
                             m_overlayArtifacts.artifacts(QString::fromStdString(project.uid)));
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
    // A settled grade edit in the CC panel → persist it onto the project (undoable, via the Project dock).
    connect(viewer, &StripViewer::colourCorrectionEdited, this,
            [this, projectIndex](const Platemaker::Models::ColourCorrection &cc) {
        if (auto *pw = projectWidget(projectIndex))
            pw->applyColourCorrection(cc);
    });

    // Text & bubbles. Creation goes through the project because the *library* mints the overlay's uid
    // and hashes its bitmap; every other edit arrives as the complete new state and is stored as one
    // undo step. Both are guarded the same way the grade is: with the project dock closed there is no
    // undo stack to push onto, so the edit is declined rather than applied untracked.
    connect(viewer, &StripViewer::artifactCreated, this,
            [this, projectIndex](const TextArtifact &artifact, int x, int y, const QString &anchorUid) {
        if (auto *pw = projectWidget(projectIndex))
            pw->createOverlay(artifact, x, y, anchorUid);
    });
    connect(viewer, &StripViewer::overlaysEdited, this,
            [this, projectIndex](const std::vector<Platemaker::Models::StripOverlay> &overlays,
                                 const ArtifactMap &artifacts, const QString &undoText) {
        if (auto *pw = projectWidget(projectIndex))
            pw->applyOverlays(overlays, artifacts, undoText);
    });
    dock->setWidget(viewer);

    // Shared custom title bar (minimise = dock ⇄ detach, maximise = fill screen, close = hide).
    installDockTitleBar(dock);

    // Register it in a dock area first (its home when docked), then float it.
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(true);

    m_openStripDocks.append(dock);
    refreshStripViewer(dock);   // lay the strip out from the inputs (also settles the strip width)

    // Size: the output/strip width plus a 100px margin on each side, and 80% of the screen height. Fall
    // back to a typical webtoon width when the project has no pages yet (strip width unknown).
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
