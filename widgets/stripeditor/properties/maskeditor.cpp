#include "properties/maskeditor.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

namespace StripEdit {

namespace {

constexpr double k_featherMaxPx   = 100.0;   //!< A blur wider than this has nothing left of the region's shape.
constexpr int    k_featherDecimals = 1;
constexpr double k_featherStepPx  = 0.5;
//! The fade a mask is given when it starts fading: hidden wholly at the right, not at all at the left.
const QLineF k_defaultFade(1.0, 0.5, 0.0, 0.5);

} // namespace

MaskEditor::MaskEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_feather = new QDoubleSpinBox(this);
    m_feather->setRange(0.0, k_featherMaxPx);
    m_feather->setDecimals(k_featherDecimals);
    m_feather->setSingleStep(k_featherStepPx);
    m_feather->setSuffix(tr(" px"));
    m_feather->setToolTip(tr("Softens the mask's outline all round, blurred to both sides of it: 0 cuts sharply. "
                             "Where the outline lies on the object's own edge, the object shows back a little; "
                             "let the mask reach past it there. Fade, not this, hides gradually across the mask. "
                             "Shown when the object is at rest."));
    form->addRow(tr("Feather:"), m_feather);

    m_fades = new QCheckBox(tr("Fade"), this);
    m_fades->setToolTip(tr("Hide gradually across the mask, along a line: wholly from its start, not at all at its "
                           "end. Drag its two ends on the strip in Edit shape."));
    form->addRow(QString(), m_fades);

    connect(m_feather, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_populating)
            emit changed();
    });
    connect(m_fades, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        if (on && !m_fade)
            m_fade = k_defaultFade;
        emit changed();
    });
}

void MaskEditor::bind(const Subjects& subjects)
{
    const Composition* c = subjects.isEmpty() ? nullptr : subjects.first();
    const bool         has = c && c->mask;
    m_populating = true;
    m_feather->setValue(has ? c->mask->feather : 0.0);
    m_fade = has ? c->mask->fade : std::nullopt;
    m_fades->setChecked(m_fade.has_value());
    m_populating = false;
}

void MaskEditor::applyTo(Composition& target) const
{
    if (!target.mask)
        return;
    target.mask->feather = m_feather->value();
    target.mask->fade    = m_fades->isChecked() ? m_fade : std::nullopt;
}

}  // namespace StripEdit
