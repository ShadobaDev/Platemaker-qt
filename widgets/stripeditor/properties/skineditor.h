#ifndef STRIPEDIT_SKINEDITOR_H
#define STRIPEDIT_SKINEDITOR_H

#include "propertygroupeditor.h"

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
    void applyTo(TextArtifact& target) const override;

    //! The values the controls currently show — what applyTo() writes.
    [[nodiscard]] const SkinProperties& values() const { return m_values; }

private:
    //! Raises the colour dialog for \p target and repaints \p swatch. A dialog choice is discrete, so
    //! it commits immediately rather than waiting on anyone's debounce timer.
    void pickColour(QColor& target, QPushButton* swatch);

    //! Pushes m_values into the controls with their signals blocked.
    void syncFromValues();

    QPushButton* m_fillSwatch   = nullptr;
    QPushButton* m_strokeSwatch = nullptr;
    QSpinBox*    m_strokeWidth  = nullptr;

    SkinProperties m_values;
    bool m_populating = false;   //!< Suppresses edited() while bind() runs.
};

}  // namespace StripEdit

#endif // STRIPEDIT_SKINEDITOR_H
