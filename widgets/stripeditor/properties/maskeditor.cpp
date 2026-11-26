#include "properties/maskeditor.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolButton>

#include "segmented.hpp"
#include "styles.hpp"   // Geometry::crossFades()

namespace StripEdit {

namespace {

constexpr double k_edgeWidthMaxPx = 500.0;   //!< A panel's half width, about: deeper, the edge fade is the whole mask.
constexpr int    k_pxDecimals     = 1;
//! How far in from the mask's box a new fade's handles lie, as a fraction of it: off the outline's nodes and "+".
constexpr qreal k_defaultFadeInset = 0.1;
//! The fade a mask is given when it starts fading: hidden wholly at the right, not at all at the left.
const QLineF k_defaultFade(1.0 - k_defaultFadeInset, 0.5, k_defaultFadeInset, 0.5);
//! The cross a mask is given when it starts fading by one: hidden in the middle, fading on every side towards the box.
constexpr qreal k_defaultCrossFrom = 60.0;                                  //!< Each arm's start, % of its reach.
constexpr qreal k_defaultCrossTo   = 100.0 * (1.0 - k_defaultFadeInset);    //!< Each arm's end: off the box.

//! @p p's cross started: its centre in the middle, every arm fading from k_defaultCrossFrom to k_defaultCrossTo.
void startCross(Paint& p)
{
    const Paint middle;   // its centre is the box's middle
    p.fadeCentreX = middle.fadeCentreX;
    p.fadeCentreY = middle.fadeCentreY;
    for (qreal* from : {&p.fadeLeftFrom, &p.fadeRightFrom, &p.fadeTopFrom, &p.fadeBottomFrom})
        *from = k_defaultCrossFrom;
    for (qreal* to : {&p.fadeLeftTo, &p.fadeRightTo, &p.fadeTopTo, &p.fadeBottomTo})
        *to = k_defaultCrossTo;
}
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
    m_applied = new QToolButton(this);
    m_applied->setText(tr("Applied"));
    m_applied->setToolTip(tr("Show only what the mask does: the objects below it hidden where it lies. Only on the "
                             "strip: the render applies the mask."));
    m_hued = new QToolButton(this);
    m_hued->setText(tr("Hued"));
    m_hued->setToolTip(tr("Show where the mask lies, black where it hides, half see-through. Only on the strip: the "
                          "render applies the mask."));
    m_view = segmented(this, new QButtonGroup(this), {m_applied, m_hued});
    form->addRow(tr("View:"), m_view);

    m_keep = new QCheckBox(tr("Keep inside"), this);
    m_keep->setToolTip(tr("Show only what is inside the mask, and hide the rest of the object: a balloon tucked into "
                          "a panel, the mask traced along the panel's frame."));
    form->addRow(QString(), m_keep);

    // Where it fades, then how.
    m_shape = new QComboBox(this);
    m_shape->addItem(tr("None"), int(Mask::Fade::None));
    m_shape->addItem(tr("Linear"), int(Mask::Fade::Linear));
    m_shape->addItem(tr("Rectangle"), int(Mask::Fade::Rectangle));
    m_shape->addItem(tr("Ellipse"), int(Mask::Fade::Ellipse));
    m_shape->addItem(tr("Edge"), int(Mask::Fade::Edge));
    m_shape->setToolTip(tr("Where the mask hides gradually: nowhere (it hides evenly), along a line, by a cross as a "
                           "balloon's fill fades, its corners square or on ellipses (dragged on the strip in Edit "
                           "shape), or inwards from its outline, as deep as Width: a soft or broken edge."));
    form->addRow(tr("Fade:"), m_shape);

    m_width = new QDoubleSpinBox(this);
    m_width->setRange(0.0, k_edgeWidthMaxPx);
    m_width->setDecimals(k_pxDecimals);
    m_width->setSuffix(tr(" px"));
    m_width->setToolTip(tr("How deep from the outline the mask hides wholly: shown on the outline, gradually hidden "
                           "inwards. Nothing outside the mask is hidden."));
    form->addRow(tr("Width:"), m_width);

    // Smooth, or a fill's patterns, named for what they do to a fade.
    m_transition = new QComboBox(this);
    m_transition->addItem(tr("Smooth"), int(Mask::Transition::Smooth));
    m_transition->addItem(tr("Halftone"), int(Mask::Transition::Halftone));
    m_transition->addItem(tr("Hatch"), int(Mask::Transition::Hatch));
    m_transition->addItem(tr("Cross-hatch"), int(Mask::Transition::CrossHatch));
    m_transition->addItem(tr("Zigzag"), int(Mask::Transition::Zigzag));
    m_transition->setToolTip(tr("How the mask goes from hidden to shown where it fades: smoothly; or what it leaves "
                                "breaking into dots, lines or a grid; or cut off by teeth reaching from where the "
                                "fade starts to where it ends."));
    form->addRow(tr("Transition:"), m_transition);

    m_spacing = new QDoubleSpinBox(this);
    m_spacing->setRange(k_spacingMinPx, k_spacingMaxPx);
    m_spacing->setDecimals(k_pxDecimals);
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
    connect(m_hued, &QToolButton::toggled, this, edited);
    connect(m_keep, &QCheckBox::toggled, this, edited);
    connect(m_width, &QDoubleSpinBox::valueChanged, this, edited);
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
            startCross(m_tone);
            m_crossSet = true;
        }
        edited();
    });
    showRows();
}

void MaskEditor::showRows()
{
    // A transition is how it goes where it fades: with no fade there is nowhere for it to show.
    const bool fades   = m_fadeShape != Mask::Fade::None;
    const bool pattern = fades && patterned(m_kind);
    const bool crossed = m_fadeShape == Mask::Fade::Rectangle || m_fadeShape == Mask::Fade::Ellipse;
    m_form->setRowVisible(m_width, m_fadeShape == Mask::Fade::Edge);
    m_form->setRowVisible(m_transition, fades);
    m_form->setRowVisible(m_spacing, pattern || (fades && m_kind == Mask::Transition::Zigzag));
    m_form->setRowVisible(m_angle, pattern);
    m_fadeRows.setVisible(m_form, crossed);
}

void MaskEditor::bind(const Subjects& subjects)
{
    const Composition* c     = subjects.isEmpty() ? nullptr : subjects.first();
    const Mask         shown = c && c->mask ? *c->mask : Mask{};
    m_populating = true;
    m_form->setRowVisible(m_view, c && isMaskObject(*c));   // an object's own mask is seen through its object
    (shown.hued ? m_hued : m_applied)->setChecked(true);
    m_tone = shown.tone;
    m_fade = shown.fade;
    m_fadeRows.reset();
    m_keep->setChecked(shown.keepInside);
    m_width->setValue(shown.edgeWidth);
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
        m.hued = m_hued->isChecked();
    m.keepInside = m_keep->isChecked();
    m.edgeWidth  = m_width->value();
    m.shape      = m_fadeShape;
    m.fade       = m_fade;
    // The cross's own numbers are dragged on the strip, so they are the target's, unless a cross was just started.
    if (m_crossSet)
        startCross(m.tone);
    m.transition        = m_kind;
    m.tone.spacing      = m_tone.spacing;
    m.tone.angle        = m_tone.angle;
    m_fadeRows.applyEdited(m_tone, m.tone);   // where the cross stands, and its turning over
}

}  // namespace StripEdit
