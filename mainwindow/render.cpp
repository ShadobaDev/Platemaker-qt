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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
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
    // Return the Project widget for the project at the given model index, or nullptr if not found.
    if (QDockWidget *dock = dockForProject(projectIndex))
        return qobject_cast<Project *>(dock->widget());
    return nullptr;
}

Platemaker::Models::OutputProfile MainWindow::resolveOutputProfileFor(
    const Platemaker::Models::ProjectItem &project) const
{
    // Resolve the output profile for the given project. If the project has a specific
    // output profile selected, return that. Otherwise, return the workspace's default
    // output profile. If no profiles exist, return a default-constructed OutputProfile.
    for (const auto &op : m_workspace.outputProfiles)
        if (op.id == project.outputProfileId)
            return op;
    return m_workspace.outputProfiles.empty()
        ? Platemaker::Models::OutputProfile{}
        : m_workspace.outputProfiles.front();
}

void MainWindow::setActionStatus(const QString &projectName, const QString &action)
{
    // Set the action status message in the UI for the specified project.
    // This is displayed in the Action Status text browser.
    ui->textBrowserActionStatus->setPlainText(projectName + ": " + action);
}

void MainWindow::setProjectStatus(const QString &message)
{
    // Set the project status message in the UI (e.g., "Rendering...", "Finished", etc.).
    // This is displayed in the Project Status text browser.
    ui->textBrowserProjectStatus->setPlainText(message);
}

void MainWindow::setProgressValue(int percent, bool error)
{
    // Set the progress bar value and color (red if error is true).
    // The progress bar is displayed in the UI to indicate rendering progress.
    ui->progressBar->setValue(percent);
    ui->progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar::chunk { background-color: %1; }")
        .arg(error ? QStringLiteral("#b41414") : QStringLiteral("#888888")));
}

void MainWindow::onRenderToggle(int projectIndex)
{
    // Handle the Render/Stop button click from a Project widget. If a render is already
    // in progress, cancel it. Otherwise, start a new render for the specified project index.
    if (m_rendering) {
        if (projectIndex == m_renderProjectIndex)
            cancelRender();
        return;
    }
    startRender(projectIndex);
}

void MainWindow::startRender(int projectIndex)
{
    // Skip if a render is already running.
    if (m_rendering) {
        setProjectStatus(tr("A render is already running."));
        return;
    }

    // Skip if the project index is out of bounds.
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

    // Detect an output-invalidating configuration change (e.g. PNG→JPEG, slice
    // height, quality). The stored signature was written by the last render; a
    // mismatch means every existing slice is stale → force a full re-render.
    const auto resolvedProfile = resolveOutputProfileFor(project);
    const std::string curSig = Platemaker::Models::outputProfileSignature(resolvedProfile);
    const bool hasOutputs = !project.getOutputImages().empty();
    const bool sigMismatch =
        !project.outputSignature.empty() && project.outputSignature != curSig;

    // Format changes are detectable even for projects rendered before signatures
    // existed (empty outputSignature): the recorded slice extension on disk won't
    // match the current output format.
    bool formatMismatch = false;
    if (hasOutputs) {
        const std::string wantExt =
            Platemaker::Models::outputFormatExtension(resolvedProfile.outputFormat);
        const std::string &firstName = project.getOutputImages().front().fileName;
        const auto dot = firstName.find_last_of('.');
        const std::string haveExt =
            (dot == std::string::npos) ? std::string{} : firstName.substr(dot);
        formatMismatch = !haveExt.empty() && haveExt != wantExt;
    }

    const bool configChanged = hasOutputs && (sigMismatch || formatMismatch);

    if (project.isUpToDate() && !configChanged) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Project is up to date — nothing to render."));
        return;
    }

    // Config change: existing outputs (possibly in another format) will be
    // replaced and any that the new configuration no longer produces will be
    // deleted. Warn before doing destructive work.
    m_renderOrphanCandidates.clear();
    m_renderOrphanDir.clear();
    if (configChanged) {
        const auto ret = QMessageBox::warning(
            this, tr("Output settings changed"),
            tr("The output configuration for \"%1\" changed since the last render "
               "(e.g. format or slice size).\n\n"
               "All slices will be regenerated, and previous output files that the "
               "new settings no longer produce (such as the old-format files) will "
               "be deleted from:\n%2\n\nContinue?")
                .arg(name, QString::fromStdString(project.getOutputDirectory())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) {
            setProjectStatus(tr("Render cancelled."));
            return;
        }
        // Remember the old output files so we can delete the orphans on success.
        m_renderOrphanDir = QString::fromStdString(project.getOutputDirectory());
        for (const auto &of : project.getOutputImages())
            m_renderOrphanCandidates << QString::fromStdString(of.fileName);
    }

    const QString outDir = QString::fromStdString(project.getOutputDirectory());
    if (!QDir().mkpath(outDir)) {
        setActionStatus(name, tr("Failed"));
        setProjectStatus(tr("Cannot create output directory:\n%1").arg(outDir));
        return;
    }

    // Decide full vs partial re-render. When every input is Processed but some
    // outputs are Missing/Modified, only those slices need regenerating. A config
    // change invalidates every slice, so it always forces a full render.
    std::unordered_set<std::string> onlySlices;
    if (!configChanged && project.inputsAllProcessed()) {
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
    // Skip if no render is in progress. Otherwise, signal the worker to cancel and update the UI.
    if (!m_rendering) return;
    m_cancelToken.cancel();
    setProjectStatus(tr("Cancelling…"));
    ui->pushButtonStop->setEnabled(false);
}

void MainWindow::deleteOrphanedOutputs(const Platemaker::Models::ProjectItem &project)
{
    // Names the new render produced — anything in the old set but not here is an
    // orphan to remove (e.g. old-format files after a PNG→JPEG switch).
    QSet<QString> produced;
    for (const auto &of : project.getOutputImages())
        produced.insert(QString::fromStdString(of.fileName));

    // Delete any orphaned output files that the new render no longer produces. Only
    // candidates the user confirmed via the cleanup prompt are removed.
    QDir dir(m_renderOrphanDir);
    int removed = 0;
    for (const QString &fileName : m_renderOrphanCandidates) {
        if (produced.contains(fileName)) continue;
        if (QFile::remove(dir.filePath(fileName))) {
            ++removed;
            ui->textBrowserActionLogs->append(tr("Removed stale output: %1").arg(fileName));
        }
    }
    if (removed > 0)
        ui->textBrowserActionLogs->append(
            tr("Cleaned up %1 stale output file(s) from the previous configuration.")
                .arg(removed));
}

void MainWindow::onRenderProgress(int done, int total, QString sliceName)
{
    // Update the progress bar value based on the number of slices processed so far.
    Q_UNUSED(sliceName);
    setProgressValue(total > 0 ? qRound(done * 100.0 / total) : 0, false);
}

void MainWindow::onRenderLog(int level, QString message)
{
    // Log a message from the rendering process. The log level indicates the severity of the message.
    const char *tag = (level == 2) ? "[error] " : (level == 1) ? "[warn] " : "";
    ui->textBrowserActionLogs->append(QString::fromLatin1(tag) + message);
}

void MainWindow::onRenderSliceSaved(QString name, QString fullPath)
{
    // Called when a slice has been saved during rendering. Update the corresponding Project widget with the new output tile.
    Q_UNUSED(name);
    if (auto *pw = projectWidget(m_renderProjectIndex))
        pw->addOutputTile(fullPath);
}

void MainWindow::onRenderFinished()
{
    // snapshot before m_renderWorker is nulled below
    const auto &outcome = m_renderWorker->outcome();
    const bool partial  = m_renderWorker->isPartial();
    const int idx = m_renderProjectIndex;

    // Update the UI and project state based on the outcome of the rendering process.
    QString name;
    if (idx >= 0 && idx < static_cast<int>(m_workspace.projectItems.size())) {
        auto &project = m_workspace.projectItems[static_cast<std::size_t>(idx)];
        name = QString::fromStdString(project.name);

        // Apply the processing results to the project if the render was successful and not cancelled.
        if (!outcome.failed && !outcome.cancelled) {
            if (partial) {
                // Only the dirty slices were regenerated; refresh just those.
                project.applyPartialResults(outcome.records);
            } else {
                const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                project.applyProcessingResults(
                    outcome.records, project.getOutputDirectory(), ts.toStdString());

                // Delete outputs the new configuration no longer produces (e.g. the
                // old-format files after a PNG→JPEG switch). Only candidates the user
                // confirmed via the cleanup prompt are removed.
                if (!m_renderOrphanCandidates.isEmpty())
                    deleteOrphanedOutputs(project);
            }
            // Record the configuration that produced these outputs so a later
            // format/size/quality change is detected as stale.
            project.outputSignature =
                Platemaker::Models::outputProfileSignature(resolveOutputProfileFor(project));
            setDirty(true);
            if (auto *pw = projectWidget(idx)) pw->refreshOutputTiles();
        }
    }

    //--- reset render state ---
    m_renderOrphanCandidates.clear();
    m_renderOrphanDir.clear();

    // Update the UI based on the outcome of the rendering process.
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

    // Update the UI and reset the render state.
    if (auto *pw = projectWidget(idx)) pw->setRendering(false);
    ui->pushButtonStop->setEnabled(false);

    m_rendering          = false;
    m_renderWorker       = nullptr;   // deleted via thread.finished → deleteLater
    m_renderThread       = nullptr;
    m_renderProjectIndex = -1;
}
