#ifndef STRIPEDIT_TEXTUREEDITOR_HPP
#define STRIPEDIT_TEXTUREEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QFormLayout;

namespace StripEdit {

/**
 * @brief What is done to the balloon as it is rendered: its texture, and the texture's numbers.
 *
 * Its own section of the panel (PLAN-L B32), after the line: a texture acts on the whole balloon, line
 * and shadow, never on the words. Rough's rows are built from TextureProperties' field list, one spin per
 * `rough…` field, and shown only while Rough is chosen.
 */
class TextureEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TextureEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Texture; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

private:
    void syncFromValues();
    void showRows();

    QFormLayout* m_form = nullptr;
    QComboBox*   m_kind = nullptr;
    FieldRows<TextureProperties> m_rough;   //!< One spin per `rough…` field.

    TextureProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedKind   = false;
    bool m_kindTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TEXTUREEDITOR_HPP
