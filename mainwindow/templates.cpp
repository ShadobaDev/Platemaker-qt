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

void MainWindow::onManageTemplates()
{
    // Skip if no workspace is loaded.
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Skip if no canvas profiles exist in the workspace.
    if (m_workspace.canvasProfiles.empty()) {
        QMessageBox::information(this, tr("No Canvas Profiles"),
            tr("Create a canvas profile before generating templates."));
        return;
    }

    // Open the TemplatesDialog, passing the workspace and its directory. 
    // Connect the workspaceModified signal to mark the workspace as dirty 
    // when templates are generated or deleted.
    const QString workspaceDir = QFileInfo(m_workspacePath).absolutePath();
    TemplatesDialog dlg(m_workspace, workspaceDir, this);
    connect(&dlg, &TemplatesDialog::workspaceModified, this, [this]{ setDirty(true); });
    dlg.exec();
}

void MainWindow::onOpenTemplatesDir()
{
    // Skip if no workspace is loaded.
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Open the templates directory in the system file explorer. 
    // If the directory does not exist, inform the user.
    const QString dir = QDir(QFileInfo(m_workspacePath).absolutePath())
                            .filePath(QStringLiteral("templates"));
    if (!QDir(dir).exists()) {
        QMessageBox::information(this, tr("Templates"),
            tr("No templates folder yet — generate a template first."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
