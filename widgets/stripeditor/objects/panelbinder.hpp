#ifndef STRIPEDIT_PANELBINDER_HPP
#define STRIPEDIT_PANELBINDER_HPP

#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>

#include <functional>
#include <optional>

#include "composition.hpp"
#include "objects/selection.hpp"

#include <platemaker/models/project_item.hpp>   // BlendMode

namespace StripEdit {

/**
 * \brief What OBJECT STATE shows for the selection, and where what it says goes back to.
 *
 * It binds the panel to the selection (bind()), says again what an edit made of it (show()), and writes the
 * panel's answer to the objects it was bound to (applyEdits()) — which are remembered when it binds, so a
 * selection that has changed since cannot make an answer land on the wrong objects. The Editor wires its
 * signals to OBJECT STATE; this class knows no panel.
 */
class PanelBinder : public QObject
{
    Q_OBJECT

public:
    //! What the binder needs from whoever owns the objects.
    struct Host
    {
        //! The composition object @p uid holds now; none when there is no such object.
        std::function<std::optional<Composition>(const QString& uid)>            composition;
        std::function<int(const ElementRef& tail)>                               tailIndex;        //!< Its position among its balloon's tails.
        std::function<std::optional<Platemaker::Models::BlendMode>()>            blend;            //!< The selection's, none when they disagree.
        std::function<std::optional<double>()>                                   picturePercent;   //!< One picture's size; none otherwise.
        //! Writes @p compositions to the objects @p uids, previewed or (@p commit) as one history step.
        std::function<void(const QStringList& uids, const QList<Composition>& compositions, bool commit)> apply;
    };

    PanelBinder(const SelectionModel& selection, Host host, QObject* parent = nullptr);

    //! Binds OBJECT STATE to the selection as it is now: one object, several, one tail, tails, a mix or nothing.
    void bind();
    //! The strip or a page is the selection: no object answers the panel.
    void unbind() { m_subjects.clear(); }
    //! An edit made @p c of the one object shown: shown again (its picked words, when they are the subject).
    void show(const Composition& c);
    //! An edit made @p all of the objects shown, in their order: shown again (one as one).
    void show(const QList<Composition>& all);
    //! A drag moved tail @p index of balloon @p c: its tail shown again.
    void showTail(const Composition& c, int index) { emit boundToTail(c, index); }
    //! One picture's size moved: OBJECT STATE's size row follows it.
    void showPictureScale() { emit pictureScaleBound(m_host.picturePercent()); }

    /**
     * \brief What OBJECT STATE just said, written to the objects it was bound to.
     *
     * The panel answers with compositions and no uids, because it was handed compositions and no uids. The
     * size and the tail tips are taken from the objects, not from the panel: they move on the canvas, under
     * the panel's feet, and belong to no property group.
     *
     * \param compositions What the panel says, in the order it was bound with.
     * \param commit       False while a control is being dragged or typed in: the objects show it, nothing is kept.
     */
    void applyEdits(const QList<Composition>& compositions, bool commit);

    //! The objects OBJECT STATE is bound to, in the order its compositions are in.
    [[nodiscard]] const QStringList& subjects() const { return m_subjects; }

signals:
    // --- for OBJECT STATE: what it is bound to now ---
    void boundToComposition(const Composition& composition);            //!< One object.
    void boundToTail(const Composition& composition, int index);   //!< One tail, of the balloon @p composition.
    void boundToWords(const Composition& composition, int id);     //!< Words @p id picked, of @p composition.
    void boundToBody(const Composition& composition, int id);      //!< Body @p id picked, of @p composition.
    void boundToMask(const Composition& composition, int id);      //!< The mask picked, its region @p id, of @p composition.
    void boundToCompositions(const QList<Composition>& compositions);   //!< Several objects, of any kinds.
    void boundToMixed(int count);   //!< Objects and tails together: nothing editable in common.
    //! Several tails and no whole object: their @p balloons, and each tail as (balloon index, tail index).
    void boundToTails(const QList<Composition>& balloons, const QList<QPair<int, int>>& tails);
    void boundToNothing();          //!< Nothing is selected.
    //! The selection's blend mode, none when they disagree; @p applies is false for a tail or nothing.
    void blendBound(std::optional<Platemaker::Models::BlendMode> blend, bool applies);
    //! One picture's scale in percent; no value for anything else.
    void pictureScaleBound(std::optional<double> percent);

private:
    const SelectionModel& m_selection;
    Host                  m_host;
    /**
     * \brief The objects OBJECT STATE is bound to, in the order its compositions are in.
     *
     * Not the same thing as the selection, and that is the point: the panel answers about what it was
     * shown, which may no longer be what is selected. Empty whenever the panel is showing something
     * that cannot be edited — nothing, a mixed set — so a stray signal writes nothing.
     */
    QStringList m_subjects;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PANELBINDER_HPP
