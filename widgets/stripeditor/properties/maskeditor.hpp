#ifndef STRIPEDIT_MASKEDITOR_HPP
#define STRIPEDIT_MASKEDITOR_HPP

#include "properties/faderows.hpp"
#include "properties/propertygroupeditor.hpp"

#include <QLineF>

#include <optional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QToolButton;
class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;

namespace StripEdit {

/**
 * \brief A mask, an object's own or a mask object: whether it keeps or hides, where it fades (a line, the fill's cross,
 *        FadeRows, or inwards from its outline) and how (Mask::Transition); a mask object's view.
 *
 * Its regions are the mask tools' and are edited by their nodes, and where the fade runs is dragged on the strip in
 * shape editing; this editor owns the rest of `Mask`. applyTo() writes nothing to a composition with no mask.
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
    void showRows();   //!< A fade: its transition; Edge: its width; a pattern: its cell; a cross: its turning over.

    QFormLayout*    m_form       = nullptr;
    QWidget*        m_view       = nullptr;   //!< A mask object's view, Applied or Hued; no row for an object's own mask.
    QToolButton*    m_applied    = nullptr;
    QToolButton*    m_hued       = nullptr;   //!< Mask::hued.
    QCheckBox*      m_keep       = nullptr;
    QDoubleSpinBox* m_width      = nullptr;   //!< Fade Edge's width (Mask::edgeWidth).
    QComboBox*      m_transition = nullptr;
    QComboBox*      m_shape      = nullptr;
    QDoubleSpinBox* m_spacing    = nullptr;
    QSpinBox*       m_angle      = nullptr;
    FadeRows        m_fadeRows;   //!< The fill's fade's turning over, on m_tone (the shape is m_shape).
    Mask::Fade      m_fadeShape = Mask::Fade::None;
    bool            m_crossSet  = false;   //!< A cross was just started here (picking its shape): written whole.
    Paint           m_tone;       //!< The mask's tone as bound, edited by the rows.
    Mask::Transition m_kind = Mask::Transition::Smooth;   //!< The Transition picked.
    //! The smooth fade as bound, so turning it off and on again gives back the line it had.
    std::optional<QLineF> m_fade;
    bool                  m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKEDITOR_HPP
