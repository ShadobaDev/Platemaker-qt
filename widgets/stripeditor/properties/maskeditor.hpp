#ifndef STRIPEDIT_MASKEDITOR_HPP
#define STRIPEDIT_MASKEDITOR_HPP

#include "properties/propertygroupeditor.hpp"

#include <QLineF>

#include <optional>

class QCheckBox;
class QDoubleSpinBox;

namespace StripEdit {

/**
 * \brief An object's own mask: how soft its edge is, and whether it fades.
 *
 * Its region's shape is the Shape section's (ShapeProperties reaches the mask's region), and where the fade runs
 * is dragged on the strip in shape editing; this editor owns the rest of `Mask`. applyTo() writes nothing to a
 * composition with no mask.
 */
class MaskEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit MaskEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Mask; }

    void bind(const Subjects& subjects) override;
    void applyTo(Composition& target) const override;

private:
    QDoubleSpinBox* m_feather = nullptr;
    QCheckBox*      m_fades   = nullptr;
    //! The fade as bound, so turning it off and on again gives back the line it had.
    std::optional<QLineF> m_fade;
    bool                  m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKEDITOR_HPP
