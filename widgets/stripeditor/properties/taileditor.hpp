#ifndef STRIPEDIT_TAILEDITOR_HPP
#define STRIPEDIT_TAILEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QFormLayout;
class QSpinBox;

namespace StripEdit {

/**
 * \brief One selected tail: how wide it leaves the balloon, how much it bends, and how it is drawn — its
 *        style, its tip, and their numbers (TailLook).
 *
 * The property group `TailProperties` — one tail, addressed by its position in the balloon's list — with
 * the same contract as every other group, one level down: it writes that tail and nothing else, so the
 * balloon's other tails keep exactly what they were given.
 *
 * The look's numbers are sections of TailLook (CirclesParams…), whose rows are built from their section()
 * and shown while the style, or a tipped style's tip, reads them (TailStyleInfo, TailTipInfo).
 *
 * A tail's **aim** is not here: pointing at a speaker is a drag on the strip. applyTo() therefore reads the
 * tip from the target rather than from what was bound, so moving a spin box can never undo a drag made
 * since.
 */
class TailEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TailEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::TailItem; }

    //! Which tail bind() reads and applyTo() writes. Set it before binding.
    void setIndex(int index) { m_index = index; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;

    //! Several tails at once: each control shows their common value, or *Mixed* where they disagree.
    void bindTails(const QList<TailProperties>& tails);
    //! Writes to tail @p index of @p target only what was moved since bindTails(); the rest stays its own.
    void applyEditedToTail(ObjectRecord& target, int index) const;

private:
    //! Per control outside the sections: whether a set disagrees on it, or, after binding, whether it was moved.
    struct Own
    {
        bool width = false, bend = false, style = false, tip = false;
    };
    void show(const Tail& values, const Own& mixed);

    //! The tip's row, and the sections the style and its tip read.
    void showRows();

    QFormLayout*          m_form  = nullptr;
    QSpinBox*             m_width = nullptr;
    QSpinBox*             m_bend  = nullptr;
    QComboBox*            m_style = nullptr;
    QComboBox*            m_tip   = nullptr;   //!< How a tipped style ends.
    SectionRows<TailLook> m_sections;          //!< Every style's and tip's numbers; those read shown.
    TailLook              m_look;              //!< The shown look: the sections' values, the style and the tip.
    int                   m_index      = -1;
    bool                  m_populating = false;
    Own                   m_mixed;
    Own                   m_touched;   //!< What was moved since the last binding: what a set takes.
};

}  // namespace StripEdit

#endif // STRIPEDIT_TAILEDITOR_HPP
