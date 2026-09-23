#ifndef STRIPEDIT_CURSORS_HPP
#define STRIPEDIT_CURSORS_HPP

#include <QCursor>

#include "toolregistry.hpp"

namespace StripEdit {

/**
 * @brief What the pointer is over — the other half of "which cursor".
 *
 * A cursor is a function of two things, the active tool and what is under the pointer, and this is the
 * second. Three of these are **the canvas's own affordances**: every object can be moved, resized and
 * aimed under every tool, so a corner says *resize* and a tail tip says *aim* whichever tool is picked.
 * The rest is the tool's to answer.
 */
enum class PointerTarget {
    BareStrip,    //!< The pages, or the space beside them: nothing of ours is under the pointer.
    Object,       //!< A balloon, a caption or imported artwork — its body.
    Orphan,       //!< An object whose page is gone. Not on the strip, so nothing acts on it.
    ResizeFDiag,  //!< Top-left / bottom-right corner grip.
    ResizeBDiag,  //!< Top-right / bottom-left corner grip.
    TailHandle,   //!< A tail's tip.
};

//! Whether @p target belongs to the canvas rather than to the tool — a press there resizes or aims.
[[nodiscard]] bool isCanvasAffordance(PointerTarget target);

/**
 * @brief The cursor for @p tool over @p target.
 *
 * The **one** place a cursor is decided. Three writers used to disagree here: the view's drag mode, the
 * editor on every tool change, and each item on every hover — which is why the pointer flickered between
 * a hand, an arrow and a move cross depending on the order things happened in.
 *
 * Where the view's drag mode writes a cursor of its own (`ScrollHandDrag`, and the rubber band later),
 * this deliberately answers **the same** cursor, so those writes agree with this one instead of fighting
 * it.
 */
[[nodiscard]] QCursor cursorFor(const Tool& tool, PointerTarget target);

}  // namespace StripEdit

#endif // STRIPEDIT_CURSORS_HPP
