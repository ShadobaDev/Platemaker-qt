#include "properties/maskeditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>

#include "styles.hpp"   // Geometry::crossFades()

namespace StripEdit {

namespace {

constexpr double k_featherMaxPx   = 100.0;   //!< A blur wider than this has nothing left of the region's shape.
constexpr int    k_featherDecimals = 1;
constexpr double k_featherStepPx  = 0.5;
//! The fade a mask is given when it starts fading: hidden wholly at the right, not at all at the left.
const QLineF k_defaultFade(1.0, 0.5, 0.0, 0.5);
//! The cross a mask is given when it starts fading by one: its centre on the right, the left arm fading all along.
constexpr qreal k_defaultCrossCentre = 100.0;
constexpr qreal k_defaultCrossEnd    = 100.0;
constexpr double k_spacingMinPx = 2.0;     //!< Closer marks read as the smooth transition.
constexpr double k_spacingMaxPx = 100.0;
constexpr int    k_angleMax     = 359;

//! Whether @p t breaks what the mask leaves into marks: a cell, its spacing and angle.
bool patterned(Mask::Transition t)
{
    return t == Mask::Transition::Halftone || t == Mask::Transition::Hatch || t == Mask::Transition::CrossHatch;
}

} // namespace

MaskEditor::MaskEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = m_form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    // A mask object's view on the strip (Mask::hued); the render applies it either way.
    m_view = new QComboBox(this);
    m_view->addItem(tr("Applied"));
    m_view->addItem(tr("Hued"));
    m_view->setToolTip(tr("Applied shows only what the mask does: the objects below it hidden where it lies. Hued "
                          "shows where it lies, black where it hides, half see-through. Only on the strip: the render "
                          "applies the mask."));
    form->addRow(tr("View:"), m_view);

    m_keep = new QCheckBox(tr("Keep inside"), this);
    m_keep->setToolTip(tr("Show only what is inside the mask, and hide the rest of the object: a balloon tucked into "
                          "a panel, the mask traced along the panel's frame."));
    form->addRow(QString(), m_keep);

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

    // Smooth, or a fill's patterns, named for what they do to a fade.
    m_transition = new QComboBox(this);
    m_transition->addItem(tr("Smooth"), int(Mask::Transition::Smooth));
    m_transition->addItem(tr("Halftone"), int(Mask::Transition::Halftone));
    m_transition->addItem(tr("Hatch"), int(Mask::Transition::Hatch));
    m_transition->addItem(tr("Cross-hatch"), int(Mask::Transition::CrossHatch));
    m_transition->addItem(tr("Zigzag"), int(Mask::Transition::Zigzag));
    m_transition->setToolTip(tr("How the mask hides: smoothly, along a line it fades on; or what it leaves breaking "
                                "into dots, lines or a grid, faded by a cross as a balloon's fill is; or cut off by "
                                "teeth reaching from where the fade starts to where it ends."));
    form->addRow(tr("Transition:"), m_transition);

    m_shape = new QComboBox(this);
    m_shape->addItem(tr("None"), int(Mask::Fade::None));
    m_shape->addItem(tr("Linear"), int(Mask::Fade::Linear));
    m_shape->addItem(tr("Rectangle"), int(Mask::Fade::Rectangle));
    m_shape->addItem(tr("Ellipse"), int(Mask::Fade::Ellipse));
    m_shape->setToolTip(tr("How the mask hides gradually: not at all, along a line, or by a cross as a balloon's "
                           "fill fades, its corners square or on ellipses. Dragged on the strip in Edit shape."));
    form->addRow(tr("Fade:"), m_shape);

    m_spacing = new QDoubleSpinBox(this);
    m_spacing->setRange(k_spacingMinPx, k_spacingMaxPx);
    m_spacing->setDecimals(k_featherDecimals);
    m_spacing->setSuffix(tr(" px"));
    m_spacing->setToolTip(tr("From one dot, line or tooth to the next."));
    form->addRow(tr("Spacing:"), m_spacing);

    m_angle = new QSpinBox(this);
    m_angle->setRange(0, k_angleMax);
    m_angle->setWrapping(true);
    m_angle->setSuffix(QStringLiteral("°"));
    m_angle->setToolTip(tr("How far the pattern is turned."));
    form->addRow(tr("Angle:"), m_angle);

    // The pattern's fade: the fill's cross, set on the strip in Edit shape; its shape and its turning over here.
    const auto edited = [this] {
        if (m_populating)
            return;
        showRows();
        emit changed();
    };
    m_fadeRows.build(this, form, m_tone, m_populating, edited, [this] { emit committed(); }, /*withShape=*/false);

    // As every row: changed() is what carries the edit into the object (committed() alone would commit it as it was).
    connect(m_view, &QComboBox::currentIndexChanged, this, edited);
    connect(m_keep, &QCheckBox::toggled, this, edited);
    connect(m_feather, &QDoubleSpinBox::valueChanged, this, edited);
    connect(m_transition, &QComboBox::currentIndexChanged, this, [this, edited] {
        m_kind = Mask::Transition(m_transition->currentData().toInt());
        edited();
    });
    connect(m_spacing, &QDoubleSpinBox::valueChanged, this, [this, edited](double v) {
        m_tone.spacing = v;
        edited();
    });
    connect(m_angle, &QSpinBox::valueChanged, this, [this, edited](int v) {
        m_tone.angle = v;
        edited();
    });
    connect(m_shape, &QComboBox::currentIndexChanged, this, [this, edited] {
        m_fadeShape = Mask::Fade(m_shape->currentData().toInt());
        // A shape picked for the first time starts as the end of a line broken off, as a new mask does.
        if (m_fadeShape == Mask::Fade::Linear && !m_fade)
            m_fade = k_defaultFade;
        if ((m_fadeShape == Mask::Fade::Rectangle || m_fadeShape == Mask::Fade::Ellipse)
            && !Geometry::crossFades(m_tone)) {
            m_tone.fadeCentreX  = k_defaultCrossCentre;
            m_tone.fadeLeftFrom = 0.0;
            m_tone.fadeLeftTo   = k_defaultCrossEnd;
            m_crossSet          = true;
        }
        edited();
    });
    showRows();
}

void MaskEditor::showRows()
{
    const bool pattern = patterned(m_kind);
    const bool crossed = m_fadeShape == Mask::Fade::Rectangle || m_fadeShape == Mask::Fade::Ellipse;
    m_form->setRowVisible(m_spacing, pattern || m_kind == Mask::Transition::Zigzag);
    m_form->setRowVisible(m_angle, pattern);
    m_fadeRows.setVisible(m_form, crossed);
}

void MaskEditor::bind(const Subjects& subjects)
{
    const Composition* c     = subjects.isEmpty() ? nullptr : subjects.first();
    const Mask         shown = c && c->mask ? *c->mask : Mask{};
    m_populating = true;
    m_form->setRowVisible(m_view, c && isMaskObject(*c));   // an object's own mask is seen through its object
    m_view->setCurrentIndex(shown.hued ? 1 : 0);
    m_tone = shown.tone;
    m_fade = shown.fade;
    m_fadeRows.reset();
    m_keep->setChecked(shown.keepInside);
    m_feather->setValue(shown.feather);
    m_fadeShape = shown.shape;
    m_crossSet  = false;
    m_shape->setCurrentIndex(qMax(0, m_shape->findData(int(m_fadeShape))));
    m_kind = shown.transition;
    m_transition->setCurrentIndex(qMax(0, m_transition->findData(int(m_kind))));
    m_spacing->setValue(m_tone.spacing);
    m_angle->setValue(m_tone.angle);
    m_fadeRows.show(m_tone);
    m_populating = false;
    showRows();
}

void MaskEditor::applyTo(Composition& target) const
{
    if (!target.mask)
        return;
    Mask& m      = *target.mask;
    if (isMaskObject(target))
        m.hued = m_view->currentIndex() == 1;
    m.keepInside = m_keep->isChecked();
    m.feather    = m_feather->value();
    m.shape      = m_fadeShape;
    m.fade       = m_fade;
    // The cross's own numbers are dragged on the strip, so they are the target's, unless a cross was just started.
    if (m_crossSet) {
        m.tone.fadeCentreX  = m_tone.fadeCentreX;
        m.tone.fadeLeftFrom = m_tone.fadeLeftFrom;
        m.tone.fadeLeftTo   = m_tone.fadeLeftTo;
    }
    m.transition        = m_kind;
    m.tone.spacing      = m_tone.spacing;
    m.tone.angle        = m_tone.angle;
    m_fadeRows.applyEdited(m_tone, m.tone);   // where the cross stands, and its turning over
}

}  // namespace StripEdit
