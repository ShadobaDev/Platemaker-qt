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
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Render / processing
// ---------------------------------------------------------------------------

Project *MainWindow::projectWidget(int projectIndex) const
{
    if (QDockWidget *dock = dockForProject(projectIndex))
        return qobject_cast<Project *>(dock->widget());
    return nullptr;
}

Platemaker::Models::OutputProfile MainWindow::resolveOutputProfileFor(
    const Platemaker::Models::ProjectItem &project) const
{
    for (const auto &op : m_workspace.outputProfiles)
        if (op.id == project.outputProfileId)
            return op;
    return m_workspace.outputProfiles.empty()
        ? Platemaker::Models::OutputProfile{}
        : m_workspace.outputProfiles.front();
}

void MainWindow::setActionStatus(const QString &projectName, const QString &action)
{
    ui->textBrowserActionStatus->setPlainText(projectName + ": " + action);
}

void MainWindow::setProjectStatus(const QString &message)
{
    ui->textBrowserProjectStatus->setPlainText(message);
}

void MainWindow::setProgressValue(int percent, bool error)
{
    ui->progressBar->setValue(percent);
    ui->progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar::chunk { background-color: %1; }")
        .arg(error ? QStringLiteral("#b41414") : QStringLiteral("#888888")));
}

void MainWindow::onRenderToggle(int projectIndex)
{
    if (m_rendering) {
        if (projectIndex == m_renderProjectIndex)
            cancelRender();
        return;
    }
    startRender(projectIndex);
}

void MainWindow::startRender(int projectIndex)
{
    if (m_rendering) {
        setProjectStatus(tr("A render is already running."));
        return;
    }
    if (projectIndex < 0 || projectIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    auto &project = m_workspace.projectItems[static_cast<std::size_t>(projectIndex)];
    const QString name = QString::fromStdString(project.name);

    // --- guards ---
    if (project.outputProfileId.empty()) {
        setProjectStatus(tr("Output profile is not selected."));
        return;
    }
    if (project.getOutputDirectory().empty()) {
        setProjectStatus(tr("Output directory is not selected."));
        return;
    }
    if (project.getInputImages().empty()) {
        setProjectStatus(tr("Project has no input files."));
        return;
    }

    // Refresh file statuses (hashes inputs — may briefly pause on huge projects; see TODO).
    project.sanitize();
    if (auto *pw = projectWidget(projectIndex)) pw->populate();
    if (project.isUpToDate()) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Project is up to date — nothing to render."));
        return;
    }

    const QString outDir = QString::fromStdString(project.getOutputDirectory());
    if (!QDir().mkpath(outDir)) {
        setActionStatus(name, tr("Failed"));
        setProjectStatus(tr("Cannot create output directory:\n%1").arg(outDir));
        return;
    }

    // Decide full vs partial re-render. When every input is Processed but some
    // outputs are Missing/Modified, only those slices need regenerating.
    std::unordered_set<std::string> onlySlices;
    if (project.inputsAllProcessed()) {
        const auto names = project.dirtyOutputNames();
        onlySlices.insert(names.begin(), names.end());
    }

    // --- launch the worker on its own thread, with snapshot copies ---
    m_cancelToken.reset();
    m_renderProjectIndex = projectIndex;
    m_rendering = true;

    auto *worker = new RenderWorker(
        project.getInputImages(),          // copy
        resolveOutputProfileFor(project),  // copy
        m_workspace.canvasProfiles,        // copy
        project.canvasProfileIds,          // copy
        outDir.toStdString(),
        m_cancelToken);
    if (!onlySlices.empty())
        worker->setOnlySlices(std::move(onlySlices));
    auto *thread = new QThread(this);
    worker->moveToThread(thread);
    m_renderWorker = worker;
    m_renderThread = thread;

    connect(thread, &QThread::started,         worker, &RenderWorker::process);
    connect(worker, &RenderWorker::progress,   this,   &MainWindow::onRenderProgress);
    connect(worker, &RenderWorker::log,        this,   &MainWindow::onRenderLog);
    connect(worker, &RenderWorker::sliceSaved, this,   &MainWindow::onRenderSliceSaved);
    connect(worker, &RenderWorker::finished,   this,   &MainWindow::onRenderFinished);
    connect(worker, &RenderWorker::finished,   thread, &QThread::quit);
    connect(thread, &QThread::finished,        worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,        thread, &QObject::deleteLater);

    // --- enter rendering UI state ---
    setActionStatus(name, tr("Processing"));
    setProjectStatus(tr("Rendering…"));
    ui->textBrowserActionLogs->clear();
    setProgressValue(0, false);
    ui->pushButtonStop->setEnabled(true);
    if (auto *pw = projectWidget(projectIndex)) pw->setRendering(true);

    thread->start();
}

void MainWindow::cancelRender()
{
    if (!m_rendering) return;
    m_cancelToken.cancel();
    setProjectStatus(tr("Cancelling…"));
    ui->pushButtonStop->setEnabled(false);
}

void MainWindow::onRenderProgress(int done, int total, QString sliceName)
{
    Q_UNUSED(sliceName);
    setProgressValue(total > 0 ? qRound(done * 100.0 / total) : 0, false);
}

void MainWindow::onRenderLog(int level, QString message)
{
    const char *tag = (level == 2) ? "[error] " : (level == 1) ? "[warn] " : "";
    ui->textBrowserActionLogs->append(QString::fromLatin1(tag) + message);
}

void MainWindow::onRenderSliceSaved(QString name, QString fullPath)
{
    Q_UNUSED(name);
    if (auto *pw = projectWidget(m_renderProjectIndex))
        pw->addOutputTile(fullPath);
}

void MainWindow::onRenderFinished()
{
    const auto &outcome = m_renderWorker->outcome();
    const bool partial  = m_renderWorker->isPartial();
    const int idx = m_renderProjectIndex;

    QString name;
    if (idx >= 0 && idx < static_cast<int>(m_workspace.projectItems.size())) {
        auto &project = m_workspace.projectItems[static_cast<std::size_t>(idx)];
        name = QString::fromStdString(project.name);

        if (!outcome.failed && !outcome.cancelled) {
            if (partial) {
                // Only the dirty slices were regenerated; refresh just those.
                project.applyPartialResults(outcome.records);
            } else {
                const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                project.applyProcessingResults(
                    outcome.records, project.getOutputDirectory(), ts.toStdString());
            }
            setDirty(true);
            if (auto *pw = projectWidget(idx)) pw->refreshOutputTiles();
        }
    }

    if (outcome.failed) {
        setActionStatus(name, tr("Failed"));
        setProgressValue(ui->progressBar->value(), true);   // freeze at %, recolour red
        setProjectStatus(QString::fromStdString(outcome.errorMessage));
    } else if (outcome.cancelled) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Render cancelled (partial output kept)."));
    } else if (!outcome.skippedPages.empty()) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Finished with %1 skipped page(s).")
                             .arg(outcome.skippedPages.size()));
    } else {
        setActionStatus(name, tr("Finished"));
        setProjectStatus(tr("Render finished."));
    }

    if (auto *pw = projectWidget(idx)) pw->setRendering(false);
    ui->pushButtonStop->setEnabled(false);

    m_rendering          = false;
    m_renderWorker       = nullptr;   // deleted via thread.finished → deleteLater
    m_renderThread       = nullptr;
    m_renderProjectIndex = -1;
}
