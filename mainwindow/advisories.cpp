/**
 * @file advisories.cpp
 * @brief The conditions this window can observe, and the status bar that reports them.
 *
 * Every condition here is derived from the workspace, so it is re-evaluated rather than remembered:
 * `raise()` replaces its own entry under a fixed key, and the condition going away is what withdraws
 * it. Nothing has to remember to clear anything, which is the property that keeps a badge from
 * outliving the problem it describes.
 */

#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include "advisories.hpp"
#include "advisorybar.hpp"
#include "recordpainter.hpp"
#include "editor.hpp"
#include "project.hpp"

#include <QDockWidget>
#include <QUndoGroup>
#include <QWidget>

#include <set>

namespace {

// Key prefixes. One per condition, suffixed with the project's uid — an index would move under the
// advisory the moment a lower-numbered project was removed.
const QString k_unanchoredKey   = QStringLiteral("unanchored/");
const QString k_missingFontsKey = QStringLiteral("missingFonts/");

//! The uids of this project's overlays that have no page under them, in composite order.
[[nodiscard]] QStringList unanchoredUids(const Platemaker::Models::ProjectItem& project)
{
    // Membership, not order: the strip is the input list laid out in `order`, and whether a page is in
    // it does not depend on where in it the page sits.
    std::set<std::string> pages;
    for (const auto& input : project.getInputImages())
        pages.insert(input.uid);

    QStringList out;
    for (const auto& overlay : project.getStripOverlays()) {
        // An empty anchor names no page, so it can never be placed — unanchored by construction rather
        // than by a page going missing, and the artist needs to hear about it the same way.
        if (overlay.anchorInputUid.empty() || !pages.count(overlay.anchorInputUid))
            out << QString::fromStdString(overlay.uid);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Raising and withdrawing
// ---------------------------------------------------------------------------

void MainWindow::refreshAdvisoriesFor(int projectIndex)
{
    if (!m_advisories || projectIndex < 0
        || projectIndex >= static_cast<int>(m_workspace.projectItems.size()))
        return;

    const auto& project = m_workspace.projectItems[static_cast<std::size_t>(projectIndex)];
    const QString uid   = QString::fromStdString(project.uid);
    const QString name  = QString::fromStdString(project.name);

    // Two conditions. A third candidate — a grade the render would not run — cannot arise: a neutral
    // grade is no grade and a grade that is not neutral always runs, so there is no state in between for
    // anyone to be warned about.

    // --- objects with no page under them ------------------------------------
    const QStringList stranded = unanchoredUids(project);
    if (stranded.isEmpty()) {
        m_advisories->clear(k_unanchoredKey + uid);
    } else {
        Advisory a;
        a.level      = Advisory::Level::Error;
        a.text       = tr("%n object(s) unanchored", "", stranded.size());
        a.detail     = tr("%1: %2 of this chapter's objects are anchored to a page that is not in the "
                          "strip, so a render leaves them out. Nothing is deleted — each one comes back "
                          "the moment its page does.").arg(name).arg(stranded.size());
        a.actionText = tr("Show them");
        a.projectUid = uid;
        a.action     = [this, uid, stranded] { showObjects(uid, stranded); };
        a.resolveText = tr("Delete the %n object(s)", "", stranded.size());
        a.resolve     = [this, uid] { deleteUnanchoredObjects(uid); };
        m_advisories->raise(k_unanchoredKey + uid, a);
    }

    // --- lettering whose font is not installed ------------------------------
    // The render is unaffected — the words are outlines in the file — so this is a warning about editing:
    // until the font is installed, an edit sets the words in a stand-in. The same answer the SVG writer
    // gets (Painter::fontFallback()), so the badge and the file can never disagree about what is missing.
    QStringList       families;
    const QStringList inStandIns = objectsInStandIns(project, &families);
    if (inStandIns.isEmpty()) {
        m_advisories->clear(k_missingFontsKey + uid);
    } else {
        Advisory a;
        a.level      = Advisory::Level::Warning;
        a.text       = tr("%n font(s) missing", "", families.size());
        a.detail     = tr("%1: %2 — used by %3 of this chapter's objects — not installed on this computer. "
                          "They render as they were last saved. Editing one sets its words in a stand-in "
                          "until the font is installed; the next open after that sets them back in their "
                          "own font.")
                           .arg(name, families.join(QStringLiteral(", ")))
                           .arg(inStandIns.size());
        a.actionText = tr("Show them");
        a.projectUid = uid;
        a.action     = [this, uid, inStandIns] { showObjects(uid, inStandIns); };
        m_advisories->raise(k_missingFontsKey + uid, a);
    }
}

QStringList MainWindow::objectsInStandIns(const Platemaker::Models::ProjectItem& project,
                                          QStringList*                         families) const
{
    const ObjectRecord::Map records = m_overlayRecords.records(QString::fromStdString(project.uid));
    QStringList       out;
    for (const auto& overlay : project.getStripOverlays()) {
        const auto rec = records.constFind(QString::fromStdString(overlay.uid));
        if (rec == records.constEnd() || Painter::fontFallback(*rec).isEmpty())
            continue;
        out << QString::fromStdString(overlay.uid);
        if (families && !families->contains(rec->text.family))
            *families << rec->text.family;
    }
    return out;
}

void MainWindow::refreshAllAdvisories()
{
    for (int i = 0; i < static_cast<int>(m_workspace.projectItems.size()); ++i)
        refreshAdvisoriesFor(i);
}

// ---------------------------------------------------------------------------
// The ways out that travel with them
// ---------------------------------------------------------------------------

void MainWindow::showObjects(const QString& projectUid, const QStringList& uids)
{
    const int idx = projectIndexForUid(projectUid);
    if (idx < 0)
        return;

    openStripEditorDock(idx);   // raises it when it is already open

    QDockWidget* strip = dockForStripEditor(idx);
    auto* editor = strip ? qobject_cast<StripEdit::Editor*>(strip->widget()) : nullptr;
    if (!editor)
        return;

    // Armed and then fed, the same handshake an undo uses: the objects only exist in the editor once
    // the feed builds them. An object that is not on the strip (unanchored) is not selectable in the
    // scene, so for it what this reaches is its row in the object stack, where it can be acted on.
    editor->selectAfterFeed(uids);
    refreshStripEditor(strip);
}

void MainWindow::deleteUnanchoredObjects(const QString& projectUid)
{
    const int idx = projectIndexForUid(projectUid);
    if (idx < 0)
        return;
    const QStringList stranded =
        unanchoredUids(m_workspace.projectItems[static_cast<std::size_t>(idx)]);
    if (stranded.isEmpty())
        return;

    // Through the project, so it lands on that chapter's history as one undoable step. The dock is
    // opened because the history's commands act on its widget — and an edit this large should end with
    // the artist looking at the chapter it happened to.
    openProjectDock(idx);
    if (Project* pw = projectWidget(idx))
        pw->deleteOverlays(stranded, tr("Delete %n unanchored object(s)", "", stranded.size()));
}

int MainWindow::projectIndexForUid(const QString& projectUid) const
{
    for (int i = 0; i < static_cast<int>(m_workspace.projectItems.size()); ++i)
        if (QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(i)].uid)
            == projectUid)
            return i;
    return -1;
}

bool MainWindow::isProjectOpen(int projectIndex) const
{
    // A tab behind another is not hidden — Qt only moves it off screen — so it counts as open, as its tab
    // on screen says it is.
    for (const QDockWidget* dock : {dockForProject(projectIndex), dockForStripEditor(projectIndex)})
        if (dock && !dock->isHidden())
            return true;
    return false;
}

int MainWindow::currentProjectIndex() const
{
    // Nothing is done to a project nobody can see: a closed one is never current, however recently raised.
    return isProjectOpen(m_activeProjectIndex) ? m_activeProjectIndex : -1;
}

QString MainWindow::activeProjectUid() const
{
    const int index = currentProjectIndex();
    if (index < 0)
        return {};
    return QString::fromStdString(m_workspace.projectItems[static_cast<std::size_t>(index)].uid);
}

void MainWindow::retargetActiveProject(int preferred)
{
    // Closing a window must not leave "the window raised last" naming a project nobody can see. Another
    // open project takes over; with none, nothing is current.
    m_activeProjectIndex = -1;
    if (isProjectOpen(preferred)) {
        m_activeProjectIndex = preferred;
    } else {
        for (int i = 0; i < static_cast<int>(m_workspace.projectItems.size()); ++i)
            if (isProjectOpen(i)) { m_activeProjectIndex = i; break; }
    }
    // Ctrl+Z follows: the project in front, or with none the workspace — never a project out of sight.
    m_undoGroup->setActiveStack(m_activeProjectIndex >= 0 ? historyFor(m_activeProjectIndex)
                                                          : m_workspaceUndoStack);
    refreshProjectActions();
    retargetStatusAdvisories();
}

// ---------------------------------------------------------------------------
// The status bar
// ---------------------------------------------------------------------------

void MainWindow::retargetStatusAdvisories()
{
    // The strip keeps itself in step with the registry; the one thing it cannot work out alone is which
    // chapter it is speaking about. One at a time — a bar showing every open chapter's problems at once
    // would make "3 objects unanchored" a sentence with no subject.
    if (m_statusAdvisories)
        m_statusAdvisories->setProjectUid(activeProjectUid());
}
