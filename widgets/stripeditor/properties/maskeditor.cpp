#include "properties/maskeditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>

namespace StripEdit {

namespace {

constexpr double k_featherMaxPx   = 100.0;   //!< A blur wider than this has nothing left of the region's shape.
constexpr int    k_featherDecimals = 1;
constexpr double k_featherStepPx  = 0.5;
//! The fade a mask is given when it starts fading: hidden wholly at the right, not at all at the left.
const QLineF k_defaultFade(1.0, 0.5, 0.0, 0.5);
constexpr double k_spacingMinPx = 2.0;     //!< Closer marks read as the smooth transition.
constexpr double k_spacingMaxPx = 100.0;
constexpr int    k_angleMax     = 359;

} // namespace

MaskEditor::MaskEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = m_form = new QFormLayout(this);
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

    // A fill's patterns, named for what they do to a fade.
    m_transition = new QComboBox(this);
    m_transition->addItem(tr("Smooth"), int(Paint::Kind::Solid));
    m_transition->addItem(tr("Halftone"), int(Paint::Kind::Dots));
    m_transition->addItem(tr("Hatch"), int(Paint::Kind::Lines));
    m_transition->addItem(tr("Cross-hatch"), int(Paint::Kind::Grid));
    m_transition->setToolTip(tr("How the fade goes: a gradient, or what the mask leaves breaking into dots, lines or "
                                "a grid that grow towards the fade's end."));
    form->addRow(tr("Transition:"), m_transition);

    m_spacing = new QDoubleSpinBox(this);
    m_spacing->setRange(k_spacingMinPx, k_spacingMaxPx);
    m_spacing->setDecimals(k_featherDecimals);
    m_spacing->setSuffix(tr(" px"));
    m_spacing->setToolTip(tr("From one dot or line to the next."));
    form->addRow(tr("Spacing:"), m_spacing);

    m_angle = new QSpinBox(this);
    m_angle->setRange(0, k_angleMax);
    m_angle->setWrapping(true);
    m_angle->setSuffix(QStringLiteral("°"));
    m_angle->setToolTip(tr("How far the pattern is turned."));
    form->addRow(tr("Angle:"), m_angle);

    const auto edited = [this] {
        if (m_populating)
            return;
        showRows();
        emit changed();
    };
    connect(m_transition, &QComboBox::currentIndexChanged, this, edited);
    connect(m_spacing, &QDoubleSpinBox::valueChanged, this, edited);
    connect(m_angle, &QSpinBox::valueChanged, this, edited);

    connect(m_feather, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_populating)
            emit changed();
    });
    connect(m_fades, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        if (on && !m_fade)
            m_fade = k_defaultFade;
        showRows();
        emit changed();
    });
    showRows();
}

void MaskEditor::showRows()
{
    const bool fades   = m_fades->isChecked();
    const bool pattern = m_transition->currentData().toInt() != int(Paint::Kind::Solid);
    m_form->setRowVisible(m_transition, fades);
    m_form->setRowVisible(m_spacing, fades && pattern);
    m_form->setRowVisible(m_angle, fades && pattern);
}

void MaskEditor::bind(const Subjects& subjects)
{
    const Composition* c = subjects.isEmpty() ? nullptr : subjects.first();
    const bool         has = c && c->mask;
    m_populating = true;
    m_feather->setValue(has ? c->mask->feather : 0.0);
    m_fade = has ? c->mask->fade : std::nullopt;
    m_fades->setChecked(m_fade.has_value());
    const Mask shown = has ? *c->mask : Mask{};
    m_transition->setCurrentIndex(qMax(0, m_transition->findData(int(shown.transition))));
    m_spacing->setValue(shown.spacing);
    m_angle->setValue(shown.angle);
    m_populating = false;
    showRows();
}

void MaskEditor::applyTo(Composition& target) const
{
    if (!target.mask)
        return;
    target.mask->feather = m_feather->value();
    target.mask->fade       = m_fades->isChecked() ? m_fade : std::nullopt;
    target.mask->transition = Paint::Kind(m_transition->currentData().toInt());
    target.mask->spacing    = m_spacing->value();
    target.mask->angle      = m_angle->value();
}

}  // namespace StripEdit
