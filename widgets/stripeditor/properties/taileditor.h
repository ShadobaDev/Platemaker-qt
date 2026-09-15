#ifndef STRIPEDIT_TAILEDITOR_H
#define STRIPEDIT_TAILEDITOR_H

#include "propertygroupeditor.h"

class QSpinBox;

namespace StripEdit {

/**
 * @brief One selected tail: how wide it leaves the balloon, and how much it bends.
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
    void applyTo(TextArtifact& target) const override;

private:
    QSpinBox* m_width = nullptr;
    QSpinBox* m_bend  = nullptr;
    int       m_index = -1;
    bool      m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TAILEDITOR_H
