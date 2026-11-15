#ifndef STRIPEDIT_DOCUMENT_HPP
#define STRIPEDIT_DOCUMENT_HPP

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

#include "composition.hpp"
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
    //! Overlay @p uid drawn @p wFrac of the target width. True when that moved it.
    bool setWidth(const QString& uid, double wFrac);
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

}  // namespace StripEdit

#endif // STRIPEDIT_DOCUMENT_HPP
