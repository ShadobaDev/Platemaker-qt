#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "project.h"
#include "canvasprofiledialog.h"
#include "managecanvasprofilesdialog.h"
#include "manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QTabBar>
#include <QUrl>

#include <algorithm>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    ui->centralwidget->setFixedSize(0, 0);

    // dockWidgetAction is defined in the .ui as a Right-area dock, which creates
    // two separators (one left, one right of the empty central widget) — they can
    // overlap when the central widget has zero width. Fix: remove action from the
    // Right area and re-add it as a horizontal split of the workspace dock so the
    // whole window lives in ONE dock area with a single splitter.
    removeDockWidget(ui->dockWidgetAction);
    splitDockWidget(ui->dockWidgetWorkspace, ui->dockWidgetAction, Qt::Horizontal);
    ui->dockWidgetAction->show();

    // Keyboard shortcuts (the .ui already sets text labels, we only add keys)
    ui->actionOpen_worksapce->setShortcut(QKeySequence::Open);
    ui->actionNew_workspace->setShortcut(QKeySequence::New);
    ui->actionSave_Ctrl_S->setShortcut(QKeySequence::Save);
    ui->actionSave_as_Ctrl_Shift_S->setShortcut(QKeySequence::SaveAs);

    // --- Workspace menu ---
    connect(ui->actionOpen_worksapce,               &QAction::triggered, this, &MainWindow::onOpenWorkspace);
    connect(ui->actionNew_workspace,                &QAction::triggered, this, &MainWindow::onNewWorkspace);
    connect(ui->actionSave_Ctrl_S,                  &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSave_as_Ctrl_Shift_S,         &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(ui->actionClose_workspace,              &QAction::triggered, this, &MainWindow::onCloseWorkspace);
    connect(ui->actionReveal_workspace_in_Explorer, &QAction::triggered, this, &MainWindow::onRevealInExplorer);
    connect(ui->actionShow_workspace_panel, &QAction::triggered, this, [this]{
        ui->dockWidgetWorkspace->show();
        ui->dockWidgetWorkspace->raise();
    });

    // --- Canvas Profiles menu ---
    connect(ui->actionManage_profiles,      &QAction::triggered, this, &MainWindow::onManageCanvasProfiles);
    connect(ui->actionNew_canvas_profile,   &QAction::triggered, this, &MainWindow::onNewCanvasProfile);
    connect(ui->actionEdit_active_profile,  &QAction::triggered, this, &MainWindow::onEditActiveCanvasProfile);

    // --- Output menu ---
    connect(ui->actionManage_output_profiles, &QAction::triggered, this, &MainWindow::onManageOutputProfiles);
    connect(ui->actionNew_output_profile,     &QAction::triggered, this, &MainWindow::onNewOutputProfile);
    connect(ui->actionEdit_output_settings,   &QAction::triggered, this, &MainWindow::onEditActiveOutputProfile);

    // --- Projects menu + workspace panel button ---
    connect(ui->actionNew_project_chapter, &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->pushButtonNewProject,      &QPushButton::clicked, this, &MainWindow::onNewProject);
    connect(ui->listWidgetProjects,        &QListWidget::itemDoubleClicked,
            this, &MainWindow::onProjectDoubleClicked);

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
    maybeSave() ? event->accept() : event->ignore();
}

// ---------------------------------------------------------------------------
// Workspace menu slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenWorkspace()
{
    if (!maybeSave()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Workspace"), {},
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (!path.isEmpty())
        loadWorkspace(path);
}

void MainWindow::onNewWorkspace()
{
    if (!maybeSave()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("New Workspace"), {},
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (path.isEmpty()) return;

    closeWorkspace();

    Platemaker::Models::OutputProfile defaultOut;
    defaultOut.name        = "Webtoon Standard";
    defaultOut.targetWidth = 800;
    defaultOut.sliceHeight = 1280;

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

    setDirty(false);
    applyWorkspaceToUi();
}

void MainWindow::onSave()
{
    if (m_workspacePath.isEmpty()) { onSaveAs(); return; }

    try {
        m_serializer.save(m_workspace, m_workspacePath.toStdString());
        setDirty(false);
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot save workspace:\n%1").arg(e.what()));
    }
}

void MainWindow::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Workspace As"), m_workspacePath,
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (path.isEmpty()) return;

    m_workspacePath = path;
    onSave();
}

void MainWindow::onCloseWorkspace()
{
    if (!maybeSave()) return;
    closeWorkspace();
}

void MainWindow::onRevealInExplorer()
{
    if (m_workspacePath.isEmpty()) return;
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_workspacePath).absolutePath()));
}

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
    onSave();
}

void MainWindow::onProjectDoubleClicked(QListWidgetItem *item)
{
    const int index = ui->listWidgetProjects->row(item);
    if (index < 0 || index >= static_cast<int>(m_workspace.projectItems.size()))
        return;
    openProjectDock(index);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool MainWindow::maybeSave()
{
    if (!m_dirty) return true;

    const auto btn = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("The workspace has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (btn == QMessageBox::Save)    { onSave(); return !m_dirty; }
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
    setDirty(false);
    applyWorkspaceToUi();
}

void MainWindow::applyWorkspaceToUi()
{
    ui->listWidgetProjects->clear();
    for (const auto &proj : m_workspace.projectItems)
        ui->listWidgetProjects->addItem(QString::fromStdString(proj.name));

    updateTitle();
}

void MainWindow::closeWorkspace()
{
    for (QDockWidget *dock : std::as_const(m_openProjectDocks))
        dock->deleteLater();
    m_openProjectDocks.clear();

    m_workspace     = Platemaker::Models::Workspace{};
    m_workspacePath.clear();
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
        onSave();
    });
    newDock->setWidget(projectWidget);

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

// ---------------------------------------------------------------------------
// Canvas profile slots
// ---------------------------------------------------------------------------

static QString nowIsoTag()
{
    return QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz");
}

void MainWindow::onManageCanvasProfiles()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    ManageCanvasProfilesDialog dlg(this);

    QList<Platemaker::Models::CanvasProfile> profiles(
        m_workspace.canvasProfiles.begin(), m_workspace.canvasProfiles.end());
    dlg.setProfiles(profiles, m_activeCanvasProfileName);

    // Stage 5: connect template generation signal (placeholder for now)
    connect(&dlg, &ManageCanvasProfilesDialog::generateTemplatesRequested,
        this, [this](const Platemaker::Models::CanvasProfile&) {
            QMessageBox::information(this, tr("Coming soon"),
                tr("Template generation will be wired in Stage 5."));
        });

    if (dlg.exec() != QDialog::Accepted) return;

    const auto result = dlg.profiles();
    m_workspace.canvasProfiles.assign(result.begin(), result.end());
    m_activeCanvasProfileName = dlg.activeProfileName();

    // Assign IDs to any profiles added without one (dialog doesn't generate them)
    for (auto& p : m_workspace.canvasProfiles) {
        if (p.id.empty())
            p.id = "cp-" + nowIsoTag().toStdString();
    }

    setDirty(true);
    onSave();
}

void MainWindow::onNewCanvasProfile()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    CanvasProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    for (const auto& p : m_workspace.canvasProfiles) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("A canvas profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    profile.id = "cp-" + nowIsoTag().toStdString();
    m_workspace.canvasProfiles.push_back(profile);

    if (m_workspace.canvasProfiles.size() == 1)
        m_activeCanvasProfileName = QString::fromStdString(profile.name);

    setDirty(true);
    onSave();
}

void MainWindow::onEditActiveCanvasProfile()
{
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
    *it = dlg.profile();
    it->id = savedId; // preserve the stable ID

    if (m_activeCanvasProfileName == QString::fromStdString(oldName))
        m_activeCanvasProfileName = QString::fromStdString(it->name);

    setDirty(true);
    onSave();
}

// ---------------------------------------------------------------------------
// Output profile slots
// ---------------------------------------------------------------------------

void MainWindow::onManageOutputProfiles()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    ManageOutputProfilesDialog dlg(this);

    QList<Platemaker::Models::OutputProfile> profiles(
        m_workspace.outputProfiles.begin(), m_workspace.outputProfiles.end());
    dlg.setProfiles(profiles, m_activeOutputProfileName);

    if (dlg.exec() != QDialog::Accepted) return;

    const auto result = dlg.profiles();
    m_workspace.outputProfiles.assign(result.begin(), result.end());
    m_activeOutputProfileName = dlg.activeProfileName();

    setDirty(true);
    onSave();
}

void MainWindow::onNewOutputProfile()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    OutputProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    for (const auto& p : m_workspace.outputProfiles) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("An output profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    m_workspace.outputProfiles.push_back(profile);

    if (m_workspace.outputProfiles.size() == 1)
        m_activeOutputProfileName = QString::fromStdString(profile.name);

    setDirty(true);
    onSave();
}

void MainWindow::onEditActiveOutputProfile()
{
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

    const std::string oldName = it->name;
    *it = dlg.profile();

    if (m_activeOutputProfileName == QString::fromStdString(oldName))
        m_activeOutputProfileName = QString::fromStdString(it->name);

    setDirty(true);
    onSave();
}
