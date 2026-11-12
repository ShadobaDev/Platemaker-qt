#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include "project.hpp"
#include "canvasprofiledialog.hpp"
#include "managecanvasprofilesdialog.hpp"
#include "manageoutputprofilesdialog.hpp"
#include "outputprofiledialog.hpp"
#include "templatesdialog.hpp"
#include "renderworker.hpp"
#include "editor.hpp"
#include "docktitlebar.hpp"
#include "dockattention.hpp"
#include "advisories.hpp"

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
#include <QSize>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
#include <QUndoGroup>
#include <QUndoStack>

#include "history.hpp"
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
    // With no workspace open there are no projects to act on, and a project needs a workspace first.
    if (m_workspacePath.isEmpty()) {
        QMenu menu(this);
        menu.addAction(ui->actionNew_workspace);
        menu.exec(ui->listWidgetProjects->viewport()->mapToGlobal(pos));
        return;
    }

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
        if (QDockWidget *strip = dockForStripEditor(modelIndex))
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

    // The project is going, so its history has nothing left to restore. It is dropped here rather than
    // left in the group, where its entries would keep Undo enabled for a project that no longer exists.
    const QString goingUid = QString::fromStdString(
        m_workspace.projectItems[static_cast<std::size_t>(modelIndex)].uid);
    dropHistoryFor(goingUid);
    // ...and nothing is true of it any more, so nothing should still be said about it.
    m_advisories->clearProject(goingUid);

    // Close this project's dock if it is open. Destroyed rather than hidden, unlike closeDock(): there
    // is no project left for it to show.
    if (QDockWidget *dock = dockForProject(modelIndex)) {
        m_openProjectDocks.removeOne(dock);
        dock->deleteLater();
    }
    // ...and its strip editor dock, if open.
    if (QDockWidget *strip = dockForStripEditor(modelIndex)) {
        m_openStripDocks.removeOne(strip);
        strip->deleteLater();
    }

    // Erase from the model.
    m_workspace.projectItems.erase(
        m_workspace.projectItems.begin() + modelIndex);
    // The raised project shifts down with the rest, or, if it was this one, is re-picked below.
    const bool wasActive = m_activeProjectIndex == modelIndex;
    if (m_activeProjectIndex > modelIndex)
        --m_activeProjectIndex;

    // The docks are keyed by project uid, which the erase leaves alone. Only the Project widgets hold a
    // model index of their own, and every one above the gap has moved down by one.
    for (QDockWidget *dock : std::as_const(m_openProjectDocks))
        if (auto *pw = qobject_cast<Project *>(dock->widget()))
            pw->setProjectIndex(projectIndexOf(dock));
    if (wasActive)
        retargetActiveProject(-1);

    setDirty(true);
    applyWorkspaceToUi();
}

QDockWidget *MainWindow::dockForProject(int modelIndex) const
{
    const QString uid = projectUidAt(modelIndex);
    if (uid.isEmpty())
        return nullptr;
    for (QDockWidget *dock : m_openProjectDocks)
        if (dock->property("projectUid").toString() == uid)
            return dock;
    return nullptr;
}

QString MainWindow::projectUidAt(int modelIndex) const
{
    if (modelIndex < 0 || modelIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return {};
    return QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(modelIndex)].uid);
}

int MainWindow::projectIndexOf(const QDockWidget *dock) const
{
    return projectIndexForUid(dock->property("projectUid").toString());
}

// ---------------------------------------------------------------------------
// Project dock management
// ---------------------------------------------------------------------------

History* MainWindow::historyFor(int projectIndex)
{
    const QString uid = QString::fromStdString(
        m_workspace.projectItems[static_cast<std::size_t>(projectIndex)].uid);

    auto it = m_projectHistories.constFind(uid);
    if (it != m_projectHistories.constEnd())
        return it.value();

    // History::k_depth deep: enough to undo a train of thought, and a step holds one project's state at
    // most, so a chapter's worth of them does not weigh.
    auto* history = new History(this);
    m_undoGroup->addStack(history->stack());
    m_projectHistories.insert(uid, history);
    return history;
}

void MainWindow::dropHistoryFor(const QString& projectUid)
{
    const auto it = m_projectHistories.constFind(projectUid);
    if (it == m_projectHistories.constEnd())
        return;
    m_undoGroup->removeStack(it.value()->stack());
    delete it.value();
    m_projectHistories.erase(it);
}

void MainWindow::openProjectDock(int projectIndex)
{
    // Bring existing dock to front if already open
    if (QDockWidget *dock = dockForProject(projectIndex)) {
        dock->show();
        dock->raise();
        return;
    }

    // Get the project name for the dock title.
    const QString name = QString::fromStdString(
        m_workspace.projectItems[projectIndex].name);

    // Create a new dock for the project, with a Project widget inside.
    // Keyed by the project's uid, which survives every reorder and removal of the others; the object
    // name gives the dock the same stable identity in Qt's own bookkeeping.
    const QString projectUid = projectUidAt(projectIndex);
    QDockWidget *newDock = new QDockWidget(name, this);
    newDock->setProperty("projectUid", projectUid);
    newDock->setObjectName(QStringLiteral("project:") + projectUid);
    newDock->setFeatures(QDockWidget::DockWidgetMovable  |
                         QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetFloatable);
    // Free to arrange anywhere but the Action column (Right), matching the workspace dock, so a project
    // can never be tab-combined with Action but can split/tab with Workspace and other projects.
    newDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    const QString cacheDir = workspaceCacheDir();
    auto* projectWidget = new Project(projectIndex, m_workspace, cacheDir,
                                      historyFor(projectIndex), m_workspaceHistory, newDock);

    // Text & bubbles: the project needs to know where the workspace file lives (its `overlays/` folder
    // is written beside it) and which authoring records are its own. The records travel back here on
    // every edit — including an undo, which restores them alongside the library's project state — so the
    // per-project cache stays in step with what the editor has committed.
    projectWidget->setWorkspacePath(m_workspacePath);
    projectWidget->setWriteGuard([this] { return canWriteWorkspace(); });
    projectWidget->setRecords(m_overlayRecords.records(projectUid));
    connect(projectWidget, &Project::recordsChanged, this,
            [this, projectUid](const ObjectRecord::Map& records) {
        // Only the cache: the strip is told by projectModified(), which follows every record change and
        // refreshes it from this cache. Refreshing it here too fed it the same records twice per edit.
        m_overlayRecords.setRecords(projectUid, records);
    });
    connect(projectWidget, &Project::projectModified, this, [this, newDock]{
        setDirty(true);
        // Conditions are derived, so they are re-derived here rather than at each of the places that
        // could make one true. An edit that changes neither says nothing: raise() is silent when the
        // advisory it is handed is the one already standing.
        refreshAdvisoriesFor(projectIndexOf(newDock));
        // The strip is built from the inputs, so it follows an input / profile edit immediately — no
        // render needed. setPreviewSource() ignores a feed that has not actually changed, so the edits
        // that leave the strip alone (a grade tweak, render bookkeeping) cost nothing here.
        // The index is looked up by the dock's project uid, so it follows the model when it shifts.
        if (QDockWidget *strip = dockForStripEditor(projectIndexOf(newDock)))
            refreshStripEditor(strip);
    });
    // An undone or redone step can land in the window the artist is not looking at — one history
    // covers both docks. Take them to it, and to the objects it touched, so Ctrl+Z is never mistaken
    // for having done nothing. The strip editor may simply not be open, and then there is nowhere to go.
    connect(projectWidget, &Project::historyStepApplied, this,
            [this, newDock, projectWidget](EditScope scope, const QStringList& uids) {
        const int idx = projectIndexOf(newDock);
        if (scope != EditScope::StripEditor) {
            showDockAttention(newDock);
            projectWidget->showStep(scope);
            return;
        }
        QDockWidget* strip = dockForStripEditor(idx);
        if (!strip)
            return;
        // Armed before the state goes out: the objects do not exist here until the feed that follows
        // this signal builds them.
        if (auto* editor = qobject_cast<StripEdit::Editor*>(strip->widget()))
            editor->selectAfterFeed(uids);
        showDockAttention(strip);
    });
    connect(projectWidget, &Project::renderToggleRequested,
            this, &MainWindow::onRenderToggle);
    connect(projectWidget, &Project::viewStripRequested, this, [this](int idx, const QString& toolId) {
        openStripEditorDock(idx);   // lays the strip out before it returns, so there is a strip to select
        if (toolId.isEmpty())
            return;
        QDockWidget* strip  = dockForStripEditor(idx);
        auto*        editor = strip ? qobject_cast<StripEdit::Editor*>(strip->widget()) : nullptr;
        if (!editor)
            return;
        // A grade is the strip's: the card lands on it, whatever was selected before.
        if (toolId == QLatin1String("grade"))
            editor->selectStrip();
        editor->setTool(toolId);
    });
    // A workspace-level edit made from this dock (canvas-profile content, output format) is on the
    // workspace's timeline (the project records it there); every view of the workspace follows it.
    connect(projectWidget, &Project::workspaceEdited, this, &MainWindow::workspaceChanged);
    // Keep this dock's palette-derived views (output combo, assigned-canvas list) in step with every
    // workspace-level change, this dock's own included. The connection is auto-removed when the widget
    // is destroyed, so no manual bookkeeping is needed.
    connect(this, &MainWindow::workspaceChanged,
            projectWidget, &Project::refreshProfileViews);
    newDock->setWidget(projectWidget);
    // Same custom title bar as the workspace/strip/action docks (dock/detach, max, close).
    installDockTitleBar(newDock);

    // The stack is already in the group — historyFor() put it there, and it stays for the session
    // whether or not this dock exists. Ctrl+Z / Ctrl+Y target whichever project's stack is active, and
    // the dock in front decides which that is (visibilityChanged below, and in the strip dock).


    // Track which project is "current" for F5 / the Render menu (the raised dock), and make this
    // project's undo stack active while its tab is visible.
    m_activeProjectIndex = projectIndex;
    refreshProjectActions();
    connect(newDock, &QDockWidget::visibilityChanged, this,
            [this, newDock, projectWidget](bool visible) {
        const int idx = projectIndexOf(newDock);
        if (visible) {
            m_activeProjectIndex = idx;
            refreshProjectActions();
            m_undoGroup->setActiveStack(projectWidget->undoStack());
            retargetStatusAdvisories();   // the bar speaks about the project being looked at
        } else if (idx == m_activeProjectIndex) {
            retargetActiveProject(idx);   // its strip may still be in sight
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

    // Minimise → toggle dock <-> detach. Docking tabs it beside the Workspace, except the Workspace dock
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
    // Hidden, not destroyed. The dock is the way in, not the thing itself: an open strip editor keeps
    // sending edits to this widget, and those edits need its records, its overlay directory and its
    // history. Destroying it dropped all three silently — the editor went on showing changes nobody
    // had recorded. Reopening from the project list finds this dock and raises it.
    dock->hide();
}

// ---------------------------------------------------------------------------
// Strip editor dock (per-project, floating, custom title bar)
// ---------------------------------------------------------------------------

QDockWidget *MainWindow::dockForStripEditor(int modelIndex) const
{
    const QString uid = projectUidAt(modelIndex);
    if (uid.isEmpty())
        return nullptr;
    for (QDockWidget *dock : m_openStripDocks)
        if (dock->property("projectUid").toString() == uid)
            return dock;
    return nullptr;
}

void MainWindow::refreshStripEditor(QDockWidget *dock)
{
    if (!dock) return;
    auto *editor = qobject_cast<StripEdit::Editor *>(dock->widget());
    if (!editor) return;

    const int idx = projectIndexOf(dock);
    if (idx < 0) {
        editor->setPreviewSource({}, {}, {}, {}, workspaceCacheDir());   // project gone → empty state
        return;
    }

    // The strip is built from the project's INPUT pages, not from the rendered output: the editor has to
    // work before the first render, and a grade previewed on the committed output would be applied on
    // top of the one the render already baked in. Everything the library needs to put a page through its
    // page domain goes across: the inputs in strip order, the resolved output profile (target width +
    // slice height), and the canvas profiles that decide margins. The workspace cache dir feeds the
    // editor's proxy thumbnails — already warm, the Input tab's tiles use the same cache for the same
    // files.
    const auto &project = m_workspace.projectItems[static_cast<std::size_t>(idx)];
    editor->setPreviewSource(project.inputsInOrder(),
                             resolveOutputProfileFor(project),
                             m_workspace.canvasProfiles(),
                             project.canvasProfileIds(),
                             workspaceCacheDir());

    // Feed the Grade panel + live preview. The strip's pixels are ungraded by construction, so this
    // previews cleanly whether or not a render has happened, and a render does not change the view.
    editor->setColourCorrection(project.colourCorrection);

    // Text & bubbles: the library's placements plus the GUI's authoring records for them. The editor
    // resolves each overlay's page anchor against the layout it just built — the same arithmetic the
    // render does — so a bubble previews exactly where it will be baked.
    editor->setOverlaySource(project.getStripOverlays(),
                             m_overlayRecords.records(QString::fromStdString(project.uid)));
}

void MainWindow::reserveRoomForDocks()
{
    // A project with nothing in it lays out the same panels as any other: the Input and Output toolboxes
    // are what need the height, and they do not depend on the files.
    Platemaker::Models::Workspace scratch;
    scratch.projectItems.emplace_back();
    History history;
    History workspaceHistory;

    QDockWidget project(this);
    project.setWidget(new Project(0, scratch, QString(), &history, &workspaceHistory, &project));
    installDockTitleBar(&project);
    QDockWidget strip(this);
    strip.setWidget(new StripEdit::Editor(*m_presets, &strip));
    installDockTitleBar(&strip);
    tabifyDockWidget(ui->dockWidgetWorkspace, &project);   // where openProjectDock() puts one
    tabifyDockWidget(ui->dockWidgetWorkspace, &strip);     // and openStripEditorDock()

    // The layout's minimum, tab bar included. Measured before the scratch docks leave it.
    m_dockRoom = layout()->totalMinimumSize();
    removeDockWidget(&strip);
    removeDockWidget(&project);
    setMinimumSize(m_dockRoom);
}

void MainWindow::openStripEditorDock(int projectIndex)
{
    if (projectIndex < 0 || projectIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    // Already open → raise and refresh (the outputs may have changed since it was last shown).
    if (QDockWidget *existing = dockForStripEditor(projectIndex)) {
        existing->show();
        existing->raise();
        refreshStripEditor(existing);
        return;
    }

    const QString name = QString::fromStdString(
        m_workspace.projectItems[static_cast<std::size_t>(projectIndex)].name);

    const QString projectUid = projectUidAt(projectIndex);
    QDockWidget *dock = new QDockWidget(this);
    dock->setWindowTitle(tr("Strip — %1").arg(name));
    dock->setProperty("projectUid", projectUid);
    dock->setObjectName(QStringLiteral("strip:") + projectUid);
    dock->setFeatures(QDockWidget::DockWidgetMovable  |
                      QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetFloatable);
    // Free to dock anywhere but the Action column (Right), like the project/workspace docks — the strip
    // is never tab-combined with Action.
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);

    auto *editor = new StripEdit::Editor(*m_presets, dock);
    // "Render & view": outputs are cheap/regenerable, so this just runs the normal render for the
    // project; onRenderFinished refreshes this editor when it completes. If the project is already up to
    // date, startRender is a no-op and the already-loaded committed slices stay shown.
    connect(editor, &StripEdit::Editor::presetsDialogRequested, this, &MainWindow::openPresetsDialog);
    connect(editor, &StripEdit::Editor::renderAndViewRequested, this, [this, projectUid] {
        if (const int idx = projectIndexForUid(projectUid); idx >= 0)
            (void)startRender(idx);
    });
    // A settled grade edit → persist it onto the project as one undo step, named by the editor, which
    // knows what was done (undoable, via the Project dock).
    connect(editor, &StripEdit::Editor::colourCorrectionCommitted, this,
            [this, projectUid](const Platemaker::Models::ColourCorrection &cc, const QString &undoText) {
        if (auto *pw = projectWidget(projectIndexForUid(projectUid)))
            pw->applyColourCorrection(cc, undoText);
    });

    // Text & bubbles. Creation goes through the project because the *library* mints the overlay's uid
    // and hashes its bitmap; every other edit arrives as the complete new state and is stored as one
    // undo step. Both are guarded the same way the grade is: a project that has been removed has no
    // widget and no history, and the edit is dropped rather than applied untracked. Merely *closing*
    // the dock does not reach here — it hides, and the widget goes on recording.
    connect(editor, &StripEdit::Editor::recordCreated, this,
            [this, projectUid](const ObjectRecord &record, double xFrac, double yFrac,
                                 double wFrac, const QString &anchorUid) {
        if (auto *pw = projectWidget(projectIndexForUid(projectUid)))
            pw->createOverlay(record, xFrac, yFrac, wFrac, anchorUid);
    });
    connect(editor, &StripEdit::Editor::artworkImportRequested, this,
            [this, projectUid](const QString &file, double xFrac, double yFrac, double wFrac,
                                 QSize naturalSize, const QString &anchorUid) {
        if (auto *pw = projectWidget(projectIndexForUid(projectUid)))
            pw->importOverlayArtwork(file, xFrac, yFrac, wFrac, naturalSize, anchorUid);
    });
    connect(editor, &StripEdit::Editor::overlaysCommitted, this,
            [this, projectUid](const std::vector<Platemaker::Models::StripOverlay> &overlays,
                                 const ObjectRecord::Map &records, const QString &undoText) {
        if (auto *pw = projectWidget(projectIndexForUid(projectUid)))
            pw->applyOverlays(overlays, records, undoText);
    });
    // Its own advisory strip, along the bottom. Dragged out, this dock is a top-level window with no
    // status bar; maximised, it covers the one behind it. Either way the chapter's problems would be
    // invisible in the very window they are worked on. Docked, the strip stands down — the main
    // window's status bar is already saying it, and saying it twice teaches nothing.
    editor->setAdvisories(m_advisories, projectUid);
    editor->setAdvisoriesActive(dock->isFloating());
    connect(dock, &QDockWidget::topLevelChanged, editor, &StripEdit::Editor::setAdvisoriesActive);
    // What has happened, as opposed to what is wrong: the bar's left side, which expires on its own.
    // The advisories keep the right side, where a badge lives as long as its condition does.
    connect(editor, &StripEdit::Editor::noted, this,
            [this](const QString& text) { statusBar()->showMessage(text, k_noticeMs); });

    dock->setWidget(editor);

    // Shared custom title bar (minimise = dock/detach, maximise = fill screen, close = hide).
    installDockTitleBar(dock);

    // Looking at the strip editor makes this project's history the one Ctrl+Z targets — the same stack
    // its project dock uses, since it is the same document and the same train of thought. What the
    // window decides is the *project*, not which half of the work is undoable.
    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible) {
        const int idx = projectIndexOf(dock);
        if (!visible) {
            if (idx == m_activeProjectIndex)
                retargetActiveProject(idx);   // its project dock may still be in sight
            return;
        }
        if (idx < 0 || idx >= int(m_workspace.projectItems.size()))
            return;
        m_undoGroup->setActiveStack(historyFor(idx)->stack());
        // Looking at a chapter's strip is looking at that chapter, for the status bar as much as for
        // Ctrl+Z — otherwise the bar would go on describing whichever project dock was raised last.
        m_activeProjectIndex = idx;
        refreshProjectActions();
        retargetStatusAdvisories();
    });

    // Default placement on open: a tab beside the workspace panel, like a project dock. From there it is
    // free to split out, float (the title bar's minimise) or re-dock where it is dropped.
    tabifyDockWidget(ui->dockWidgetWorkspace, dock);

    m_openStripDocks.append(dock);
    refreshStripEditor(dock);   // lay the strip out from the inputs (also settles the strip width)

    dock->show();
    dock->raise();
    wireDockTabBars();
}
