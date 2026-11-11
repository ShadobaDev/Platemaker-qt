#ifndef STRIPEDIT_TAILEDITOR_HPP
#define STRIPEDIT_TAILEDITOR_HPP

#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;

namespace StripEdit {

/**
 * \brief One selected tail: how wide it leaves the balloon, how much it bends, and how it is drawn — one
 *        taper, or a chain of ellipses with its own rows (TailLook).
 *
 * The property group `TailProperties` — one tail, addressed by its position in the balloon's list — with
 * the same contract as every other group, one level down: it writes that tail and nothing else, so the
 * balloon's other tails keep exactly what they were given.
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
    //! Per control: whether a set disagrees on it, or, after binding, whether it was moved.
    struct Fields
    {
        bool width = false, bend = false, style = false, circles = false, pitch = false, offset = false,
             tip = false, tipWidth = false;
    };
    void show(const TailProperties& values, const Fields& mixed);

    //! The chain's rows, shown while the style is Circles.
    void showRows();

    QFormLayout*    m_form    = nullptr;
    QSpinBox*       m_width   = nullptr;
    QSpinBox*       m_bend    = nullptr;
    QComboBox*      m_style   = nullptr;
    QSpinBox*       m_circles = nullptr;
    QDoubleSpinBox* m_pitch   = nullptr;
    QDoubleSpinBox* m_offset  = nullptr;
    QComboBox*      m_tip      = nullptr;   //!< How a Plain tail ends.
    QSpinBox*       m_tipWidth = nullptr;   //!< A Round or Cut end's width, in per cent of the base.
    int             m_index   = -1;
    bool            m_populating = false;
    Fields          m_touched;   //!< What was moved since the last binding: what a set takes.
};

}  // namespace StripEdit

#endif // STRIPEDIT_TAILEDITOR_HPP
