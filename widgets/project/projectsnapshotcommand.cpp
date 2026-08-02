#include "projectsnapshotcommand.h"

#include "project.h"

#include <utility>

ProjectSnapshotCommand::ProjectSnapshotCommand(Project* project,
                                               QString before,
                                               QString after,
                                               const QString& text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{}

void ProjectSnapshotCommand::undo()
{
    m_project->applyProjectSnapshot(m_before);
}

void ProjectSnapshotCommand::redo()
{
    // QUndoStack::push() calls redo() immediately; the handler already applied the change, so the
    // first call is a no-op. Later redos (after an undo) re-apply the "after" snapshot.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_project->applyProjectSnapshot(m_after);
}
