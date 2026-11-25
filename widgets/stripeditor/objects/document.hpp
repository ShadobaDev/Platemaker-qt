#ifndef STRIPEDIT_DOCUMENT_HPP
#define STRIPEDIT_DOCUMENT_HPP

#include <QColor>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

#include "composition.hpp"
#include "geometry.hpp"   // Geometry::Part
#include "objects/striplayout.hpp"   // StripLayout::Anchored

#include <platemaker/models/project_item.hpp>

namespace StripEdit {

/**
 * \brief The strip editor's objects as the project holds them, **the one place each lives**: every overlay's
 *        place, width, order, blend, whether it is shown and the page it is anchored to, and the composition
 *        of each object we draw — and the operations that change them.
 *
 * QtCore and the library's types only, so every operation is tested without a scene. An operation says
 * whether it changed anything: one that did not is not an edit, and not a history step. The scene shows what
 * this holds and changes nothing itself; the controller writes here first, shows the result, and makes each
 * change one step.
 */
class Document
{
public:
    using Overlay = Platemaker::Models::StripOverlay;

    /**
     * \brief Adopts what the project fed: @p overlays in composite order (the last drawn on top), and the
     *        compositions of those it has one for — every object we draw; a picture only once it was lettered.
     */
    void reset(std::vector<Overlay> overlays, Composition::Map compositions);

    [[nodiscard]] const std::vector<Overlay>& overlays() const { return m_overlays; }   //!< In composite order.
    [[nodiscard]] const Overlay*              overlay(const QString& uid) const;          //!< None for a uid it does not hold.

    //! The composition of object @p uid; none for a picture never lettered, or a uid it does not hold.
    [[nodiscard]] std::optional<Composition> composition(const QString& uid) const;
    //! Object @p uid is @p c now. True when that changed it; false too for a uid it does not hold.
    bool setComposition(const QString& uid, const Composition& c);
    //! Every composition it holds, keyed by uid: what the project stores beside the overlays.
    [[nodiscard]] const Composition::Map& compositions() const { return m_compositions; }

    //! Overlay @p uid placed at @p at: its fractions and its page.
    bool setPlace(const QString& uid, const StripLayout::Anchored& at);
    //! Overlay @p uid anchored to the page of input uid @p page, at the offset it had. True when that changed it.
    bool setAnchor(const QString& uid, const QString& page);
    //! The blend mode the overlays @p uids share; none when they disagree, or there are none.
    [[nodiscard]] std::optional<Platemaker::Models::BlendMode> blendOf(const QStringList& uids) const;
    //! Gives the overlays @p uids blend mode @p blend. \return The uids it changed.
    QStringList setBlend(const QStringList& uids, Platemaker::Models::BlendMode blend);
    //! Shows or hides overlay @p uid. True when that changed it.
    bool setEnabled(const QString& uid, bool on);
    //! Moves overlay @p uid one place towards the front (@p forward) or the back. False at the end of the stack.
    bool move(const QString& uid, bool forward);

    //! What setOrder() did.
    enum class Reorder {
        Refused,     //!< The order lost or gained an overlay: kept as it was.
        Unchanged,   //!< The same order.
        Done,
    };
    //! Adopts @p bottomUp — uids from the back to the front — as the composite order; ids that are not overlays drop out.
    Reorder setOrder(const QStringList& bottomUp);
    //! Removes the overlays @p uids, and their compositions.
    void remove(const QStringList& uids);

private:
    Overlay* find(const QString& uid);

    std::vector<Overlay> m_overlays;
    Composition::Map     m_compositions;   //!< Only of overlays it holds.
};

/**
 * \brief The operations on one object's composition: a value in, the value changed. Each returns whether it changed
 *        anything — one that did not is not an edit — and the controller makes a selection's one history step.
 */
namespace Edits {

//! A new style seed, never the one it had nor 0, drawn from @p random. False when @p c draws nothing from a seed.
bool reroll(Composition& c, const std::function<quint32()>& random);
/**
 * \brief Gives @p part of @p c @p colour: the fill of its words @p words (their id; 0, or none such: the first), the
 *        balloon's line, or its fill. False when @p c has no such part.
 */
bool paint(Composition& c, Geometry::Part part, const QColor& colour, int words = 0);
/**
 * \brief @p c made a balloon of @p look (@p toBalloon) or text — the balloon goes with its tails — and sized to its
 *        lettering in its new kind, both ways. False when it is that kind already.
 */
bool convert(Composition& c, bool toBalloon, const BalloonLook& look);
//! The body fitted to every words' text where it lies (Geometry::fitToBlocks()), or only body @p body when it is not 0
//! (a bubble picked). False for a picture, whose box is its own pixels.
bool fitToText(Composition& c, int body = 0);
//! Removes tails @p indexes of @p c's balloon, whichever order they come in. False when none of them is there.
bool removeTails(Composition& c, QList<int> indexes);
/**
 * \brief Adds words to @p c on the seat of its words @p from (its first when there are none such): their lettering,
 *        no text, in the seat's lower half (the top edge set at half its height); on body @p seat instead when it
 *        is not 0 (a bubble picked on its own). Returns their id; 0 when those words have no seat (a text object is
 *        its one words).
 */
int addWords(Composition& c, int from, int seat = 0);
//! Removes @p c's words @p id. False when there are no such words, or they are the last: a balloon keeps somewhere to type.
bool removeWords(Composition& c, int id);
/**
 * \brief Gives @p c a mask of its own: a plain rectangle over the right third of its frame (frameOf()), reaching past
 *        the frame's top, bottom and right so an Edge fade shows nothing back there, hiding wholly from the frame's
 *        right edge on and not at all at its left: the end of a line broken off. Returns the mask's id; 0 when
 *        @p c has a mask already, or no frame.
 */
int addMask(Composition& c);
//! Removes @p c's mask. False when it has none.
bool removeMask(Composition& c);
/**
 * \brief A stroke of the mask brush: @p area (in @p c's frame) hidden (@p hide) or shown — with Keep inside, taken
 *        from the regions or added to them. The regions it meets and the area become one or more regions, as few as
 *        lie apart, their holes filled, each a Curve fitted within @p tolerance (Bezier::fitted()); the regions it
 *        does not meet keep their nodes. The fade stays where it was on the object. An object with no mask gets one
 *        from its first stroke that hides; a mask left with no regions is removed.
 *
 * False when nothing changed: an empty area, or one that adds nothing to the regions or takes nothing from them.
 */
bool paintMask(Composition& c, const QPainterPath& area, bool hide, qreal tolerance);

}  // namespace Edits

}  // namespace StripEdit

#endif // STRIPEDIT_DOCUMENT_HPP
