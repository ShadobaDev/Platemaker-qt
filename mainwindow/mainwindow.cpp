#include "mainwindow.hpp"
#include "advisories.hpp"
#include "advisorybar.hpp"
#include "recordsvg.hpp"
#include "ui_mainwindow.h"
#include "project.hpp"
#include "workspacesnapshotcommand.hpp"
#include "canvasprofiledialog.hpp"
#include "managecanvasprofilesdialog.hpp"
#include "manageoutputprofilesdialog.hpp"
#include "outputprofiledialog.hpp"
#include "templatesdialog.hpp"
#include "renderworker.hpp"
#include "workspacelock.hpp"
#include "workspacefolder.hpp"
#include "presetstore.hpp"
#include "recordpainter.hpp"
#include "docktitlebar.hpp"
#include "verticallabel.hpp"

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QProgressBar>
#include <QResizeEvent>
#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

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
    ui->menuEdit->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/edit.svg")));
    ui->menuView->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/view.svg")));
    ui->menuCanvas_Profile->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/canvas.svg")));
    ui->menu_Output_Settings->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/output.svg")));
    ui->menuTools->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/tools.svg")));
    ui->menu_Process->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/process.svg")));
    ui->menu_About->menuAction()->setIcon(QIcon(QStringLiteral(":/icons/menu/about.svg")));

    // What is installed, as opposed to what a workspace brings: taken now, before any workspace's fonts are
    // registered. Measured (Windows): a font installed while Platemaker runs is not seen until a
    // restart anyway, so the list cannot go stale in a way the font database itself would not.
    m_installedFamilies = QFontDatabase::families();

    m_presets = new StripEdit::PresetStore(this);
    m_presets->setDefaultFamily(Painter::defaultFamily());

    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);

    // QMainWindow always reserves a central-widget region and draws a separator
    // between it and the dock area. We run a dock-only layout, so remove the
    // central widget entirely — this kills the phantom central separator and lets
    // the Left and Right dock areas meet directly across the main splitter.
    delete takeCentralWidget();

    // Dock freedom is partitioned by allowed areas, not enforced with re-dock guards:
    //  - The Action panel is pinned to the Right area alone. Nothing else is allowed there, so it can
    //    never be tab-combined with another dock and can never wander — it stays its own right column.
    //  - Workspace and the project docks may live anywhere *except* that right column, free to split
    //    (horizontally and vertically) and tab among themselves via AllowNestedDocks.
    // The docks are already seated in their .ui-declared areas (Workspace = Left, Action = Right), which
    // is exactly the split we want, so there is no split-dance to run here.
    ui->dockWidgetAction->setAllowedAreas(Qt::RightDockWidgetArea);
    ui->dockWidgetWorkspace->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea |
                                             Qt::BottomDockWidgetArea);

    // The Action column is a hard fixed-width side panel. setFixedWidth (min == max) is a constraint the
    // dock layout must honour, so QMainWindow can never widen it on a window resize/maximize — the extra
    // width is forced into the flexible left region instead. A fixed dock also freezes its native
    // separator, so we grow our own drag grip on its left edge and resize it by hand (see eventFilter).
    // This sidesteps Qt's fuzzy dock-resize distribution entirely.
    ui->dockWidgetAction->setFixedWidth(m_actionDockWidth);

    // Wrap the Action content as [grip | content] so the grip sits on the panel's inner-left edge.
    m_actionContent = ui->dockWidgetAction->widget();
    auto    *actionWrapper = new QWidget(ui->dockWidgetAction);
    auto    *wrapperLayout = new QHBoxLayout(actionWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);
    m_actionGrip = new QWidget(actionWrapper);
    m_actionGrip->setFixedWidth(k_actionGripWidth);
    m_actionGrip->setCursor(Qt::SplitHCursor);
    m_actionGrip->installEventFilter(this);
    wrapperLayout->addWidget(m_actionGrip);
    wrapperLayout->addWidget(m_actionContent);

    // The collapsed form: a narrow column under the title bar's stacked buttons that keeps in view what the
    // full panel says about the job (its status, progress and the project status) and a way to stop it.
    // Mirrors, fed by the same setters, rather than the full panel's widgets: those cannot turn on their side.
    m_actionCompact = new QWidget(actionWrapper);
    auto *compactLayout = new QVBoxLayout(m_actionCompact);
    compactLayout->setContentsMargins(0, 0, 0, 0);
    m_compactActionStatus = new VerticalLabel(m_actionCompact);
    m_compactActionStatus->setText(ui->textBrowserActionStatus->placeholderText());
    m_compactProgress = new QProgressBar(m_actionCompact);
    m_compactProgress->setOrientation(Qt::Vertical);
    m_compactProgress->setTextVisible(false);
    m_compactProgress->setInvertedAppearance(true);   // fills top to bottom, the way the column reads
    m_compactProgress->setMaximumHeight(k_compactProgressMaxLength);
    m_compactProjectStatus = new VerticalLabel(m_actionCompact);
    m_compactProjectStatus->setText(ui->textBrowserProjectStatus->placeholderText());
    m_compactStop = new QToolButton(m_actionCompact);
    m_compactStop->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_compactStop->setToolTip(ui->pushButtonStop->text());
    m_compactStop->setFocusPolicy(Qt::NoFocus);
    // Stacked at the bottom, where the full panel keeps its Stop. The texts take what they need and the
    // progress bar up to its cap; the rest is empty space above them.
    compactLayout->addStretch(1);
    compactLayout->addWidget(m_compactActionStatus, 0, Qt::AlignHCenter);
    compactLayout->addWidget(m_compactProgress, 1, Qt::AlignHCenter);
    compactLayout->addWidget(m_compactProjectStatus, 0, Qt::AlignHCenter);
    compactLayout->addWidget(m_compactStop, 0, Qt::AlignHCenter);
    m_actionCompact->hide();
    wrapperLayout->addWidget(m_actionCompact);
    ui->dockWidgetAction->setWidget(actionWrapper);

    // Give the Workspace and Action docks the same custom title bar as the project / strip docks
    // (minimise = dock ⇄ detach, maximise = fill screen, close = hide). The Action dock keeps its
    // fixed-width grip content underneath — the title bar sits above it.
    installDockTitleBar(ui->dockWidgetWorkspace);
    installDockTitleBar(ui->dockWidgetAction);

    // Only the Action panel collapses, and it comes back the way it was left.
    auto *actionBar = qobject_cast<DockTitleBar *>(ui->dockWidgetAction->titleBarWidget());
    actionBar->enableCollapse();
    connect(actionBar, &DockTitleBar::collapseToggled, this, &MainWindow::setActionCollapsed);
    connect(ui->dockWidgetAction, &QDockWidget::topLevelChanged, this, [this] { applyActionDockWidth(); });
    actionBar->setCollapsed(QSettings().value(QStringLiteral("actionPanelCollapsed"), false).toBool());

    // Keyboard shortcuts (the .ui already sets text labels, we only add keys)
    ui->actionOpen_workspace->setShortcut(QKeySequence::Open);
    ui->actionNew_workspace->setShortcut(QKeySequence::New);
    ui->actionSave_Ctrl_S->setShortcut(QKeySequence::Save);
    ui->actionSave_as_Ctrl_Shift_S->setShortcut(QKeySequence::SaveAs);

    // --- File and View menus ---
    connect(ui->actionOpen_workspace,               &QAction::triggered, this, &MainWindow::onOpenWorkspace);
    connect(ui->actionNew_workspace,                &QAction::triggered, this, &MainWindow::onNewWorkspace);
    connect(ui->actionSave_Ctrl_S,                  &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSave_as_Ctrl_Shift_S,         &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(ui->actionClose_workspace,              &QAction::triggered, this, &MainWindow::onCloseWorkspace);
    connect(ui->actionReveal_workspace_in_Explorer, &QAction::triggered, this, &MainWindow::onRevealInExplorer);
    connect(ui->actionExport_package,               &QAction::triggered, this, &MainWindow::onExportPackage);
    connect(ui->actionOpen_package,                 &QAction::triggered, this, &MainWindow::onOpenPackage);
    // Through close(), so quitting from the menu asks about unsaved changes exactly as the title bar does.
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);

    // --- Tools menu ---
    connect(ui->actionImport_bubble_presets, &QAction::triggered, this, &MainWindow::onImportBubblePresets);
    connect(ui->actionExport_bubble_presets, &QAction::triggered, this, &MainWindow::onExportBubblePresets);
    connect(ui->actionFonts, &QAction::triggered, this, &MainWindow::onFonts);
    connect(ui->actionShow_workspace_panel, &QAction::triggered, this, [this]{
        ui->dockWidgetWorkspace->show();
        ui->dockWidgetWorkspace->raise();
    });
    connect(ui->actionShow_action_panel, &QAction::triggered, this, [this]{
        ui->dockWidgetAction->show();
        ui->dockWidgetAction->raise();
    });
    // Below the application's panels, the project's: the current project's strip editor, and any
    // project's from the submenu. Both open it, or raise it when it is already open.
    ui->actionStrip_editor->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(ui->actionStrip_editor, &QAction::triggered, this, [this]{ openStripEditorDock(currentProjectIndex()); });
    m_stripEditorsMenu = new QMenu(this);
    ui->actionStrip_editors->setMenu(m_stripEditorsMenu);
    connect(m_stripEditorsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildStripEditorsMenu);
    connect(ui->menuView, &QMenu::aboutToShow, this, &MainWindow::refreshProjectActions);
    connect(ui->menu_Process, &QMenu::aboutToShow, this, &MainWindow::refreshProjectActions);

    // "Open recent workspace" — attach a dynamic submenu to the existing action.
    // Rebuilt on every show so it always reflects the current QSettings list.
    m_recentMenu = new QMenu(this);
    m_recentMenu->setToolTipsVisible(true);
    ui->actionOpen_recent_workspace->setMenu(m_recentMenu);
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();

    // --- Canvas menu ---
    connect(ui->actionManage_profiles,      &QAction::triggered, this, &MainWindow::onManageCanvasProfiles);
    connect(ui->actionNew_canvas_profile,   &QAction::triggered, this, &MainWindow::onNewCanvasProfile);
    connect(ui->actionEdit_active_profile,  &QAction::triggered, this, &MainWindow::onEditActiveCanvasProfile);

    // --- Output menu ---
    connect(ui->actionManage_output_profiles, &QAction::triggered, this, &MainWindow::onManageOutputProfiles);
    connect(ui->actionNew_output_profile,     &QAction::triggered, this, &MainWindow::onNewOutputProfile);
    connect(ui->actionEdit_output_settings,   &QAction::triggered, this, &MainWindow::onEditActiveOutputProfile);

    // --- Import/Export profiles: native submenus, populated on demand. Attaching a QMenu to the action
    //     makes it a real submenu (no QMenu::exec() from a triggered handler — that fragile pattern was
    //     the earlier bug). Each submenu is rebuilt every time it opens, so recent lists stay current.
    m_importCanvasMenu = new QMenu(this);
    m_importOutputMenu = new QMenu(this);
    m_exportCanvasMenu = new QMenu(this);
    m_exportOutputMenu = new QMenu(this);
    ui->actionImport_canvas_profiles->setMenu(m_importCanvasMenu);
    ui->actionImport_output_profiles->setMenu(m_importOutputMenu);
    ui->actionExport_canvas_profiles->setMenu(m_exportCanvasMenu);
    ui->actionExport_output_profiles->setMenu(m_exportOutputMenu);
    connect(m_importCanvasMenu, &QMenu::aboutToShow, this, [this]{ populateImportMenu(m_importCanvasMenu, true);  });
    connect(m_importOutputMenu, &QMenu::aboutToShow, this, [this]{ populateImportMenu(m_importOutputMenu, false); });
    connect(m_exportCanvasMenu, &QMenu::aboutToShow, this, [this]{ populateExportMenu(m_exportCanvasMenu, true);  });
    connect(m_exportOutputMenu, &QMenu::aboutToShow, this, [this]{ populateExportMenu(m_exportOutputMenu, false); });

    // --- Projects panel (managed via the workspace dock's context menu) ---
    // Selecting a chapter in the list does not open it: only its window does (currentProjectIndex()).
    connect(ui->listWidgetProjects, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onProjectDoubleClicked);
    ui->listWidgetProjects->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetProjects, &QWidget::customContextMenuRequested,
            this, &MainWindow::onProjectsContextMenu);

    // --- Action log (right-click: Copy/Select All + Save log as… / Clear) ---
    // A narrow icon column, then the message, wrapped to the panel's width.
    ui->treeWidgetActionLogs->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->treeWidgetActionLogs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidgetActionLogs, &QWidget::customContextMenuRequested,
            this, &MainWindow::onActionLogContextMenu);

    // Apply the slim styled progress bar look at rest (idle 0%), before the first render.
    setProgressValue(0, false);

    // --- Templates menu ---
    connect(ui->actionManage_templates,   &QAction::triggered, this, &MainWindow::onManageTemplates);
    connect(ui->actionOpen_dir_templates, &QAction::triggered, this, &MainWindow::onOpenTemplatesDir);

    // --- Render menu ---
    // F5 is the primary render key (shown in the menu); Ctrl+R is an accepted alternate (the
    // "run" convention in many editors). setShortcuts keeps F5 as the displayed one.
    ui->actionRender_current_project_F5->setShortcuts(
        {QKeySequence(Qt::Key_F5), QKeySequence(QStringLiteral("Ctrl+R"))});
    ui->actionRender_all_projects_F6->setShortcut(Qt::Key_F6);
    ui->actionStop_Esc->setShortcut(Qt::Key_Escape);
    connect(ui->actionRender_current_project_F5, &QAction::triggered, this, [this]{
        if (const int idx = currentProjectIndex(); idx >= 0) (void)startRender(idx);
    });
    connect(ui->actionRender_all_projects_F6, &QAction::triggered,
            this, &MainWindow::onRefreshAllProjects);
    connect(ui->actionStop_Esc, &QAction::triggered, this, &MainWindow::cancelRender);
    connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::cancelRender);
    connect(m_compactStop, &QToolButton::clicked, this, &MainWindow::cancelRender);
    setStopEnabled(false);

    // --- About menu ---
    connect(ui->actionVersion, &QAction::triggered, this, &MainWindow::onShowVersion);
    connect(ui->actionAuthors, &QAction::triggered, this, &MainWindow::onShowAuthors);
    connect(ui->actionHelp,    &QAction::triggered, this, &MainWindow::onShowHelp);

    // --- Undo / redo (Edit-menu actions; group routes Ctrl+Z / Ctrl+Y to the active context) ---
    setupUndo();

    // The registry knows nothing about what raises anything, and its subscribers know nothing about
    // each other. This one lives in the status bar; the strip editor grows a second when it floats.
    m_advisories = new Advisories(this);
    m_statusAdvisories = new AdvisoryBar(m_advisories, statusBar());
    statusBar()->addPermanentWidget(m_statusAdvisories);

    // Coming back to this window is when a takeover from another computer is worth hearing about — before
    // any more work goes into a workspace that can no longer be saved where it is.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_lock && !m_lock->stillOurs())
            QTimer::singleShot(0, this, &MainWindow::onWorkspaceTakenOver);
    });

    refreshProjectActions();
    updateTitleBar();
}

MainWindow::~MainWindow()
{
    delete ui;
    // m_lock lets the folder go here — which is also what covers quitting, since closeEvent() does not go
    // through closeWorkspace().
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyActionDockWidth();
}

void MainWindow::applyActionDockWidth()
{
    // Detached, the panel is a window like any other: resized freely by its edges, down to the docked
    // minimum. The grip and the fixed width are for the docked column only.
    const bool floating = ui->dockWidgetAction->isFloating();
    m_actionGrip->setVisible(!m_actionCollapsed && !floating);
    if (floating) {
        ui->dockWidgetAction->setMinimumWidth(k_actionDockDefaultWidth);
        ui->dockWidgetAction->setMaximumWidth(QWIDGETSIZE_MAX);
        return;
    }
    // Collapsed, the column is as wide as the wider of its stacked title-bar buttons and its mirrors.
    if (m_actionCollapsed) {
        const QWidget *bar = ui->dockWidgetAction->titleBarWidget();
        ui->dockWidgetAction->setFixedWidth(qMax(bar->sizeHint().width(), m_actionCompact->sizeHint().width()));
        return;
    }
    // If the window shrank below what the chosen Action width needs, shrink the panel to fit (never more
    // than half the window, never below its minimum). m_actionDockWidth — the user's choice — is left
    // untouched, so growing the window back restores it. setFixedWidth remains the thing that stops the
    // panel from ever growing on its own; this only clamps it down.
    const int maxW    = qMax(k_actionDockDefaultWidth, width() / 2);
    const int applied = qBound(k_actionDockDefaultWidth, m_actionDockWidth, maxW);
    ui->dockWidgetAction->setFixedWidth(applied);
}

void MainWindow::setActionCollapsed(bool collapsed)
{
    m_actionCollapsed = collapsed;
    // Remember the choice, not what shows now: a floating panel shows expanded but stays collapsed by choice.
    const auto *bar = qobject_cast<DockTitleBar *>(ui->dockWidgetAction->titleBarWidget());
    QSettings().setValue(QStringLiteral("actionPanelCollapsed"), bar->isCollapsed());
    m_actionContent->setVisible(!collapsed);
    m_actionCompact->setVisible(collapsed);
    applyActionDockWidth();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Manual resize of the fixed-width Action column via its left-edge grip. Dragging the grip left grows
    // the panel, dragging right shrinks it; the new width is applied with setFixedWidth so it stays put on
    // the next window resize. Clamped so it can't drop below its minimum or eat more than half the window.
    if (watched == m_actionGrip) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_actionGripDragging   = true;
                m_actionGripStartX     = me->globalPosition().toPoint().x();
                m_actionGripStartWidth = ui->dockWidgetAction->width();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_actionGripDragging) {
                auto     *me    = static_cast<QMouseEvent *>(event);
                const int delta = m_actionGripStartX - me->globalPosition().toPoint().x(); // left → grow
                const int maxW  = qMax(k_actionDockDefaultWidth, width() / 2);
                m_actionDockWidth =
                    qBound(k_actionDockDefaultWidth, m_actionGripStartWidth + delta, maxW);
                ui->dockWidgetAction->setFixedWidth(m_actionDockWidth);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (m_actionGripDragging) {
                m_actionGripDragging = false;
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
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

void MainWindow::loadWorkspace(const QString &requested)
{
    // The folder is claimed first: one in use elsewhere is not ours to tidy, so the wizard below — which
    // moves files to the Recycle Bin — only ever runs in a folder this window holds. Reopening the folder
    // already held keeps that hold across the close below.
    std::unique_ptr<WorkspaceLock> lock;
    if (!holdsFolderOf(requested)) {
        lock = lockFolderOf(requested);
        if (!lock)
            return;
    }
    const QString path = resolveSharedFolder(requested);
    if (path.isEmpty())
        return;
    if (!lock)
        lock = std::move(m_lock);

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
    m_lock          = std::move(lock);
    // The workspace's own fonts, before anything is drawn or checked: a family it brings is then simply
    // there — for the bubbles, the missing-fonts advisory and the heal alike — and wins over an installed
    // copy of the same family. Files added while it is open count from the next open.
    for (const QString &font : workspaceFontFiles(QFileInfo(path).absolutePath())) {
        const int id = QFontDatabase::addApplicationFont(font);
        if (id < 0)
            logAction(LogLevel::Warning,
                tr("Font could not be loaded: %1").arg(QDir::toNativeSeparators(font)));
        else
            m_workspaceFonts.insert(QFileInfo(font).fileName(), id);
    }
    // Bubbles carry their own authoring parameters inside the SVG the library composites, so the
    // records are read back from the assets themselves — there is no sidecar to fall out of step with
    // them. An asset that is missing, or was drawn elsewhere, simply yields no record: the overlay
    // still renders, it just cannot be re-typed.
    m_overlayRecords.clear();
    for (const auto& project : m_workspace.projectItems)
        m_overlayRecords.setRecords(QString::fromStdString(project.uid),
                                        Svg::readOverlays(project.getStripOverlays()));
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
        // Last: they write into the folder and may ask a question, and they are housekeeping — the least of
        // these. Only with the folder held: one another process may be writing into has files this workspace
        // cannot see the use of. The heal first: re-setting a lettered picture renames its wrapper, and the
        // sweep has to see the final set of files.
        if (m_lock && m_lock->isHeld() && m_lock->stillOurs()) {
            healFontFallbacks();
            sweepWorkspaceFolder();
        } else {
            logAction(LogLevel::Warning,
                tr("Unused files were not tidied: the workspace folder could not be locked."));
        }
    });
}

void MainWindow::refreshProjectActions()
{
    const int  idx = currentProjectIndex();
    const bool has = idx >= 0;
    // A project name is user text: an '&' in it would otherwise turn into a mnemonic.
    QString name = has ? QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(idx)].name)
                       : QString{};
    name.replace('&', QStringLiteral("&&"));

    ui->actionStrip_editor->setText(has ? tr("Strip editor — %1").arg(name)
                                        : tr("Strip editor (no project open)"));
    ui->actionStrip_editor->setEnabled(has);
    ui->actionRender_current_project_F5->setText(has ? tr("Render — %1").arg(name)
                                                     : tr("Render current project (no project open)"));
    ui->actionRender_current_project_F5->setEnabled(has);
    ui->actionStrip_editors->setEnabled(!m_workspace.projectItems.empty());
}

void MainWindow::rebuildStripEditorsMenu()
{
    // The project list is already in natural order (Chapter 9 before Chapter 12) and carries each row's
    // model index, so the menu reads it rather than sorting the workspace again.
    m_stripEditorsMenu->clear();
    for (int row = 0; row < ui->listWidgetProjects->count(); ++row) {
        const QListWidgetItem *item = ui->listWidgetProjects->item(row);
        const int idx = item->data(Qt::UserRole).toInt();
        QString label = item->text();
        label.replace('&', QStringLiteral("&&"));
        QAction *act = m_stripEditorsMenu->addAction(label);
        act->setCheckable(true);
        const QDockWidget *strip = dockForStripEditor(idx);
        act->setChecked(strip && !strip->isHidden());   // open, as isProjectOpen() reads a window
        connect(act, &QAction::triggered, this, [this, idx]{ openStripEditorDock(idx); });
    }
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

    // Every view of the workspace has just been rebuilt from the model, and the advisories are one of
    // them: this is the path a freshly opened workspace arrives by, with its conditions already true.
    refreshAllAdvisories();
    refreshProjectActions();

    updateTitleBar();
}

void MainWindow::closeWorkspace()
{
    // Every project is going, so every project history has nothing left to restore — and the widgets
    // its commands point at are destroyed just below. Dropped explicitly, because these stacks are
    // owned here and outlive the docks by design: left behind, they would keep Undo enabled for a
    // document that is no longer open.
    for (QUndoStack *stack : std::as_const(m_projectHistories)) {
        m_undoGroup->removeStack(stack);   // reparents to the stack itself
        delete stack;
    }
    m_projectHistories.clear();

    // Close any open project docks and clear the list.
    for (QDockWidget *dock : std::as_const(m_openProjectDocks))
        dock->deleteLater();
    m_openProjectDocks.clear();

    // Strip editor docks belong to the workspace's projects too — close them with it.
    for (QDockWidget *dock : std::as_const(m_openStripDocks))
        dock->deleteLater();
    m_openStripDocks.clear();

    // An index into a workspace that is going: kept, it would name a project of the next one.
    m_activeProjectIndex = -1;
    refreshProjectActions();

    // Drop the workspace-scope undo history (a new/closed workspace starts fresh).
    if (m_workspaceUndoStack)
        m_workspaceUndoStack->clear();

    // Every advisory was about a project in the workspace being closed, so there is no condition left
    // for any of them to describe.
    if (m_advisories)
        m_advisories->clearAll();

    // Clear the workspace model and reset state.
    m_workspace     = Platemaker::Models::Workspace{};
    m_overlayRecords.clear();
    m_workspacePath.clear();
    m_savedSnapshot.clear();
    m_activeCanvasProfileName.clear();
    m_activeOutputProfileId.clear();
    m_lock.reset();   // the folder is someone else's to open now
    for (const int id : std::as_const(m_workspaceFonts))
        QFontDatabase::removeApplicationFont(id);
    m_workspaceFonts.clear();
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

    // Wire the existing Undo/Redo actions (defined in the .ui, in the Edit menu) to the group.
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
    for (QDockWidget* dock : std::as_const(m_openStripDocks)) {
        const int idx = dock->property("projectIndex").toInt();
        if (idx >= 0 && idx < static_cast<int>(m_workspace.projectItems.size()))
            dock->setWindowTitle(tr("Strip — %1").arg(QString::fromStdString(
                m_workspace.projectItems[static_cast<std::size_t>(idx)].name)));
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
