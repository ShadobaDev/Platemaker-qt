#ifndef PROJECTSNAPSHOTCOMMAND_H
#define PROJECTSNAPSHOTCOMMAND_H

#include <QPointer>
#include <QString>
#include <QUndoCommand>

class Project;

/**
 * @brief One undoable project-scope edit (inputs, canvas links, output-profile selection, output dir).
 *
 * Snapshot-based: it holds the project's serialized state before and after the edit (from the lib's
 * ProjectEditor::snapshot) and restores one or the other. A single command type therefore covers every
 * project-local operation. The originating handler performs the mutation and hands both snapshots to
 * the command, so the first redo() (fired by QUndoStack::push) is a deliberate no-op — the state is
 * already applied — and only a later redo (after an undo) re-applies it.
 *
 * The snapshots are compact JSON of one project (~tens of KB), not the whole workspace, so the undo
 * history stays light even with many projects open. Not a QObject (QUndoCommand isn't one).
 *
 * The target is a `QPointer`, like its overlay-scope sibling: a history lives for the session and a dock
 * does not, so a project that has actually gone leaves commands with nothing to restore. They do nothing
 * rather than reaching into freed memory.
 */
class ProjectSnapshotCommand : public QUndoCommand
{
public:
    /**
     * @param project The project this edit belongs to (owns/repaints itself on restore).
     * @param before  ProjectEditor::snapshot() taken before the edit — restored on undo().
     * @param after   ProjectEditor::snapshot() taken after the edit — restored on a redo() after undo.
     * @param text    Short operation label (e.g. "Reorder inputs").
     */
    ProjectSnapshotCommand(Project* project,
                           QString before,
                           QString after,
                           const QString& text);

    void undo() override;   //!< Restore the "before" project snapshot.
    void redo() override;   //!< No-op on the first call (push); restore the "after" snapshot thereafter.

private:
    QPointer<Project> m_project; //!< Target project; null once it has been removed.
    QString  m_before;           //!< Serialized project state before the edit.
    QString  m_after;            //!< Serialized project state after the edit.
    bool     m_firstRedo = true; //!< Swallows the redo QUndoStack::push() fires.
};

#endif // PROJECTSNAPSHOTCOMMAND_H
