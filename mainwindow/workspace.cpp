#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include "project.hpp"
#include "canvasprofiledialog.hpp"
#include "managecanvasprofilesdialog.hpp"
#include "manageoutputprofilesdialog.hpp"
#include "outputprofiledialog.hpp"
#include "templatesdialog.hpp"
#include "renderworker.hpp"
#include "workspacefolder.hpp"

#include <platemaker/models/output_profile.hpp>

#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QThread>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Workspace menu slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // Open a file dialog to select a workspace file (JSON) to load. If the user selects a file, load the workspace.
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Workspace"), defaultDialogDir(),
        tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
    if (!path.isEmpty())
        loadWorkspace(path);
}

void MainWindow::onNewWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // Prompt the user to select a file path for the new workspace — in a folder no other workspace uses.
    // If the user selects a path, create a new workspace and save it to that path.
    const QString path = askWorkspaceFile(tr("New Workspace"), defaultDialogDir());
    if (path.isEmpty()) return;

    // Create a new workspace with a default output profile and save it to the selected path.
    closeWorkspace();

    // A new workspace starts with no output profiles of its own: presets are code-defined,
    // never persisted, and always available from the catalogue (so rendering has at least one
    // profile without this file storing anything about what a preset contains). The user's own
    // profiles are added later via Manage → New / Duplicate. This matches the CLI's
    // `workspace create`, which also stores nothing until the settings diverge from a preset.
    m_workspace = Platemaker::Models::Workspace{};
    m_workspacePath = path;

    try {
        m_serializer.save(m_workspace, path.toStdString());
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot create workspace:\n%1").arg(e.what()));
        closeWorkspace();
        return;
    }

    // Update the UI to reflect the new workspace,
    // capture a snapshot for change tracking, 
    // and add the new workspace to the recent workspaces list.
    captureSnapshot();
    addToRecentWorkspaces(path);
    applyWorkspaceToUi();
}

void MainWindow::onSave()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) { onSaveAs(); return; }

    // Save the current workspace to disk. If the save operation fails, show an error message to the user.
    try {
        m_serializer.save(m_workspace, m_workspacePath.toStdString());
        // No companion file to write: a bubble's authoring parameters live inside the SVG the library
        // already references, and that was written when the edit settled.
        captureSnapshot();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot save workspace:\n%1").arg(e.what()));
    }
}

void MainWindow::onSaveAs()
{
    // Prompt the user to select a file path to save the current workspace — in a folder no other workspace
    // uses, which includes this one's own folder under a different name.
    // If the user selects a path, save the workspace to that path and add it to the recent workspaces list.
    const QString path = askWorkspaceFile(
        tr("Save Workspace As"), m_workspacePath.isEmpty() ? defaultDialogDir() : m_workspacePath);
    if (path.isEmpty()) return;

    // Save the current workspace to the selected path.
    // If the save operation fails, show an error message to the user.
    m_workspacePath = path;
    // New bubbles are written next to the workspace file, so every open project has to learn where that
    // is now. (Bitmaps already registered keep their old absolute paths — the same way input pages do;
    // a workspace-wide "collect assets" step is the general fix, and is not this round's.)
    for (QDockWidget* dock : std::as_const(m_openProjectDocks))
        if (auto* pw = qobject_cast<Project*>(dock->widget()))
            pw->setWorkspacePath(m_workspacePath);

    onSave();
    if (!isWorkspaceModified())   // save succeeded
        addToRecentWorkspaces(path);
}

void MainWindow::onCloseWorkspace()
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    closeWorkspace();
}

void MainWindow::onRevealInExplorer()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) return;

    // Open the system file explorer at the directory containing the current workspace file.
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_workspacePath).absolutePath()));
}

// ---------------------------------------------------------------------------
// One workspace per folder (see workspacefolder.hpp for why)
// ---------------------------------------------------------------------------

namespace {

//! Paints a selected row in the accent colour. The windows11 style marks a selection with a faint tint,
//! which is enough where a selection is a cursor — but here the selected row is the file that survives,
//! and the others go to the Recycle Bin, so which one it is must not be a matter of squinting. The
//! Active colours are used whatever the window's state, so it stays as clear while a message box is up.
class AccentSelectionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.brush(QPalette::Active, QPalette::Highlight));
            opt.state &= ~QStyle::State_Selected;   // or the style draws its own tint over ours
            opt.palette.setColor(QPalette::Text, opt.palette.color(QPalette::Active, QPalette::HighlightedText));
        }
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

} // namespace

QString MainWindow::askWorkspaceFile(const QString &title, QString startAt)
{
    for (;;) {
        const QString path = QFileDialog::getSaveFileName(
            this, title, startAt, tr("Platemaker Workspace (*.platemaker.json);;All files (*)"));
        if (path.isEmpty())
            return {};

        const QStringList others = otherWorkspacesBeside(path);
        if (others.isEmpty())
            return path;

        // The folder is taken. A subfolder named after the new file is the answer that needs no further
        // thought, so it is the one offered first; the name drops the ".platemaker.json" the file carries.
        const QFileInfo chosen(path);
        const QString   folderName = chosen.baseName().isEmpty() ? tr("Workspace") : chosen.baseName();

        QMessageBox box(QMessageBox::Warning, title,
                        tr("This folder already holds a workspace:\n%1\n\n"
                           "Each workspace keeps its bubbles, artwork and templates in the folder it is "
                           "saved in, so two in one folder would share — and eventually delete — each "
                           "other's files.")
                            .arg(QFileInfo(others.first()).fileName()),
                        QMessageBox::NoButton, this);
        QPushButton *createBtn = box.addButton(
            tr("Create folder \"%1\" here and save inside it").arg(folderName), QMessageBox::AcceptRole);
        QPushButton *elsewhereBtn = box.addButton(tr("Choose another location…"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(createBtn);
        box.exec();

        if (box.clickedButton() == elsewhereBtn) {
            startAt = chosen.absolutePath();
            continue;
        }
        if (box.clickedButton() != createBtn)
            return {};

        const QString folder = QDir(chosen.absolutePath()).filePath(folderName);
        if (!QDir().mkpath(folder)) {
            QMessageBox::warning(this, title, tr("Could not create the folder:\n%1").arg(folder));
            return {};
        }
        const QString inside = QDir(folder).filePath(chosen.fileName());
        if (otherWorkspacesBeside(inside).isEmpty())
            return inside;
        // That subfolder already belongs to a workspace of the same name — back to the dialog, there.
        startAt = inside;
    }
}

QString MainWindow::resolveSharedFolder(const QString &path)
{
    const QStringList all = workspacesInFolder(QFileInfo(path).absolutePath());
    if (all.size() <= 1)
        return path;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Several workspaces in one folder"));
    auto *layout = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(
        tr("This folder holds %n workspaces. Each workspace keeps its bubbles, artwork and templates in "
           "the folder it is saved in, so these share one set of files, and nothing that tidies or copies "
           "them can be safe.\n\n"
           "Choose the workspace to keep. The others are moved to the Recycle Bin, where they can be "
           "restored from — into a folder of their own.", "", static_cast<int>(all.size())),
        &dlg);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *list = new QTreeWidget(&dlg);
    list->setRootIsDecorated(false);
    list->setItemDelegate(new AccentSelectionDelegate(list));
    list->setHeaderLabels({tr("Workspace"), tr("Last saved"), tr("Projects"), tr("Objects")});
    for (const QString &ws : all) {
        auto *row = new QTreeWidgetItem(list);
        const QFileInfo fi(ws);
        row->setText(0, fi.fileName());
        row->setToolTip(0, QDir::toNativeSeparators(ws));
        row->setData(0, Qt::UserRole, ws);
        row->setText(1, QLocale().toString(fi.lastModified(), QLocale::ShortFormat));
        // What is inside is what tells two copies apart — a Drive conflict copy has the same name plus
        // " (1)" and may differ in exactly this. A file that will not load says so rather than guessing.
        try {
            const auto loaded = m_serializer.load(ws.toStdString());
            std::size_t objects = 0;
            for (const auto &project : loaded.projectItems)
                objects += project.getStripOverlays().size();
            row->setText(2, QString::number(loaded.projectItems.size()));
            row->setText(3, QString::number(objects));
        } catch (const std::exception &e) {
            row->setText(2, tr("unreadable"));
            row->setToolTip(2, QString::fromUtf8(e.what()));
        }
        if (QFileInfo(ws) == QFileInfo(path))
            list->setCurrentItem(row);
    }
    for (int c = 0; c < list->columnCount(); ++c)
        list->resizeColumnToContents(c);
    layout->addWidget(list);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Keep selected, move the others to the Recycle Bin"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QTreeWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted || !list->currentItem())
        return {};

    const QString keep = list->currentItem()->data(0, Qt::UserRole).toString();
    QStringList stuck;
    for (const QString &ws : all)
        if (QFileInfo(ws) != QFileInfo(keep) && !QFile::moveToTrash(ws))
            stuck << QDir::toNativeSeparators(ws);

    // Opening while one is left behind would be opening the folder this dialog exists to refuse.
    if (!stuck.isEmpty()) {
        QMessageBox::warning(this, tr("Several workspaces in one folder"),
                             tr("Could not move to the Recycle Bin:\n%1\n\nMove or delete it yourself, then "
                                "open the workspace again.").arg(stuck.join(QLatin1Char('\n'))));
        return {};
    }
    return keep;
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
    // Skip if the path is empty
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
    // Skip if the recent menu is not initialized
    if (!m_recentMenu) return;

    m_recentMenu->clear();

    // If there are no recent workspaces, show a disabled menu item indicating that.
    // Otherwise, populate the menu with the recent workspaces, allowing the user to open them or clear the list.
    const QStringList list = recentWorkspaces();
    if (list.isEmpty()) {
        QAction *none = m_recentMenu->addAction(tr("(No recent workspaces)"));
        none->setEnabled(false);
        return;
    }

    // Populate the recent workspaces menu with the list of recent workspaces,
    // allowing the user to open them or clear the list.
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

    // Add a separator and a "Clear recent list" action to the recent workspaces menu.
    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear recent list")),
            &QAction::triggered, this, [this]{
        QSettings().remove(QStringLiteral("recentWorkspaces"));
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentWorkspace(const QString &path)
{
    // Skip if a render is in progress
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    // Prompt to save changes if the current workspace is modified
    if (!maybeSave()) return;

    // If the selected recent workspace file does not exist, show a warning and remove it from the recent list.
    // Otherwise, load the workspace from the selected path.
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

    // Load the workspace from the selected recent workspace path.
    loadWorkspace(path);
}

QString MainWindow::defaultDialogDir() const
{
    // Return the default directory for file dialogs.
    // If there are recent workspaces, return the directory of
    // the most recent one; otherwise, return the user's home directory.
    const QStringList list = recentWorkspaces();
    return list.isEmpty() ? QString{}
                          : QFileInfo(list.first()).absolutePath();
}

QString MainWindow::workspaceCacheDir() const
{
    // The per-workspace cache folder next to the workspace file (thumbnails, render logs). Empty when
    // no workspace is loaded — callers treat that as "nowhere to cache" and skip. Single definition,
    // reused by openProjectDock (thumbnails) and persistRenderLog (logs).
    if (m_workspacePath.isEmpty()) return {};
    return QFileInfo(m_workspacePath).absolutePath() + "/.platemaker-cache";
}
