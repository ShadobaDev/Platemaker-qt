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
// Construction / destruction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

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

    // --- Output menu ---
    connect(ui->actionManage_output_profiles, &QAction::triggered, this, &MainWindow::onManageOutputProfiles);
    connect(ui->actionNew_output_profile,     &QAction::triggered, this, &MainWindow::onNewOutputProfile);
    connect(ui->actionEdit_output_settings,   &QAction::triggered, this, &MainWindow::onEditActiveOutputProfile);

    // --- Projects panel (managed via the workspace dock's context menu) ---
    connect(ui->listWidgetProjects, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onProjectDoubleClicked);
    ui->listWidgetProjects->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetProjects, &QWidget::customContextMenuRequested,
            this, &MainWindow::onProjectsContextMenu);

    // --- Templates menu ---
    connect(ui->actionManage_templates,   &QAction::triggered, this, &MainWindow::onManageTemplates);
    connect(ui->actionOpen_dir_templates, &QAction::triggered, this, &MainWindow::onOpenTemplatesDir);

    // --- Process menu / render (F6 "all projects" deferred) ---
    ui->actionRender_current_project_F5->setShortcut(Qt::Key_F5);
    ui->actionStop_Esc->setShortcut(Qt::Key_Escape);
    connect(ui->actionRender_current_project_F5, &QAction::triggered, this, [this]{
        if (m_activeProjectIndex >= 0) startRender(m_activeProjectIndex);
    });
    connect(ui->actionStop_Esc, &QAction::triggered, this, &MainWindow::cancelRender);
    connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::cancelRender);
    ui->pushButtonStop->setEnabled(false);

    updateTitle();
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

    try {
        m_workspace = m_serializer.load(path.toStdString());
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot open workspace:\n%1").arg(e.what()));
        return;
    }

    m_workspacePath = path;
    m_activeCanvasProfileName = m_workspace.canvasProfiles.empty()
        ? QString{}
        : QString::fromStdString(m_workspace.canvasProfiles.front().name);
    m_activeOutputProfileName = m_workspace.outputProfiles.empty()
        ? QString{}
        : QString::fromStdString(m_workspace.outputProfiles.front().name);
    captureSnapshot();
    addToRecentWorkspaces(path);
    applyWorkspaceToUi();
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

    updateTitle();
}

void MainWindow::closeWorkspace()
{
    for (QDockWidget *dock : std::as_const(m_openProjectDocks))
        dock->deleteLater();
    m_openProjectDocks.clear();

    m_workspace     = Platemaker::Models::Workspace{};
    m_workspacePath.clear();
    m_savedSnapshot.clear();
    m_activeCanvasProfileName.clear();
    m_activeOutputProfileName.clear();
    setDirty(false);

    ui->listWidgetProjects->clear();
    updateTitle();
}

void MainWindow::setDirty(bool dirty)
{
    m_dirty = dirty;
    updateTitle();
}

void MainWindow::captureSnapshot()
{
    m_savedSnapshot = m_workspacePath.isEmpty()
        ? QString{}
        : QString::fromStdString(m_serializer.serialize(m_workspace));
    setDirty(false);
}

bool MainWindow::isWorkspaceModified() const
{
    if (m_workspacePath.isEmpty())
        return false; // no workspace loaded — nothing to save
    return QString::fromStdString(m_serializer.serialize(m_workspace))
           != m_savedSnapshot;
}
void MainWindow::updateTitle()
{
    if (m_workspacePath.isEmpty()) {
        setWindowTitle(tr("Platemaker"));
    } else {
        setWindowTitle(tr("Platemaker — %1%2")
            .arg(QFileInfo(m_workspacePath).fileName(),
                 m_dirty ? QStringLiteral("*") : QString{}));
    }
}
