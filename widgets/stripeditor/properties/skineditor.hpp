#ifndef STRIPEDIT_SKINEDITOR_HPP
#define STRIPEDIT_SKINEDITOR_HPP

#include "properties/paintrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QFormLayout;

namespace StripEdit {

/**
 * @brief The Fill section: what the balloon is filled with — its Paint, a colour or a halftone (PLAN-L
 *        B32, B33). The line round it is the Line section's.
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
    //! Pushes m_values into the controls with their signals blocked.
    void syncFromValues();

    QFormLayout*              m_form = nullptr;
    PaintRows<SkinProperties> m_fill;   //!< The fill's colour, pattern and pattern's numbers.

    SkinProperties m_values;
    bool m_populating = false;   //!< Suppresses changed() while bind() runs.
};

}  // namespace StripEdit

#endif // STRIPEDIT_SKINEDITOR_HPP
