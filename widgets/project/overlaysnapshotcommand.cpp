#include "overlaysnapshotcommand.hpp"

#include "project.hpp"

#include <utility>

OverlaySnapshotCommand::OverlaySnapshotCommand(Project* project, OverlayState before,
                                               OverlayState after, const QString& text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void OverlaySnapshotCommand::undo()
{
    if (m_project)
        m_project->restoreOverlayState(m_before);
}

void OverlaySnapshotCommand::redo()
{
    // QUndoStack::push() fires redo() immediately; the handler has already applied the edit, so the
    // first call is a no-op and only a redo after an undo re-applies it.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    if (m_project)
        m_project->restoreOverlayState(m_after);
}
