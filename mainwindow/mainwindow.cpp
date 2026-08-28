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

#include <QAction>
#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>

#include <algorithm>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Top-level menu icons (SVG; replaces the old Unicode glyphs in the titles).
    // Requires the qsvg image plugin — pulled in by linking Qt::Svg.
    ui->menuPlatemaker->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/workspace.svg")));
    ui->menuCanvas_Profile->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/canvas.svg")));
    ui->menu_Output_Settings->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/output.svg")));
    ui->menu_Process->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/process.svg")));
    ui->menuTemplates->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/templates.svg")));
    ui->menu_About->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/about.svg")));

    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);

    // QMainWindow always reserves a central-widget region and draws a separator
    // between it and the dock area. We run a dock-only layout, so remove the
    // central widget entirely — this kills the phantom right-side separator
    // while leaving the working Workspace|Action splitter intact.
    delete takeCentralWidget();

    // Both docks live in ONE dock area, split horizontally: dockWidgetAction is
    // declared as a Right-area dock in the .ui, so move it next to the workspace
    // dock to get a single Workspace|Action splitter.
    removeDockWidget(ui->dockWidgetAction);
    splitDockWidget(ui->dockWidgetWorkspace, ui->dockWidgetAction, Qt::Horizontal);
    ui->dockWidgetAction->show();

    // Re-dock guard for the two shell docks. Floating one out and snapping it back otherwise corrupts
    // the layout: Qt re-docks the returning dock into the Left area but with a degenerate geometry that
    // overlaps the incumbent (which grabs the whole area), and it does not self-correct — only a manual
    // splitter drag recovered it. Cache the docked widths at the moment of floating, and on return
    // rebuild the split via reestablishShellSplit(). Deferred to a singleShot(0): doing it synchronously
    // here fights Qt's in-progress re-dock and produces a worse, unrecoverable state. The project docks
    // carry their own topLevelChanged re-dock guard (openProjectDock); this is the shell-dock analogue.
    for (QDockWidget *shellDock : {ui->dockWidgetWorkspace, ui->dockWidgetAction}) {
        connect(shellDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
            if (floating) {
                m_workspaceDockWidthBeforeFloat = ui->dockWidgetWorkspace->width();
                m_actionDockWidthBeforeFloat    = ui->dockWidgetAction->width();
            } else {
                QTimer::singleShot(0, this, [this] { reestablishShellSplit(); });
            }
        });
    }

    // Keyboard shortcuts (the .ui already sets text labels, we only add keys)
    ui->actionOpen_workspace->setShortcut(QKeySequence::Open);
    ui->actionNew_workspace->setShortcut(QKeySequence::New);
    ui->actionSave_Ctrl_S->setShortcut(QKeySequence::Save);
    ui->actionSave_as_Ctrl_Shift_S->setShortcut(QKeySequence::SaveAs);

    // --- Workspace menu ---
    connect(ui->actionOpen_workspace,               &QAction::triggered, this, &MainWindow::onOpenWorkspace);
    connect(ui->actionNew_workspace,                &QAction::triggered, this, &MainWindow::onNewWorkspace);
    connect(ui->actionSave_Ctrl_S,                  &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSave_as_Ctrl_Shift_S,         &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(ui->actionClose_workspace,              &QAction::triggered, this, &MainWindow::onCloseWorkspace);
    connect(ui->actionReveal_workspace_in_Explorer, &QAction::triggered, this, &MainWindow::onRevealInExplorer);
    connect(ui->actionShow_workspace_panel, &QAction::triggered, this, [this]{
        ui->dockWidgetWorkspace->show();
        ui->dockWidgetWorkspace->raise();
    });

    // "Open recent workspace" — attach a dynamic submenu to the existing action.
    // Rebuilt on every show so it always reflects the current QSettings list.
    m_recentMenu = new QMenu(this);
    m_recentMenu->setToolTipsVisible(true);
    ui->actionOpen_recent_workspace->setMenu(m_recentMenu);
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();

    // --- Canvas Profiles menu ---
    connect(ui->actionManage_profiles,      &QAction::triggered, this, &MainWindow::onManageCanvasProfiles);
    connect(ui->actionNew_canvas_profile,   &QAction::triggered, this, &MainWindow::onNewCanvasProfile);
    connect(ui->actionEdit_active_profile,  &QAction::triggered, this, &MainWindow::onEditActiveCanvasProfile);
    connect(ui->actionImport_canvas_profiles, &QAction::triggered, this, &MainWindow::onImportCanvasProfiles);
    connect(ui->actionExport_canvas_profiles, &QAction::triggered, this, &MainWindow::onExportCanvasProfiles);

    // --- Output menu ---
    connect(ui->actionManage_output_profiles, &QAction::triggered, this, &MainWindow::onManageOutputProfiles);
    connect(ui->actionNew_output_profile,     &QAction::triggered, this, &MainWindow::onNewOutputProfile);
    connect(ui->actionEdit_output_settings,   &QAction::triggered, this, &MainWindow::onEditActiveOutputProfile);
    connect(ui->actionImport_output_profiles, &QAction::triggered, this, &MainWindow::onImportOutputProfiles);
    connect(ui->actionExport_output_profiles, &QAction::triggered, this, &MainWindow::onExportOutputProfiles);

    // --- Projects panel (managed via the workspace dock's context menu) ---
    connect(ui->listWidgetProjects, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onProjectDoubleClicked);
    ui->listWidgetProjects->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetProjects, &QWidget::customContextMenuRequested,
            this, &MainWindow::onProjectsContextMenu);

    // --- Action log (right-click: Copy/Select All + Save log as… / Clear) ---
    ui->textBrowserActionLogs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->textBrowserActionLogs, &QWidget::customContextMenuRequested,
            this, &MainWindow::onActionLogContextMenu);

    // Apply the slim styled progress bar look at rest (idle 0%), before the first render.
    setProgressValue(0, false);

    // --- Templates menu ---
    connect(ui->actionManage_templates,   &QAction::triggered, this, &MainWindow::onManageTemplates);
    connect(ui->actionOpen_dir_templates, &QAction::triggered, this, &MainWindow::onOpenTemplatesDir);

    // --- Process menu / render ---
    // F5 is the primary render key (shown in the menu); Ctrl+R is an accepted alternate (the
    // "run" convention in many editors). setShortcuts keeps F5 as the displayed one.
    ui->actionRender_current_project_F5->setShortcuts(
        {QKeySequence(Qt::Key_F5), QKeySequence(QStringLiteral("Ctrl+R"))});
    ui->actionRender_all_projects_F6->setShortcut(Qt::Key_F6);
    ui->actionStop_Esc->setShortcut(Qt::Key_Escape);
    connect(ui->actionRender_current_project_F5, &QAction::triggered, this, [this]{
        if (m_activeProjectIndex >= 0) (void)startRender(m_activeProjectIndex);
    });
    connect(ui->actionRender_all_projects_F6, &QAction::triggered,
            this, &MainWindow::onRefreshAllProjects);
    connect(ui->actionStop_Esc, &QAction::triggered, this, &MainWindow::cancelRender);
    connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::cancelRender);
    ui->pushButtonStop->setEnabled(false);

    // --- About menu ---
    connect(ui->actionVersion, &QAction::triggered, this, &MainWindow::onShowVersion);
    connect(ui->actionAuthors, &QAction::triggered, this, &MainWindow::onShowAuthors);
    connect(ui->actionHelp,    &QAction::triggered, this, &MainWindow::onShowHelp);

    // --- Undo / redo (Workspace-menu actions; group routes Ctrl+Z / Ctrl+Y to the active context) ---
    setupUndo();

    updateTitleBar();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// closeEvent
// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Stop an in-flight render before tearing down (the worker checks the token
    // between slices; wait briefly for it to unwind).
    if (m_rendering) {
        m_cancelToken.cancel();
        if (m_renderThread)
            m_renderThread->wait(5000);
    }
    maybeSave() ? event->accept() : event->ignore();
}

void MainWindow::reestablishShellSplit()
{
    // Rebuild the Workspace | Action split, exactly as the constructor does at startup. After a float
    // returns, Qt leaves the returning dock in the Left area with a degenerate, overlapping geometry —
    // not tabified, and not a resizable sibling of the incumbent (which occupies the whole area), so a
    // plain resizeDocks is a no-op (verified). removeDockWidget detaches the Action dock, which forces
    // Workspace to relayout as the sole, full-area occupant (undoing its degenerate geometry);
    // splitDockWidget then re-seats Action beside it, recreating the real splitter that a manual drag
    // relied on. Anchoring on Workspace leaves any project docks tabbed on it untouched. Neither call
    // floats anything, so there is no topLevelChanged re-entry. Called deferred (singleShot) so it runs
    // after Qt's own re-dock settles — doing it synchronously fights the transition and worsens it.
    removeDockWidget(ui->dockWidgetAction);
    splitDockWidget(ui->dockWidgetWorkspace, ui->dockWidgetAction, Qt::Horizontal);
    ui->dockWidgetAction->show();

    // Bias the fresh split back to the pre-float proportion. The cache is always populated before a
    // return (a dock must float before it can re-dock), so the guard is just defensive.
    if (m_workspaceDockWidthBeforeFloat > 0 && m_actionDockWidthBeforeFloat > 0)
        resizeDocks({ui->dockWidgetWorkspace, ui->dockWidgetAction},
                    {m_workspaceDockWidthBeforeFloat, m_actionDockWidthBeforeFloat},
                    Qt::Horizontal);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool MainWindow::maybeSave()
{
    // Authoritative check — independent of the eager m_dirty flag, so a forgotten
    // setDirty() can never silently drop changes.
    if (!isWorkspaceModified()) return true;

    // Optional preference: save silently instead of prompting (Stage 6 setting).
    QSettings settings;
    if (settings.value("autoSaveOnExit", false).toBool()) {
        onSave();
        return !isWorkspaceModified(); // proceed only if the save actually succeeded
    }

    const auto btn = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("The workspace has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (btn == QMessageBox::Save)    { onSave(); return !isWorkspaceModified(); }
    if (btn == QMessageBox::Discard) return true;
    return false; // Cancel
}

void MainWindow::loadWorkspace(const QString &path)
{
    closeWorkspace();

    // The report carries any profile-identifier collisions the load had to repair; the
    // repair itself happens either way, this overload just lets us explain it.
    Platemaker::Infrastructure::WorkspaceRepairReport repair;
    try {
        m_workspace = m_serializer.load(path.toStdString(), repair);
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot open workspace:\n%1").arg(e.what()));
        return;
    }

    m_workspacePath = path;
    m_activeCanvasProfileName = m_workspace.canvasProfiles().empty()
        ? QString{}
        : QString::fromStdString(m_workspace.canvasProfiles().front().name);
    // Default the active output profile to the first user profile; if the workspace has none,
    // fall back to a preset (always available from the catalogue) so "active" still means
    // something and rendering has a profile to resolve. Tracked by id (names may repeat).
    m_activeOutputProfileId = !m_workspace.outputProfiles().empty()
        ? QString::fromStdString(m_workspace.outputProfiles().front().id)
        : (Platemaker::Models::outputProfilePresets().empty()
              ? QString{}
              : QString::fromStdString(Platemaker::Models::outputProfilePresets().front().id));
    captureSnapshot();
    addToRecentWorkspaces(path);
    applyWorkspaceToUi();

    // Deferred to the next event-loop pass: applyWorkspaceToUi() only *queues* the
    // repaint, so showing a modal here would block it and leave the *previous*
    // workspace on screen behind the dialog — as if we were asking about the one being
    // closed rather than the one being opened.
    //
    // The repair notice goes first: it is the cause, the amber tiles are the effect, so
    // showing them the other way round would have the user reading about a symptom before
    // being told what produced it.
    QTimer::singleShot(0, this, [this, repair] {
        reportWorkspaceRepair(repair);
        warnIfCanvasConfigStale();
    });
}

void MainWindow::applyWorkspaceToUi()
{
    ui->listWidgetProjects->clear();

    // Display projects sorted by name (natural/numeric order) while leaving the
    // model order untouched — each item carries its real model index in UserRole,
    // so open project docks (which reference projects by index) stay valid.
    std::vector<int> order(m_workspace.projectItems.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        order[static_cast<std::size_t>(i)] = i;

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return collator.compare(
            QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(a)].name),
            QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(b)].name)) < 0;
    });

    for (int modelIndex : order) {
        auto* item = new QListWidgetItem(
            QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(modelIndex)].name));
        item->setData(Qt::UserRole, modelIndex);
        ui->listWidgetProjects->addItem(item);
    }

    updateTitleBar();
}

void MainWindow::closeWorkspace()
{
    // Close any open project docks and clear the list. Each dock's Project owns its undo stack, whose
    // destructor removes itself from the group — so no manual removeStack() is needed here.
    for (QDockWidget *dock : std::as_const(m_openProjectDocks))
        dock->deleteLater();
    m_openProjectDocks.clear();

    // Drop the workspace-scope undo history (a new/closed workspace starts fresh).
    if (m_workspaceUndoStack)
        m_workspaceUndoStack->clear();

    // Clear the workspace model and reset state.
    m_workspace     = Platemaker::Models::Workspace{};
    m_workspacePath.clear();
    m_savedSnapshot.clear();
    m_activeCanvasProfileName.clear();
    m_activeOutputProfileId.clear();
    setDirty(false);

    // Clear the project list in the UI and update the title bar.
    ui->listWidgetProjects->clear();
    updateTitleBar();
}

void MainWindow::setDirty(bool dirty)
{
    // Eager flag driving the title-bar asterisk (*)
    m_dirty = dirty;
    updateTitleBar();
}

void MainWindow::captureSnapshot()
{
    // Capture the current workspace's serialized form as the "saved" baseline.
    // Call after every successful load/save; clears the dirty flag.
    m_savedSnapshot = m_workspacePath.isEmpty()
        ? QString{}
        : QString::fromStdString(m_serializer.serialize(m_workspace));
    setDirty(false);
}

bool MainWindow::isWorkspaceModified() const
{
    // Authoritative change check: true if the workspace differs from the last
    if (m_workspacePath.isEmpty())
        return false; // no workspace loaded — nothing to save
    return QString::fromStdString(m_serializer.serialize(m_workspace))
           != m_savedSnapshot;
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void MainWindow::setupUndo()
{
    m_undoGroup = new QUndoGroup(this);

    // The workspace stack (profiles, project rename, templates). Per-project stacks are added to the
    // group as their docks open (see openProjectDock).
    m_workspaceUndoStack = new QUndoStack(this);
    m_workspaceUndoStack->setUndoLimit(10);
    m_undoGroup->addStack(m_workspaceUndoStack);
    m_undoGroup->setActiveStack(m_workspaceUndoStack);

    // Wire the existing Undo/Redo actions (defined in the .ui, in the Workspace menu) to the group.
    // The group always targets the *active* stack — whichever tab (a project dock, or the workspace
    // panel) is in front — so Ctrl+Z / Ctrl+Y do the right thing. We drive these actions ourselves
    // rather than use QUndoGroup::createUndoAction, because the actions already live in the .ui.
    ui->actionUndo->setShortcut(QKeySequence::Undo);   // Ctrl+Z
    ui->actionRedo->setShortcut(QKeySequence::Redo);   // Ctrl+Y (+ Ctrl+Shift+Z on Windows)
    connect(ui->actionUndo, &QAction::triggered, m_undoGroup, &QUndoGroup::undo);
    connect(ui->actionRedo, &QAction::triggered, m_undoGroup, &QUndoGroup::redo);

    // Enable each only when the active stack has something to undo/redo, and reflect the command name
    // in the menu text ("Undo Add files") — mirroring what createUndoAction would have given us.
    const auto refreshUndoText = [this](const QString& t){
        ui->actionUndo->setText(t.isEmpty() ? tr("Undo") : tr("Undo %1").arg(t));
    };
    const auto refreshRedoText = [this](const QString& t){
        ui->actionRedo->setText(t.isEmpty() ? tr("Redo") : tr("Redo %1").arg(t));
    };
    ui->actionUndo->setEnabled(m_undoGroup->canUndo());
    ui->actionRedo->setEnabled(m_undoGroup->canRedo());
    refreshUndoText(m_undoGroup->undoText());
    refreshRedoText(m_undoGroup->redoText());
    connect(m_undoGroup, &QUndoGroup::canUndoChanged, ui->actionUndo, &QAction::setEnabled);
    connect(m_undoGroup, &QUndoGroup::canRedoChanged, ui->actionRedo, &QAction::setEnabled);
    connect(m_undoGroup, &QUndoGroup::undoTextChanged, this, refreshUndoText);
    connect(m_undoGroup, &QUndoGroup::redoTextChanged, this, refreshRedoText);

    // The workspace panel coming to the front makes the workspace stack active. Each project dock does
    // the symmetric thing for its own stack (openProjectDock).
    connect(ui->dockWidgetWorkspace, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_undoGroup && m_workspaceUndoStack)
            m_undoGroup->setActiveStack(m_workspaceUndoStack);
    });
}

void MainWindow::applyWorkspaceSnapshot(const QString& snapshot)
{
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).restoreMeta(snapshot.toStdString());

    // Names may have changed → refresh the project list and open dock titles; profile palettes may
    // have changed → refresh every open dock's palette-derived views. Project *contents* are untouched
    // (they live on each project's own undo stack).
    applyWorkspaceToUi();
    for (QDockWidget* dock : std::as_const(m_openProjectDocks)) {
        const int idx = dock->property("projectIndex").toInt();
        if (idx >= 0 && idx < static_cast<int>(m_workspace.projectItems.size()))
            dock->setWindowTitle(QString::fromStdString(
                m_workspace.projectItems[static_cast<std::size_t>(idx)].name));
    }
    emit workspaceProfilesChanged();
    setDirty(true);
}

void MainWindow::commitWorkspaceEdit(const QString& text, const std::function<void()>& mutate)
{
    const QString before = QString::fromStdString(
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).snapshotMeta());
    mutate();
    QString after = QString::fromStdString(
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).snapshotMeta());

    if (after == before)   // nothing workspace-level actually changed → no undo step
        return;

    m_workspaceUndoStack->push(new WorkspaceSnapshotCommand(this, before, std::move(after), text));
}

void MainWindow::updateTitleBar()
{
    // Show the workspace file name (or just "Platemaker" if none) and an asterisk
    if (m_workspacePath.isEmpty()) {
        setWindowTitle(tr("Platemaker"));
    } else {
        setWindowTitle(tr("Platemaker — %1%2")
            .arg(QFileInfo(m_workspacePath).fileName(),
                 m_dirty ? QStringLiteral("*") : QString{}));
    }
}
