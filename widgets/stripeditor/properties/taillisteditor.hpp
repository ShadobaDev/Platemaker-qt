#ifndef STRIPEDIT_TAILLISTEDITOR_HPP
#define STRIPEDIT_TAILLISTEDITOR_HPP

#include <QSize>

#include "properties/propertygroupeditor.hpp"

class QLabel;
class QPushButton;

namespace StripEdit {

/**
 * @brief An existing balloon's tails, as a collection: how many there are, and one more.
 *
 * Nothing here edits a tail. Each tail is an object of its own — selected in the object list or by its
 * handle on the strip — and its width and bend belong to `TailEditor`, one tail at a time. A control here
 * that set every tail's width at once would overwrite, on the next edit of anything, whatever a single tail
 * had been given; that is the reason this editor has no such control.
 *
 * *Add tail* seeds the new tail from the last one — its width and bend, and a tip opposite it so it does
 * not land on top. A starting value, not a link: from the moment it exists it is edited on its own.
 *
 * applyTo() **reads** the target's shape: a shapeless record has nothing for a tail to grow from.
 */
class TailListEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TailListEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Tail; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;

    /**
     * @brief The shape changed, so the default answer to "does this speak?" changed with it.
     *
     * A balloon keeps its tails through any picked shape: they are aimed at a speaker, and a new outline
     * does not move the speaker. A shape that speaks gives a tail to a balloon with none, as its tile
     * shows; one that does not adds none.
     */
    void shapeChanged(const ShapeProperties& shape);

private:
    void refresh();   //!< The count, and whether a tail can be added at all.

    QLabel*      m_count = nullptr;
    QPushButton* m_add   = nullptr;

    TailsProperties m_values;        //!< The list as bound, plus any tail added since.
    //! The balloon's size, read at bind(): a new tail is placed relative to it. Read, never written.
    QSize m_box{280, 160};
    bool  m_shapeCanSpeak = true;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TAILLISTEDITOR_HPP
