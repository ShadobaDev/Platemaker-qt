#ifndef STRIPEDIT_EFFECTSEDITOR_HPP
#define STRIPEDIT_EFFECTSEDITOR_HPP

#include "properties/propertygroupeditor.hpp"
#include "properties/shadowrows.hpp"

class QFormLayout;

namespace StripEdit {

/**
 * @brief What is drawn about the balloon besides its outline and fill: a shadow so far.
 *
 * A group and an editor of its own, laid out by both surfaces inside the Style section, under the
 * line and the texture: one place for how the balloon is drawn. The shadow's rows are ShadowRows, the same
 * the lettering's shadow has.
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

    QFormLayout*                 m_form = nullptr;
    ShadowRows<EffectProperties> m_shadow;   //!< The tick, the numbers and the paint.

    EffectProperties m_values;
    bool m_populating = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_EFFECTSEDITOR_HPP
