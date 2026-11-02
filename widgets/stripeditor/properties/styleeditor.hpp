#ifndef STRIPEDIT_STYLEEDITOR_HPP
#define STRIPEDIT_STYLEEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/paintrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QComboBox;
class QFormLayout;
class QPushButton;
class QSpinBox;

namespace StripEdit {

class StretchSpin;

/**
 * @brief The Line section: the line's kind, its colour and width, and the kind's numbers (PLAN-L B32).
 *
 * The **seed is not here**, and that is the group model earning its keep. A seed is per balloon and set
 * once at placement; a group's applyTo() writes the whole group, so a seed inside it would be copied
 * along by every preset and a page of marker balloons would wear one repeated wobble. It stays a bare
 * field on the record, written by whoever places a bubble and by nobody else.
 *
 * The flash's and the brush's rows are built from StyleProperties' field list, one spin per `flash…` or
 * `brush…` field, and shown only while that line is chosen. The texture is a group and a section of its
 * own (TextureEditor).
 */
class StyleEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit StyleEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Style; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

    [[nodiscard]] const StyleProperties& values() const { return m_values; }

private:
    void syncFromValues();
    void showRows();

    QFormLayout* m_form         = nullptr;
    QComboBox*   m_line         = nullptr;
    QSpinBox*    m_strokeWidth  = nullptr;   //!< The line's width.
    PaintRows<StyleProperties> m_stroke;     //!< The line's paint.
    StretchSpin* m_stretch      = nullptr;   //!< The flash's core stretch, which reads "Auto (n %)".
    FieldRows<StyleProperties> m_flash;   //!< One spin per `flash…` field. Lengths are whole pixels.
    FieldRows<StyleProperties> m_brush;   //!< One spin per `brush…` field.
    FieldRows<StyleProperties> m_drybrush;   //!< One spin per `drybrush…` field.

    StyleProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedLine     = false;
    bool m_mixedWidth    = false;
    bool m_lineTouched   = false;
    bool m_widthTouched  = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_STYLEEDITOR_HPP
