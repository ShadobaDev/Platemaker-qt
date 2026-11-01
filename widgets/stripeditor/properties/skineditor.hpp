#ifndef STRIPEDIT_SKINEDITOR_HPP
#define STRIPEDIT_SKINEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QFormLayout;
class QPushButton;
class QSpinBox;

namespace StripEdit {

/**
 * @brief The balloon's surface: fill, stroke and stroke width.
 *
 * The first property group cut out of the bubble panel, and the one that proves the contract. It is
 * small on purpose — three properties, two of them colours — so that what is being demonstrated is
 * the contract rather than the controls.
 *
 * The text's colour is not here. Editing a balloon's frame does not edit its lettering, so the two
 * swatches are the fill and the stroke; the text's colour moves with the rest of the Text group.
 */
class SkinEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit SkinEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Skin; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

    //! The values the controls currently show — what applyTo() writes.
    [[nodiscard]] const SkinProperties& values() const { return m_values; }

private:
    //! Raises the colour dialog for \p target and repaints \p swatch. A dialog choice is discrete, so
    //! it commits immediately rather than waiting on anyone's debounce timer.
    bool pickColour(QColor& target, QPushButton* swatch);

    //! Pushes m_values into the controls with their signals blocked.
    void syncFromValues();
    //! Shows the pattern's rows for a fill that has them.
    void showRows();

    QFormLayout* m_form         = nullptr;
    QPushButton* m_fillSwatch   = nullptr;
    QComboBox*   m_fillKind     = nullptr;   //!< Solid, or a pattern.
    QPushButton* m_secondSwatch = nullptr;   //!< A pattern's own colour.
    FieldRows<SkinProperties> m_tone;        //!< One spin per `tone…` field.

    SkinProperties m_values;
    bool m_populating = false;   //!< Suppresses changed() while bind() runs.

    // --- what a *set* of subjects made of this group ---
    int  m_subjects     = 0;       //!< How many objects are bound.
    bool m_mixedFill    = false;   //!< The selection disagrees about this colour, so the swatch says so.
    bool m_mixedKind    = false;
    bool m_mixedSecond  = false;
    bool m_fillTouched   = false;  //!< The artist picked since bind(), so this colour is theirs to spend.
    bool m_kindTouched   = false;
    bool m_secondTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_SKINEDITOR_HPP
