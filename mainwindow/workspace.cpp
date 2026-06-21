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
// Workspace menu slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenWorkspace()
{
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    if (!maybeSave()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Workspace"), defaultDialogDir(),
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (!path.isEmpty())
        loadWorkspace(path);
}

void MainWindow::onNewWorkspace()
{
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    if (!maybeSave()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("New Workspace"), defaultDialogDir(),
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

    captureSnapshot();
    addToRecentWorkspaces(path);
    applyWorkspaceToUi();
}

void MainWindow::onSave()
{
    if (m_workspacePath.isEmpty()) { onSaveAs(); return; }

    try {
        m_serializer.save(m_workspace, m_workspacePath.toStdString());
        captureSnapshot();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot save workspace:\n%1").arg(e.what()));
    }
}

void MainWindow::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Workspace As"),
        m_workspacePath.isEmpty() ? defaultDialogDir() : m_workspacePath,
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (path.isEmpty()) return;

    m_workspacePath = path;
    onSave();
    if (!isWorkspaceModified())   // save succeeded
        addToRecentWorkspaces(path);
}

void MainWindow::onCloseWorkspace()
{
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
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
// Recent workspaces (advisory — purely a convenience, never required)
// ---------------------------------------------------------------------------

QStringList MainWindow::recentWorkspaces() const
{
    // Absent/corrupt settings simply yield an empty list — never an error.
    return QSettings().value(QStringLiteral("recentWorkspaces")).toStringList();
}

void MainWindow::addToRecentWorkspaces(const QString &path)
{
    if (path.isEmpty()) return;

    const QString canonical = QFileInfo(path).absoluteFilePath();

    QStringList list = recentWorkspaces();
    // Case-insensitive de-dupe so the same file can't appear twice on Windows.
    list.removeIf([&](const QString &p){
        return QString::compare(p, canonical, Qt::CaseInsensitive) == 0;
    });
    list.prepend(canonical);
    while (list.size() > k_maxRecentWorkspaces)
        list.removeLast();

    QSettings().setValue(QStringLiteral("recentWorkspaces"), list);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();

    const QStringList list = recentWorkspaces();
    if (list.isEmpty()) {
        QAction *none = m_recentMenu->addAction(tr("(No recent workspaces)"));
        none->setEnabled(false);
        return;
    }

    int n = 1;
    for (const QString &path : list) {
        const QString name = QFileInfo(path).fileName();
        QAction *act = m_recentMenu->addAction(
            tr("&%1  %2").arg(QString::number(n++), name));
        act->setData(path);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, [this, path]{
            openRecentWorkspace(path);
        });
    }

    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear recent list")),
            &QAction::triggered, this, [this]{
        QSettings().remove(QStringLiteral("recentWorkspaces"));
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentWorkspace(const QString &path)
{
    if (!maybeSave()) return;

    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Workspace Not Found"),
            tr("The workspace no longer exists:\n%1\n\n"
               "It has been removed from the recent list.").arg(path));
        QStringList list = recentWorkspaces();
        list.removeAll(path);
        QSettings().setValue(QStringLiteral("recentWorkspaces"), list);
        rebuildRecentMenu();
        return;
    }

    loadWorkspace(path);
}

QString MainWindow::defaultDialogDir() const
{
    const QStringList list = recentWorkspaces();
    return list.isEmpty() ? QString{}
                          : QFileInfo(list.first()).absolutePath();
}
