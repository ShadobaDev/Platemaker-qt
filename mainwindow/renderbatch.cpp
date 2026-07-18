#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>

// ---------------------------------------------------------------------------
// Batch render — "Refresh all projects" (F6)
//
// Projects are rendered one at a time, not in parallel. The scenario this exists for
// is a shared page (a title page, say) swapped into every project, and that makes each
// project a *full* re-render: a changed input leaves inputsAllProcessed() false, so the
// partial-render filter stays empty. Running N of those concurrently would multiply the
// peak memory of the heaviest phase and oversubscribe libvips' own thread pool — the
// worst possible case, not a win.
//
// Staying sequential also means the existing single-slot render state (m_cancelToken,
// m_renderProjectIndex, m_renderOrphan*) needs no changes: exactly one render is ever in
// flight, just as before. The queue only decides what runs next.
// ---------------------------------------------------------------------------

void MainWindow::onRefreshAllProjects()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }
    if (m_rendering) {
        setProjectStatus(tr("A render is already running."));
        return;
    }
    if (m_workspace.projectItems.empty()) {
        setProjectStatus(tr("Workspace has no projects."));
        return;
    }

    // Fresh batch: every project, in workspace order.
    m_batchQueue.clear();
    m_batchQueue.reserve(m_workspace.projectItems.size());
    for (int i = 0; i < static_cast<int>(m_workspace.projectItems.size()); ++i)
        m_batchQueue.push_back(i);

    m_batchTotal = static_cast<int>(m_batchQueue.size());
    m_batchOk.clear();
    m_batchSkipped.clear();
    m_batchFailed.clear();

    // The point of a sweep is to sync everything in one go, so confirm config changes
    // once for the whole run rather than per project. Unless the caller already got that
    // consent (the stale-canvas dialog), in which case don't ask at all.
    if (m_configChangePolicy != ConfigChangePolicy::AlreadyConfirmed)
        m_configChangePolicy = ConfigChangePolicy::AskOnceForBatch;

    ui->textBrowserActionLogs->clear();
    ui->textBrowserActionLogs->append(
        tr("Refreshing %1 project(s)…").arg(m_batchTotal));

    advanceBatch();
}

void MainWindow::advanceBatch()
{
    // A loop, not recursion: entire runs of projects can be skipped (e.g. all up to
    // date), and looping handles that without deep recursion or re-entering a slot.
    while (!m_batchQueue.empty()) {
        const int index = m_batchQueue.front();
        m_batchQueue.erase(m_batchQueue.begin());

        const QString name =
            (index >= 0 && index < static_cast<int>(m_workspace.projectItems.size()))
                ? QString::fromStdString(m_workspace.projectItems[
                      static_cast<std::size_t>(index)].name)
                : tr("(unknown)");

        m_batchSkipReason.clear();

        // Started → hand over; onRenderFinished() resumes the queue when it completes.
        if (startRender(index))
            return;

        // Not started: record why and try the next one.
        const QString reason = m_batchSkipReason.isEmpty()
                                   ? tr("skipped")
                                   : m_batchSkipReason;
        m_batchSkipped << tr("%1 (%2)").arg(name, reason);
        ui->textBrowserActionLogs->append(tr("Skipped %1 — %2").arg(name, reason));
    }

    finishBatch();
}

void MainWindow::finishBatch()
{
    const int done    = m_batchOk.size();
    const int skipped = m_batchSkipped.size();
    const int failed  = m_batchFailed.size();

    ui->textBrowserActionLogs->append(
        tr("Batch finished: %1 rendered, %2 skipped, %3 failed (of %4).")
            .arg(done).arg(skipped).arg(failed).arg(m_batchTotal));

    if (!m_batchFailed.isEmpty())
        ui->textBrowserActionLogs->append(
            tr("Failed: %1").arg(m_batchFailed.join(QStringLiteral(", "))));

    setProjectStatus(
        tr("Refresh finished — %1 rendered, %2 skipped, %3 failed.")
            .arg(done).arg(skipped).arg(failed));

    // Clear the batch state; m_batchTotal == 0 is what marks "no batch in flight".
    // Restoring the policy matters: a blanket confirmation belongs to this sweep only,
    // and leaving it set would let a later single render skip the destructive prompt.
    m_configChangePolicy = ConfigChangePolicy::AskPerProject;
    m_batchQueue.clear();
    m_batchTotal = 0;
    m_batchOk.clear();
    m_batchSkipped.clear();
    m_batchFailed.clear();
    m_batchSkipReason.clear();
}

void MainWindow::warnIfCanvasConfigStale()
{
    if (m_workspace.projectItems.empty())
        return;

    // Which projects can no longer be confirmed as up to date. sanitize() has already
    // turned their tiles amber; this only explains why and offers the remedy.
    //
    // Deliberately no breakdown of *what* changed: the causes (a profile edit, an app
    // update that records more than the workspace holds, an outside edit of the file)
    // all lead to the same answer — re-render — and naming them turns a simple message
    // into a technical one the user has no decision to make about.
    QStringList affected;
    for (const auto &project : m_workspace.projectItems)
        if (project.detectCanvasConfigChange(m_workspace.canvasProfiles).any())
            affected << QString::fromStdString(project.name);

    if (affected.isEmpty())
        return;

    const QString issuesUrl =
        QStringLiteral("https://github.com/ShadobaDev/PlateMaker/issues");

    const auto answer = QMessageBox::question(
        this, tr("Project state cannot be confirmed"),
        tr("Platemaker cannot tell whether these %1 project(s) still match their settings:\n"
           "%2\n\n"
           "Nothing is broken and no work is lost — the workspace is intact. This happens "
           "when something the output depends on moved on outside of a render: a profile "
           "was edited, the workspace was changed by another tool, or an application "
           "update now tracks more than this workspace recorded.\n\n"
           "The tiles may be shown amber. That does not mean the files changed — it means "
           "their state cannot be confirmed from the workspace alone. Re-rendering settles "
           "it and the colour clears.\n\n"
           "If you are confident nothing changed, this is worth reporting:\n%3\n\n"
           "Refresh all projects now?")
            .arg(affected.size())
            .arg(affected.join(QStringLiteral(", ")))
            .arg(issuesUrl),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (answer == QMessageBox::Yes) {
        // Consent given here covers the whole sweep; finishBatch() puts it back.
        m_configChangePolicy = ConfigChangePolicy::AlreadyConfirmed;
        onRefreshAllProjects();
    }
}

bool MainWindow::batchShouldContinueAfterFailure(const QString &projectName)
{
    const auto ret = QMessageBox::question(
        this, tr("Render failed"),
        tr("Rendering \"%1\" failed.\n\nContinue with the remaining project(s)?")
            .arg(projectName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    return ret == QMessageBox::Yes;
}
