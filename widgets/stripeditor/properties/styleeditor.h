#ifndef STRIPEDIT_STYLEEDITOR_H
#define STRIPEDIT_STYLEEDITOR_H

#include "propertygroupeditor.h"

class QComboBox;
class QSpinBox;

namespace StripEdit {

/**
 * @brief How the outline is drawn: Clean, Marker or Ink, and how strongly.
 *
 * The **seed is not here**, and that is the group model earning its keep. A seed is per balloon and set
 * once at placement; a group's applyTo() writes the whole group, so a seed inside it would be copied
 * along by every preset and a page of marker balloons would wear one repeated wobble. It stays a bare
 * field on the artifact, written by whoever places a bubble and by nobody else.
 */
class StyleEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit StyleEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Style; }

    void bind(const Subjects& subjects) override;
    void applyTo(TextArtifact& target) const override;
    void applyEditedTo(TextArtifact& target) const override;

    [[nodiscard]] const StyleProperties& values() const { return m_values; }

private:
    void syncFromValues();

    QComboBox* m_kind   = nullptr;
    QSpinBox*  m_amount = nullptr;   //!< How strongly, as a percentage of the preset.

    StyleProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedKind    = false;
    bool m_mixedAmount  = false;
    bool m_kindTouched  = false;
    bool m_amountTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_STYLEEDITOR_H
