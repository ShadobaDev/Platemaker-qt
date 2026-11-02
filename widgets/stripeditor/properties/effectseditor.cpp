#include "properties/effectseditor.hpp"

#include <QFormLayout>

namespace StripEdit {

EffectsEditor::EffectsEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    m_shadow.build(this, m_form, &EffectProperties::shadow, &EffectProperties::shadowFill, tr("Shadow"),
                   tr("The balloon's whole shape, tails and all, grown, moved and filled under it."), m_values,
                   m_populating, [this] { emit changed(); }, [this] { emit committed(); });

    syncFromValues();
}

void EffectsEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = EffectProperties::from(*subjects.first());
    m_shadow.reset();
    for (const ObjectRecord* a : subjects)
        m_shadow.compare(EffectProperties::from(*a), m_values);
    syncFromValues();
}

void EffectsEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void EffectsEditor::applyEditedTo(ObjectRecord& target) const
{
    EffectProperties t = EffectProperties::from(target);
    m_shadow.applyEdited(m_values, t);
    t.applyTo(target);
}

void EffectsEditor::syncFromValues()
{
    m_populating = true;
    m_shadow.show(m_values);
    m_populating = false;
}

}  // namespace StripEdit
