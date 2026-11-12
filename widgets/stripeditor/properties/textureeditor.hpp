#ifndef STRIPEDIT_TEXTUREEDITOR_HPP
#define STRIPEDIT_TEXTUREEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QFormLayout;

namespace StripEdit {

/**
 * \brief What is done to the balloon as it is rendered: its texture, and the texture's numbers.
 *
 * Its own section of the panel, after the line: a texture acts on the whole balloon, line
 * and shadow, never on the words. Each texture's numbers are a section of TextureProperties (RoughParams),
 * whose rows are built from its section() and shown while a texture that reads it is chosen
 * (TextureInfo::sections).
 */
class TextureEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TextureEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Texture; }
    [[nodiscard]] QList<Row> rows() const override { return {Row::Texture}; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

private:
    void syncFromValues();
    void showRows();

    QFormLayout* m_form = nullptr;
    QComboBox*   m_kind = nullptr;
    SectionRows<TextureProperties> m_sections;   //!< Every texture's numbers; those of the one chosen shown.

    TextureProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedKind   = false;
    bool m_kindTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TEXTUREEDITOR_HPP
