#include "projectsnapshotcommand.hpp"

#include "project.hpp"

#include <utility>

ProjectSnapshotCommand::ProjectSnapshotCommand(Project* project,
                                               QString before,
                                               QString after,
                                               const QString& text,
                                               EditScope scope)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_scope(scope)
{}

void ProjectSnapshotCommand::undo()
{
    if (m_project)
        m_project->applyProjectSnapshot(m_before, m_scope);
}

void ProjectSnapshotCommand::redo()
{
    // QUndoStack::push() calls redo() immediately; the handler already applied the change, so the
    // first call is a no-op. Later redos (after an undo) re-apply the "after" snapshot.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    if (m_project)
        m_project->applyProjectSnapshot(m_after, m_scope);
}
