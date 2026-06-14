#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "project.h"

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

#include <stdexcept>

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
    for (QDockWidget *dock : m_openProjectDocks)
        dock->deleteLater();
    m_openProjectDocks.clear();

    m_workspace     = Platemaker::Models::Workspace{};
    m_workspacePath.clear();
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
            .arg(QFileInfo(m_workspacePath).fileName())
            .arg(m_dirty ? QStringLiteral("*") : QString{}));
    }
}

// ---------------------------------------------------------------------------
// Project dock management
// ---------------------------------------------------------------------------

void MainWindow::openProjectDock(int projectIndex)
{
    // Bring existing dock to front if already open
    for (QDockWidget *dock : m_openProjectDocks) {
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
    newDock->setWidget(new Project(newDock));

    // Always tabify with the workspace panel — keeps the layout in two columns.
    // On first open Qt promotes the area to a tab group; subsequent opens just add tabs.
    tabifyDockWidget(ui->dockWidgetWorkspace, newDock);

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
    if (dock->isFloating()) {
        dock->setFloating(false);
        if (m_openProjectDocks.size() > 1)
            tabifyDockWidget(m_openProjectDocks.first(), dock);
    } else {
        dock->setFloating(true);
    }
}
