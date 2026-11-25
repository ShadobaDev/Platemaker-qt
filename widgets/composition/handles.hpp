#ifndef HANDLES_HPP
#define HANDLES_HPP

// The points edit mode offers to be dragged, by the element they belong to. A handle says how it is drawn
// (its glyph) and which element owns it, and carries that owner's function for its drag: the editor draws,
// hit-tests and hands drags on without knowing what any handle is for.

#include <QList>
#include <QPointF>

struct Composition;

namespace Geometry {

//! What the editor knows at a drag that the composition does not: on-screen sizes and the nodes picked.
struct HandleContext
{
    qreal      clearance = 0.0;   //!< How far in a corner's radius dot keeps from its arc, in balloon pixels.
    QList<int> picked;            //!< The nodes picked in shape editing, in pick order.
    int        words = 0;         //!< The words picked (their id): their frame's handles come first; 0 for none.
    int        body  = 0;         //!< The body whose outline is edited (its id), or the mask (its id); 0 for the first.
    int        pickedOf = 0;      //!< The element whose nodes `picked` are: a body's or words' id; 0 for none.
    bool       maskFade = false;  //!< A mask's: its fade's handles alone (shape editing's Fade), else its outline's.

    //! The node of element @p element picked last; -1 for none, or when the nodes picked are another's.
    [[nodiscard]] int lastPicked(int element) const { return picked.isEmpty() || pickedOf != element ? -1 : picked.last(); }
};

/**
 * \brief A point edit mode offers to be dragged, in the composition's coordinates.
 *
 * Each owner lists its own (shapeHandles(), fadeHandles(), textAreaHandles()) and tells them apart by
 * `part`, its own enum (ShapePart, FadePart, TextAreaPart); handlesOf() gathers them for a composition and
 * names their element and their drag.
 */
struct Handle
{
    //! How the chrome draws it.
    enum class Glyph {
        Node,         //!< A square, filled when its node is picked.
        Plus,         //!< A press adds what it stands for (a node).
        Tangent,      //!< A diamond, with a line from `origin`.
        Arrow,        //!< Turned along `axis`.
        FadeStart,    //!< A ring.
        FadeEnd,      //!< A dot, with its arm from `origin` broken round its start's ring at `start`.
        FadeCentre,   //!< A larger dot.
        Anchor,       //!< A ring with a cross: the point something is placed by.
    };

    /**
     * \brief @p next (the composition at the press) with @p handle dragged to @p to.
     * \return How far the composition's frame moved (a body grown to a node pulled past it), in its units.
     */
    using Drag = QPointF (*)(Composition& next, const Handle& handle, const QPointF& to, const HandleContext& context);
    /**
     * \brief What a press does before the drag, if anything: @p c changed and @p handle made the one the drag
     *        that follows takes (a "+" adds its node, and the drag is that node's).
     * \return Whether @p c changed.
     */
    using Press = bool (*)(Composition& c, Handle& handle);
    /**
     * \brief What a double-click on it does: @p c with what @p handle sets put back to its default (an edge made
     *        automatic again, an anchor back in the middle).
     * \return Whether @p c changed.
     */
    using Reset = bool (*)(Composition& c, const Handle& handle);

    int     part  = 0;   //!< Which of its owner's handles: the owner's own enum.
    int     index = 0;   //!< Which one of that part: a node, a side, an arm.
    QPointF at;
    QPointF axis;        //!< For an arrow, the way it points; for a fade's dot, outwards along its arm.
    QPointF origin;      //!< For a tangent, its node; for a fade's dot, the cross's centre.
    Glyph   glyph = Glyph::Node;
    QPointF start;       //!< For a fade's end, its arm's start.
    int     node    = -1;        //!< The node a press picks (a node, or one of its tangents); -1 for none.
    int     element = 0;         //!< The id of the element it belongs to (handlesOf()).
    Drag    drag    = nullptr;   //!< Its owner's (handlesOf()).
    Press   press   = nullptr;   //!< Its owner's, for a handle a press acts on (handlesOf()).
    Reset   reset   = nullptr;   //!< Its owner's, for a handle a double-click puts back (handlesOf()).
};

//! @p h's part, as its owner's enum.
template <class Part>
[[nodiscard]] constexpr Part partOf(const Handle& h)
{
    return static_cast<Part>(h.part);
}

/**
 * \brief Every handle edit mode offers on @p c: with a balloon, a body's outline and edge (HandleContext::body),
 *        then its fill's
 *        fade; then the text area of every words with a seat (a body or the picture), the picked words'
 *        (HandleContext::words) first — each with its element and its owner's drag.
 *
 * In that order on purpose: where a fade's dot sits on a node or a "+", the outline's own handle is the one
 * a press takes.
 */
[[nodiscard]] QList<Handle> handlesOf(const Composition& c, const HandleContext& context);

/**
 * \brief What a picked tail offers besides its tip: its anchor (Tail::anchor), and a bridge's far one
 *        (BalloonTail::toAnchor) after it — index 0 and 1, each dragged inside its body's box and put back in
 *        the middle by a double-click.
 * \param tail The tail's id; none for a tail that is not there.
 */
[[nodiscard]] QList<Handle> tailHandles(const Composition& c, int tail);

}  // namespace Geometry

#endif // HANDLES_HPP
