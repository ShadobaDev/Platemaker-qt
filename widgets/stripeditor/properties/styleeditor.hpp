#ifndef STRIPEDIT_STYLEEDITOR_HPP
#define STRIPEDIT_STYLEEDITOR_HPP

#include "properties/propertygroupeditor.hpp"

#include <functional>
#include <vector>

class QComboBox;
class QFormLayout;
class QSpinBox;

namespace StripEdit {

class StretchSpin;

/**
 * @brief How the outline is drawn: the line style, how strongly a filter acts, and a flash's strokes.
 *
 * The **seed is not here**, and that is the group model earning its keep. A seed is per balloon and set
 * once at placement; a group's applyTo() writes the whole group, so a seed inside it would be copied
 * along by every preset and a page of marker balloons would wear one repeated wobble. It stays a bare
 * field on the record, written by whoever places a bubble and by nobody else.
 *
 * The flash's rows are built from StyleProperties' field list, one spin per `flash…` field, and shown
 * only while Flash is selected; the amount shows only for a style it scales.
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
    //! One `flash…` field's spin, and how to reach the field. Lengths are whole pixels on screen.
    struct FlashRow
    {
        QSpinBox*                                 spin = nullptr;
        int                                       min  = 0;
        std::function<int(const StyleProperties&)> get;
        std::function<void(StyleProperties&, int)> set;
        bool                                      mixed   = false;
        bool                                      touched = false;
    };

    void syncFromValues();
    void showRows();

    QFormLayout* m_form   = nullptr;
    QComboBox*   m_kind   = nullptr;
    QSpinBox*    m_amount = nullptr;   //!< How strongly, as a percentage of the preset.
    StretchSpin* m_stretch = nullptr;  //!< The flash's core stretch, which reads "Auto (n %)".
    std::vector<FlashRow> m_flash;

    StyleProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedKind    = false;
    bool m_mixedAmount  = false;
    bool m_kindTouched  = false;
    bool m_amountTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_STYLEEDITOR_HPP
