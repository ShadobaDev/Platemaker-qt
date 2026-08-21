#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QStringList>

#include <exception>

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
    m_batchTimer.start(); // whole-batch wall-clock for the "Batch finished" summary

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
        tr("Batch finished: %1 rendered, %2 skipped, %3 failed (of %4) in %5.")
            .arg(done).arg(skipped).arg(failed).arg(m_batchTotal)
            .arg(humanReadableDuration(m_batchTimer.elapsed())));

    if (!m_batchFailed.isEmpty())
        ui->textBrowserActionLogs->append(
            tr("Failed: %1").arg(m_batchFailed.join(QStringLiteral(", "))));

    setProjectStatus(
        tr("Refresh finished — %1 rendered, %2 skipped, %3 failed.")
            .arg(done).arg(skipped).arg(failed));

    // Persist the whole batch transcript as one run's log (before clearing state below).
    persistRenderLog();

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

void MainWindow::reportWorkspaceRepair(
    const Platemaker::Infrastructure::WorkspaceRepairReport &report)
{
    if (!report.any())
        return;

    // Name the profiles that were given a new identifier — those are the ones the user
    // will notice reappearing. Deliberately no list of affected projects: canvas profiles
    // belong to the workspace rather than to a project, so such a list would name every
    // project that ever touched the colliding id and would still only be a suspicion.
    QStringList renamed;
    for (const auto &p : report.canvasProfiles)
        renamed << QString::fromStdString(p.name);
    for (const auto &p : report.outputProfiles)
        renamed << QString::fromStdString(p.name);

    const QString issuesUrl =
        QStringLiteral("https://github.com/ShadobaDev/PlateMaker/issues");

    QMessageBox::information(
        this, tr("Workspace repaired"),
        tr("%n profile(s) shared an internal identifier with another one, so they were "
           "given a new one:\n%1\n\n"
           "Nothing was lost — your profiles, their settings and their project "
           "assignments are unchanged. The identifier is internal bookkeeping and is never "
           "shown anywhere else.\n\n"
           "While it was shared, those profiles could not be told apart, so some projects "
           "may now show as out of sync and need a refresh. Platemaker works this out on "
           "its own: if anything genuinely no longer matches, those tiles turn amber.\n\n"
           "If this message keeps appearing when nothing has changed, please report it:\n%2",
           "", static_cast<int>(renamed.size()))
            .arg(renamed.join(QStringLiteral(", ")))
            .arg(issuesUrl));

    // Persist the repair. captureSnapshot() ran on the loaded (already repaired) workspace,
    // so without this the fix would look "saved", never reach disk, and the collision —
    // along with this dialog — would come back on the next open.
    if (!m_workspacePath.isEmpty()) {
        try {
            m_serializer.save(m_workspace, m_workspacePath.toStdString());
            captureSnapshot();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Could not save the repair"),
                tr("The workspace was repaired in memory but could not be saved:\n%1\n\n"
                   "It works for this session; the repair will simply run again next time.")
                    .arg(QString::fromUtf8(e.what())));
        }
    }
}

void MainWindow::warnIfCanvasConfigStale()
{
    if (m_workspace.projectItems.empty())
        return;

    // What the current canvas config actually changed, per project — precise where the pages carry
    // recorded dimensions (the common case), coarse only for a legacy project rendered before sizes
    // were tracked. detectCanvasConfigChange() re-matches each page against the profiles now in effect,
    // so a profile that matches no page produces nothing here and this dialog never appears for it —
    // which is the whole point: the warning is now about pages that genuinely change, not a blanket
    // "something moved" whenever the profile list grows.
    struct Affected { QString name; int pages = 0; bool coarse = false; };
    QList<Affected> affected;
    bool anyCoarse = false;
    for (const auto &project : m_workspace.projectItems) {
        const auto change = project.detectCanvasConfigChange(m_workspace.canvasProfiles());
        if (!change.any())
            continue;
        affected.append({ QString::fromStdString(project.name),
                          static_cast<int>(change.changedInputs.size()),
                          change.listChanged });
        if (change.listChanged) anyCoarse = true;
    }

    if (affected.isEmpty())
        return;

    // One line per project: an exact page count, or (legacy) an honest "needs one re-render".
    QStringList lines;
    for (const auto &a : affected) {
        lines << (a.coarse
                  ? tr("• %1 — needs one re-render to confirm").arg(a.name)
                  : tr("• %1 — %2 page(s)").arg(a.name).arg(a.pages));
    }

    QString body =
        tr("A canvas-profile change affects pages in %1 project(s):").arg(affected.size());
    body += "\n\n" + lines.join(QStringLiteral("\n")) + "\n\n";
    body += tr("These pages will re-render with the updated canvas. Nothing is broken and no work is "
               "lost — re-rendering settles it and the amber clears.");
    if (anyCoarse) {
        body += "\n\n" + tr("Projects marked \"needs one re-render\" were last rendered by an older "
                            "version that did not record page sizes. The first re-render records them, "
                            "and later canvas-profile changes are then pinpointed to the exact pages.");
    }
    body += "\n\n" + tr("Refresh all projects now?");

    const auto answer = QMessageBox::question(
        this, tr("Canvas profiles changed"),
        body, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

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
