#ifndef INPUTLISTCOMMAND_H
#define INPUTLISTCOMMAND_H

#include <QString>
#include <QUndoCommand>

#include "project.h"   // for Project and the nested Project::InputSnapshot

/**
 * @brief One undoable input-list edit for a Project (add / remove / clear / reorder / sort).
 *
 * Snapshot-based rather than parameter-based, so a single command type covers every input-list
 * operation uniformly: it just restores the "before" or "after" input list via
 * Project::restoreInputSnapshot(). The originating handler performs the mutation itself and hands
 * both snapshots to the command, so the first redo() (fired by QUndoStack::push) is a deliberate
 * no-op — the state is already applied — and only a later redo (after an undo) re-applies it.
 *
 * Not a QObject (QUndoCommand isn't one), so no moc / Q_OBJECT.
 */
class InputListCommand : public QUndoCommand
{
public:
    /**
     * @param project The project whose input list this edit belongs to.
     * @param before  Input snapshot to restore on undo().
     * @param after   Input snapshot to restore on a redo() that follows an undo().
     * @param text    Short operation label (e.g. "Reorder inputs"), shown by the undo/redo actions.
     */
    InputListCommand(Project* project,
                     Project::InputSnapshot before,
                     Project::InputSnapshot after,
                     const QString& text);

    void undo() override;   //!< Restore the "before" input list.
    void redo() override;   //!< No-op on the first call (push); restore the "after" list thereafter.

private:
    Project*               m_project;          //!< Target project (owned elsewhere; outlives the stack).
    Project::InputSnapshot m_before;           //!< State before the edit.
    Project::InputSnapshot m_after;            //!< State after the edit.
    bool                   m_firstRedo = true; //!< Swallows the redo QUndoStack::push() fires.
};

#endif // INPUTLISTCOMMAND_H
