#ifndef DOCKATTENTION_HPP
#define DOCKATTENTION_HPP

class QDockWidget;

/**
 * @brief Takes the user to @p dock: shows it, raises it, gives it focus — and, **only if it was not
 *        already the focused dock**, outlines it briefly so the eye finds what changed.
 *
 * Undo and redo are the reason this exists. A project has one history covering both the project dock
 * and its strip editor, so a step can land in a window the user is not looking at; without something
 * that says *here*, Ctrl+Z appears to do nothing. The condition is the whole point of the effect:
 * undoing something in front of you must not flash at you, or the flash stops meaning anything.
 *
 * The outline is a click-through child of the dock that fades itself out and deletes itself, so there
 * is nothing to keep track of and nothing to undo. Its colour comes from the palette's highlight —
 * "the colour this theme uses for *look here*" is exactly the question being asked.
 *
 * Safe with nullptr (an unopened dock is simply nowhere to go).
 */
void showDockAttention(QDockWidget* dock);

#endif // DOCKATTENTION_HPP
