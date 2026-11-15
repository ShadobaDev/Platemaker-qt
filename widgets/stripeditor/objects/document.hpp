#ifndef STRIPEDIT_DOCUMENT_HPP
#define STRIPEDIT_DOCUMENT_HPP

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

#include "objects/striplayout.hpp"   // StripLayout::Anchored

#include <platemaker/models/project_item.hpp>

namespace StripEdit {

/**
 * \brief The strip editor's objects as the project holds them: each overlay's place, width, order, blend,
 *        whether it is shown and the page it is anchored to — and the operations that change them.
 *
 * QtCore and the library's types only, so every operation is tested without a scene. An operation says
 * whether it changed anything: one that did not is not an edit, and not a history step. The scene shows what
 * this holds; the controller makes each change one step.
 */
class Document
{
public:
    using Overlay = Platemaker::Models::StripOverlay;

    //! Adopts @p overlays, in composite order (the last drawn on top): what the project fed.
    void reset(std::vector<Overlay> overlays) { m_overlays = std::move(overlays); }

    [[nodiscard]] const std::vector<Overlay>& overlays() const { return m_overlays; }   //!< In composite order.
    [[nodiscard]] const Overlay*              overlay(const QString& uid) const;          //!< None for a uid it does not hold.

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
    //! Removes the overlays @p uids.
    void remove(const QStringList& uids);

private:
    Overlay* find(const QString& uid);

    std::vector<Overlay> m_overlays;
};

}  // namespace StripEdit

#endif // STRIPEDIT_DOCUMENT_HPP
