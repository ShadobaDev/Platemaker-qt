#include "properties/effectseditor.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QPushButton>

namespace StripEdit {

namespace {

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::EffectsEditor";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::EffectsEditor", s)
//! The shadow's numbers, in strip pixels.
const FieldRowSpec k_shadowRows[] = {
    {"shadowSpread",   PM_TR("Spread:"),        0,    50,  " px",
     PM_TR("How far the shadow grows past the balloon on every side; with no offset, a rim all round."), 1},
    {"shadowX",        PM_TR("Offset across:"), -100, 100, " px", PM_TR("How far right the shadow falls; negative is left."), 1},
    {"shadowY",        PM_TR("Offset down:"),   -100, 100, " px", PM_TR("How far down the shadow falls; negative is up."), 1},
    {"shadowSoftness", PM_TR("Softness:"),      0,    50,  " px",
     PM_TR("How blurred its edge is. 0 is a hard shadow, drawn as you edit; above it the library draws the "
           "balloon, exactly as it will be baked. A halftone shadow fades instead: its dots or lines thin out "
           "over this width, drawn as you edit."), 1},
};
#undef PM_TR

} // namespace

EffectsEditor::EffectsEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    m_shadow = new QCheckBox(tr("Drop a shadow"), this);
    m_shadow->setToolTip(tr("The balloon's whole shape, tails and all, grown, moved and filled under it."));
    m_form->addRow(tr("Shadow"), m_shadow);
    m_rows.build(this, m_form, "shadow", k_shadowRows, k_trContext, m_values, m_populating, [this] { emit changed(); });
    // Its paint, whose transparency is how dark it is; or a halftone of it, with no ground under.
    m_paint.build(this, m_form, &EffectProperties::shadowFill, tr("Shadow colour"), m_values, m_populating,
                  [this] { emit changed(); }, [this] { emit committed(); }, false);

    connect(m_shadow, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_shadow->setTristate(false);   // they have taken a position
        m_values.shadow = on;
        m_shadowTouched = true;
        m_mixedShadow   = false;
        showRows();
        emit changed();
        emit committed();   // a tick is discrete, like a dialog's choice
    });

    syncFromValues();
}

void EffectsEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values      = EffectProperties::from(*subjects.first());
    m_mixedShadow = false;
    m_rows.reset();
    m_paint.reset();
    for (const ObjectRecord* a : subjects) {
        const EffectProperties e = EffectProperties::from(*a);
        m_mixedShadow = m_mixedShadow || e.shadow != m_values.shadow;
        m_rows.compare(e, m_values);
        m_paint.compare(e, m_values);
    }
    m_shadowTouched = false;
    syncFromValues();
}

void EffectsEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void EffectsEditor::applyEditedTo(ObjectRecord& target) const
{
    EffectProperties t = EffectProperties::from(target);
    if (m_shadowTouched) t.shadow       = m_values.shadow;
    m_rows.applyEdited(m_values, t);
    m_paint.applyEdited(m_values, t);
    t.applyTo(target);
}

void EffectsEditor::syncFromValues()
{
    m_populating = true;
    showCheck(m_shadow, m_mixedShadow, m_values.shadow);
    m_rows.show(m_values);
    m_paint.show(m_values);
    showRows();
    m_populating = false;
}

void EffectsEditor::showRows()
{
    // A selection that disagrees shows the rows at least one of them has.
    const bool on = m_mixedShadow || m_values.shadow;
    m_rows.setVisible(m_form, on);
    m_paint.setVisible(on);
}

}  // namespace StripEdit
