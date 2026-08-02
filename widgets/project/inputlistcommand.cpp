#include "inputlistcommand.h"

#include <utility>

InputListCommand::InputListCommand(Project* project,
                                   Project::InputSnapshot before,
                                   Project::InputSnapshot after,
                                   const QString& text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{}

void InputListCommand::undo()
{
    m_project->restoreInputSnapshot(m_before);
}

void InputListCommand::redo()
{
    // QUndoStack::push() calls redo() immediately; the handler already applied the change, so the
    // first call is a no-op. Later redos (after an undo) re-apply the "after" snapshot.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_project->restoreInputSnapshot(m_after);
}
