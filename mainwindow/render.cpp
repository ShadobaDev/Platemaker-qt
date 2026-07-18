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
    // Skip if no workspace is loaded.
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Handle the Render/Stop button click from a Project widget. If a render is already
    // in progress, cancel it. Otherwise, start a new render for the specified project index.
    if (m_rendering) {
        if (projectIndex == m_renderProjectIndex)
            cancelRender();
        return;
    }
    // Single-project path: the "did it start?" result only matters to the batch queue.
    (void)startRender(projectIndex);
}

bool MainWindow::startRender(int projectIndex)
{
    // Every early return here means "no worker started" — the batch queue depends on
    // that distinction to move on to the next project instead of waiting for a
    // finished() signal that will never arrive.

    // Skip if no workspace is loaded.
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return false;
    }

    // Skip if a render is already running.
    if (m_rendering) {
        setProjectStatus(tr("A render is already running."));
        return false;
    }

    // Skip if the project index is out of bounds.
    if (projectIndex < 0 || projectIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return false;

    auto &project = m_workspace.projectItems[static_cast<std::size_t>(projectIndex)];
    const QString name = QString::fromStdString(project.name);

    // --- guards ---
    if (project.outputProfileId.empty()) {
        m_batchSkipReason = tr("output profile is not selected");
        setProjectStatus(tr("Output profile is not selected."));
        return false;
    }
    if (project.getOutputDirectory().empty()) {
        m_batchSkipReason = tr("output directory is not selected");
        setProjectStatus(tr("Output directory is not selected."));
        return false;
    }
    if (project.getInputImages().empty()) {
        m_batchSkipReason = tr("project has no input files");
        setProjectStatus(tr("Project has no input files."));
        return false;
    }

    // Refresh file statuses against disk and against the canvas profiles in effect
    // (hashes inputs — may briefly pause on huge projects; see TODO).
    project.sanitize(m_workspace.canvasProfiles);
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

    // Canvas-profile edits are invisible to every check above: editing margins or the
    // canvas size changes neither the input files nor the output files, and
    // outputProfileSignature() covers the output profile only. Without this, changing
    // margins left the project reporting itself up to date while its outputs were stale.
    const auto canvasChange =
        project.detectCanvasConfigChange(m_workspace.canvasProfiles);

    const bool configChanged =
        hasOutputs && (sigMismatch || formatMismatch || canvasChange.any());

    if (project.isUpToDate() && !configChanged) {
        m_batchSkipReason = tr("up to date");
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Project is up to date — nothing to render."));
        return false;
    }

    // Config change: existing outputs (possibly in another format) will be
    // replaced and any that the new configuration no longer produces will be
    // deleted. Warn before doing destructive work.
    m_renderOrphanCandidates.clear();
    m_renderOrphanDir.clear();
    if (configChanged) {
        // No breakdown of which setting moved — it does not change the decision, which is
        // only ever "regenerate or not". What *does* matter is the consequence below.
        //
        // Only an output-format change leaves files behind under names the new settings
        // never produce. A canvas edit keeps the format, so the same names are simply
        // overwritten — promising deletions there would be a lie.
        const QString consequence = formatMismatch || sigMismatch
            ? tr("All slices will be regenerated, and previous output files that the new "
                 "settings no longer produce (such as the old-format ones) will be deleted "
                 "from:\n%1")
                  .arg(QString::fromStdString(project.getOutputDirectory()))
            : tr("All slices will be regenerated in place, in:\n%1")
                  .arg(QString::fromStdString(project.getOutputDirectory()));

        // A single render asks every time; a batch asks at most once (see ConfigChangePolicy).
        bool proceed = true;
        switch (m_configChangePolicy) {
        case ConfigChangePolicy::AskPerProject: {
            const auto ret = QMessageBox::warning(
                this, tr("Settings changed since the last render"),
                tr("\"%1\" no longer matches the settings it was rendered with.\n\n%2\n\n"
                   "Continue?")
                    .arg(name, consequence),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            proceed = (ret == QMessageBox::Yes);
            break;
        }
        case ConfigChangePolicy::AskOnceForBatch: {
            // First project in the sweep that actually needs confirming — the answer
            // then stands for the rest, so the user is asked once, not once per project.
            const auto ret = QMessageBox::warning(
                this, tr("Settings changed since the last render"),
                tr("\"%1\" no longer matches the settings it was rendered with.\n\n%2\n\n"
                   "This answer applies to every project in this refresh.\n\nContinue?")
                    .arg(name, consequence),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            proceed = (ret == QMessageBox::Yes);
            m_configChangePolicy = proceed ? ConfigChangePolicy::AlreadyConfirmed
                                           : ConfigChangePolicy::DeclineAll;
            break;
        }
        case ConfigChangePolicy::AlreadyConfirmed:
            proceed = true;
            break;
        case ConfigChangePolicy::DeclineAll:
            proceed = false;
            break;
        }

        if (!proceed) {
            m_batchSkipReason = tr("configuration changed, re-render declined");
            setProjectStatus(tr("Render cancelled."));
            return false;
        }
        // Remember the old output files so we can delete the orphans on success.
        m_renderOrphanDir = QString::fromStdString(project.getOutputDirectory());
        for (const auto &of : project.getOutputImages())
            m_renderOrphanCandidates << QString::fromStdString(of.fileName);
    }

    const QString outDir = QString::fromStdString(project.getOutputDirectory());
    if (!QDir().mkpath(outDir)) {
        m_batchSkipReason = tr("cannot create output directory");
        setActionStatus(name, tr("Failed"));
        setProjectStatus(tr("Cannot create output directory:\n%1").arg(outDir));
        return false;
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
    // A batch owns the log for its whole run (cleared once in onRefreshAllProjects),
    // so clearing per project would wipe the record of everything rendered before this
    // one. A single render still starts from a clean log.
    if (m_batchTotal == 0)
        ui->textBrowserActionLogs->clear();
    else
        ui->textBrowserActionLogs->append(tr("── %1 ──").arg(name));
    setProgressValue(0, false);
    ui->pushButtonStop->setEnabled(true);
    if (auto *pw = projectWidget(projectIndex)) pw->setRendering(true);

    thread->start();
    return true;
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
    // std::as_const picks the const begin(): Qt containers are implicitly shared, and
    // iterating a non-const one calls detach(), silently deep-copying when shared.
    // Fixes warning: c++11 range-loop might detach Qt container (QStringList) [clazy-range-loop-detach]
    for (const QString &fileName : std::as_const(m_renderOrphanCandidates)) {
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

    // Copied out because the batch hook at the end runs after the render state
    // (and the worker pointer) has been reset.
    const bool renderFailed    = outcome.failed;
    const bool renderCancelled = outcome.cancelled;

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
                // appliedProfiles records which canvas profile each page was rendered
                // with, so a later edit to that profile (margins, canvas size) is
                // detectable — neither the input nor the output file changes when a
                // profile is edited, so no hash would ever notice.
                project.applyProcessingResults(
                    outcome.records, outcome.appliedProfiles,
                    m_workspace.canvasProfiles,
                    project.getOutputDirectory(), ts.toStdString());

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
            // Repopulate both lists, not just the outputs: applyProcessingResults() also
            // moves the inputs back to Processed (and clears their out-of-sync marks), so
            // refreshing only the outputs would leave the input tiles stuck amber until
            // something else happened to repopulate them.
            if (auto *pw = projectWidget(idx)) pw->populate();
        } else if (outcome.cancelled && !outcome.records.empty()) {
            // Cancelling stops the pipeline between slices, but the ones already written
            // are on disk and `records` describes them. Without this the model would keep
            // their old hashes and out-of-sync status, so finished work would look
            // untouched — and the next render would redo it.
            //
            // applyPartialResults() updates exactly those outputs and leaves the rest, so
            // the unrendered slices stay flagged. Inputs are deliberately not marked
            // Processed: the run did not finish, so the project still needs a full pass.
            project.applyPartialResults(outcome.records);
            setDirty(true);
            if (auto *pw = projectWidget(idx)) pw->populate();
        }
    }

    //--- reset render state ---
    m_renderOrphanCandidates.clear();
    m_renderOrphanDir.clear();

    // Update the UI based on the outcome of the rendering process.
    //
    // The status browsers are overwritten by whatever runs next (the following project
    // in a batch, say), so each outcome is also appended to the action log — that is the
    // only place a finished run leaves a durable record of which project ended how.
    if (outcome.failed) {
        const QString why = QString::fromStdString(outcome.errorMessage);
        setActionStatus(name, tr("Failed"));
        setProgressValue(ui->progressBar->value(), true);   // freeze at %, recolour red
        setProjectStatus(why);
        ui->textBrowserActionLogs->append(tr("FAILED %1 — %2").arg(name, why));
    } else if (outcome.cancelled) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Render cancelled (partial output kept)."));
        ui->textBrowserActionLogs->append(
            tr("Cancelled %1 (partial output kept).").arg(name));
    } else if (!outcome.skippedPages.empty()) {
        setActionStatus(name, tr("Require action"));
        setProjectStatus(tr("Finished with %1 skipped page(s).")
                             .arg(outcome.skippedPages.size()));
        ui->textBrowserActionLogs->append(
            tr("Finished %1 with %2 skipped page(s).")
                .arg(name).arg(outcome.skippedPages.size()));
    } else {
        setActionStatus(name, tr("Finished"));
        setProjectStatus(tr("Render finished."));
        ui->textBrowserActionLogs->append(tr("Finished %1.").arg(name));
    }

    // Update the UI and reset the render state.
    if (auto *pw = projectWidget(idx)) pw->setRendering(false);
    ui->pushButtonStop->setEnabled(false);

    m_rendering          = false;
    m_renderWorker       = nullptr;   // deleted via thread.finished → deleteLater
    m_renderThread       = nullptr;
    m_renderProjectIndex = -1;

    // --- batch continuation (F6) ---
    // Must come last: advanceBatch() calls startRender(), which bails out while
    // m_rendering is still true, so the reset above has to have happened first.
    if (m_batchTotal > 0) {
        if (renderCancelled) {
            // Stop cancels the whole batch, not just the current project.
            m_batchQueue.clear();
            ui->textBrowserActionLogs->append(tr("Batch cancelled."));
            finishBatch();
            return;
        }

        if (renderFailed) {
            m_batchFailed << name;
            if (!batchShouldContinueAfterFailure(name)) {
                m_batchQueue.clear();
                finishBatch();
                return;
            }
        } else {
            m_batchOk << name;
        }

        advanceBatch();
    }
}
