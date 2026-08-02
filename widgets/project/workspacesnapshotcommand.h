#ifndef WORKSPACESNAPSHOTCOMMAND_H
#define WORKSPACESNAPSHOTCOMMAND_H

#include <QString>
#include <QUndoCommand>

class MainWindow;

/**
 * @brief One undoable workspace-scope edit (profile CRUD, project rename, template gen/delete).
 *
 * Snapshot-based on the lib's WorkspaceEditor::snapshotMeta — the profile palettes plus the project
 * (uid, name) roster, *without* project contents (those live on each project's own undo stack). So the
 * snapshot is small and a workspace undo cannot resurrect project content reverted on a project stack.
 * Restore runs through MainWindow, which reinstalls the metadata and refreshes every open dock.
 *
 * As with ProjectSnapshotCommand, the handler performs the mutation and hands both snapshots here, so
 * the first redo() (fired by QUndoStack::push) is a no-op. Not a QObject.
 */
class WorkspaceSnapshotCommand : public QUndoCommand
{
public:
    /**
     * @param window The main window (applies the snapshot and refreshes the UI on restore).
     * @param before WorkspaceEditor::snapshotMeta() before the edit — restored on undo().
     * @param after  WorkspaceEditor::snapshotMeta() after the edit — restored on a redo() after undo.
     * @param text   Short operation label (e.g. "Rename project").
     */
    WorkspaceSnapshotCommand(MainWindow* window,
                             QString before,
                             QString after,
                             const QString& text);

    void undo() override;   //!< Restore the "before" workspace-metadata snapshot.
    void redo() override;   //!< No-op on the first call (push); restore the "after" snapshot thereafter.

private:
    MainWindow* m_window;         //!< Owns the workspace + all docks; applies the snapshot.
    QString     m_before;         //!< Serialized workspace metadata before the edit.
    QString     m_after;          //!< Serialized workspace metadata after the edit.
    bool        m_firstRedo = true; //!< Swallows the redo QUndoStack::push() fires.
};

#endif // WORKSPACESNAPSHOTCOMMAND_H
