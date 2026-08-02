#include "workspacesnapshotcommand.h"

#include "mainwindow.h"

#include <utility>

WorkspaceSnapshotCommand::WorkspaceSnapshotCommand(MainWindow* window,
                                                   QString before,
                                                   QString after,
                                                   const QString& text)
    : QUndoCommand(text)
    , m_window(window)
    , m_before(std::move(before))
    , m_after(std::move(after))
{}

void WorkspaceSnapshotCommand::undo()
{
    m_window->applyWorkspaceSnapshot(m_before);
}

void WorkspaceSnapshotCommand::redo()
{
    // QUndoStack::push() calls redo() immediately; the handler already applied the change, so the
    // first call is a no-op. Later redos (after an undo) re-apply the "after" snapshot.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_window->applyWorkspaceSnapshot(m_after);
}
