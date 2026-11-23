#ifndef STRIPEDIT_MASKEDITOR_HPP
#define STRIPEDIT_MASKEDITOR_HPP

#include "properties/faderows.hpp"
#include "properties/propertygroupeditor.hpp"

#include <QLineF>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;

namespace StripEdit {

/**
 * \brief An object's own mask: whether it keeps or hides, how soft its edge is, and how it fades: smoothly along a
 *        line, or through a pattern faded by the fill's cross (FadeRows).
 *
 * Its region's shape is the Shape section's while it has one (ShapeProperties reaches it by the mask's id), and
 * where the fade runs is dragged on the strip in shape editing; this editor owns the rest of `Mask`. applyTo()
 * writes nothing to a composition with no mask.
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
    void showRows();   //!< A pattern: its cell; a cross: its turning over.

    QFormLayout*    m_form       = nullptr;
    QCheckBox*      m_keep       = nullptr;
    QDoubleSpinBox* m_feather    = nullptr;
    QComboBox*      m_transition = nullptr;
    QComboBox*      m_shape      = nullptr;
    QDoubleSpinBox* m_spacing    = nullptr;
    QSpinBox*       m_angle      = nullptr;
    FadeRows        m_fadeRows;   //!< The fill's fade's turning over, on m_tone (the shape is m_shape).
    Mask::Fade      m_fadeShape = Mask::Fade::None;
    bool            m_crossSet  = false;   //!< A cross was just started here (picking its shape): written whole.
    Paint           m_tone;       //!< The mask's tone as bound, edited by the rows.
    //! The smooth fade as bound, so turning it off and on again gives back the line it had.
    std::optional<QLineF> m_fade;
    bool                  m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKEDITOR_HPP
