#ifndef STRIPEDIT_EFFECTSEDITOR_HPP
#define STRIPEDIT_EFFECTSEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/paintrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QCheckBox;
class QFormLayout;
class QPushButton;

namespace StripEdit {

/**
 * @brief What is drawn about the balloon besides its outline and fill: a shadow so far (PLAN-L M14).
 *
 * A group and an editor of its own, laid out by both surfaces inside the Style section (B23), under the
 * line and the texture: one place for how the balloon is drawn. The shadow's numbers are rows built from
 * EffectProperties' field list, one spin per `shadow…` number, shown while the shadow is on.
 */
class EffectsEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit EffectsEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Effects; }

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

private:
    void syncFromValues();
    void showRows();

    QFormLayout* m_form   = nullptr;
    QCheckBox*   m_shadow = nullptr;
    FieldRows<EffectProperties> m_rows;    //!< The offset and the softness.
    PaintRows<EffectProperties> m_paint;   //!< Its paint.

    EffectProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    bool m_mixedShadow   = false;
    bool m_shadowTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_EFFECTSEDITOR_HPP
