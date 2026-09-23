#ifndef OVERLAYSNAPSHOTCOMMAND_HPP
#define OVERLAYSNAPSHOTCOMMAND_HPP

#include <QPointer>
#include <QString>
#include <QUndoCommand>

#include "overlaystate.hpp"

class Project;

/**
 * @brief One undoable edit to a project's **text & bubbles** — and nothing else about the project.
 *
 * A **typed step**, not a second history: it shares one stack with `ProjectSnapshotCommand` and the two
 * are undone in the order they were made. What is typed is the reach — this one covers exactly
 * `stripOverlays` plus the GUI's authoring records, and its sibling restores everything else while
 * leaving these two alone. So restoring the lettering does not reload the project, restoring the project
 * does not rewind the lettering, and neither has to know what the other did.
 *
 * The two move together and are never split from each other: an overlay's record says *which* file
 * renders, its artifact says what that file contains, and restoring one without the other would leave
 * the strip showing text the render does not bake.
 *
 * Snapshot-based, like its project-scope sibling: the originating handler performs the mutation and
 * hands both ends here, so the first redo() (fired by QUndoStack::push) is a deliberate no-op.
 *
 * The target is a `QPointer` because a stack now outlives the widget that pushed onto it — histories
 * live for the session, docks come and go. A project that has actually been removed leaves commands
 * with nothing to restore, and they do nothing rather than reaching into freed memory.
 */
class OverlaySnapshotCommand : public QUndoCommand
{
public:
    /**
     * @param project The project these overlays belong to.
     * @param before  Project::overlayState() taken before the edit — restored on undo().
     * @param after   Project::overlayState() taken after the edit — restored on a later redo().
     * @param text    Short operation label (e.g. "Add bubble").
     */
    OverlaySnapshotCommand(Project* project, OverlayState before, OverlayState after,
                           const QString& text);

    void undo() override;   //!< Restore the "before" overlays and artifacts.
    void redo() override;   //!< No-op on the first call (push); restore the "after" state thereafter.

private:
    QPointer<Project> m_project;
    OverlayState      m_before;
    OverlayState      m_after;
    bool              m_firstRedo = true;
};

#endif // OVERLAYSNAPSHOTCOMMAND_HPP
