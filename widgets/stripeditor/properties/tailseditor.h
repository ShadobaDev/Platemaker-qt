#ifndef STRIPEDIT_TAILSEDITOR_H
#define STRIPEDIT_TAILSEDITOR_H

#include "propertygroupeditor.h"

class QCheckBox;
class QPushButton;
class QSpinBox;

namespace StripEdit {

/**
 * @brief Every tail on the balloon — whether there are any, how wide they leave it, how much they bend.
 *
 * A tail's **aim** is not here: it is a drag on the strip, because pointing at a speaker is a gesture
 * and not a pair of numbers. These are the two things a drag cannot say, and they apply to every tail
 * on the artifact rather than to one of them — almost every balloon has exactly one, and per-tail
 * controls would need a "current tail" to hang off. That is what makes a tail impossible to delete
 * today, and it is the thing nesting fixes: once a tail is an object it can be selected, and once it
 * can be selected it can be removed.
 *
 * applyTo() **reads** the target's shape and box before deciding what to write. Reading is not writing:
 * a shapeless artifact has nothing for a tail to grow from, and a first tail needs somewhere to point.
 */
class TailsEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TailsEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Tail; }

    void bind(const Subjects& subjects) override;
    void applyTo(TextArtifact& target) const override;

    /**
     * @brief Writes a fresh set onto an object that does not exist yet.
     *
     * One tail if tails are on, aimed where a first tail goes. The bound list belongs to the balloon
     * currently selected, and copying it onto a new one would give every placement the last balloon's
     * aim — which is the sort of thing that looks like a rendering bug for a week.
     */
    void applyToNew(TextArtifact& target) const;

    /**
     * @brief The shape changed, so the default answer to "does this speak?" changed with it.
     *
     * Picking a shape gives you the shape its tile shows — a narration box does not arrive wearing a
     * tail. The checkbox stays available for the cases that want one anyway.
     */
    void shapeChanged(TextArtifact::Shape kind);

    //! Hides "add another tail", for a surface describing an object that does not exist yet.
    void setAddVisible(bool on);

private:
    void syncEnabled();

    QCheckBox*   m_enabled = nullptr;
    QSpinBox*    m_width   = nullptr;
    QSpinBox*    m_bend    = nullptr;
    QPushButton* m_add     = nullptr;

    TailsProperties m_values;   //!< Carries the authored tips through an edit; the spins overwrite the rest.
    //! The balloon's size, read at bind(). A new tail has to be placed relative to it, and this editor
    //! has no other way to know how big the balloon is. Read, never written — the box is not its group.
    QSize m_box{280, 160};
    bool m_shapeCanSpeak = true;
    bool m_populating    = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TAILSEDITOR_H
