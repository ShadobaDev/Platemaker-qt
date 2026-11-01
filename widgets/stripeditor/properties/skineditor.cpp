#include "properties/skineditor.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

namespace StripEdit {

namespace {

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::SkinEditor";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::SkinEditor", s)
//! A screentone's numbers, in strip pixels and degrees.
const FieldRowSpec k_toneRows[] = {
    {"toneSpacing", PM_TR("Spacing:"),  1,   100, " px", PM_TR("From one dot to the next."), 1},
    {"toneDot",     PM_TR("Dot size:"), 0,   100, " px", PM_TR("A dot's width. Past the spacing the dots merge into a dark tone."), 1},
    {"toneAngle",   PM_TR("Angle:"),    -90, 90,  "°",   PM_TR("How far the grid of dots is turned, clockwise.")},
};
#undef PM_TR

} // namespace

SkinEditor::SkinEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = m_form = new QFormLayout(this);
    // No margins: this sits inside the host panel's own group box, so its rows should line up with
    // the rows around them rather than being visibly boxed off.
    form->setContentsMargins(0, 0, 0, 0);

    // What fills the balloon: the colour alone, or a pattern of it and a second colour. The line round it
    // is the Line section's (PLAN-L B32).
    m_fillSwatch = new QPushButton(tr("Colour"), this);
    m_fillSwatch->setToolTip(tr("The fill's colour; under a pattern, its ground."));
    form->addRow(tr("Colour"), m_fillSwatch);
    m_fillKind = new QComboBox(this);
    for (int i = 0; i <= int(SkinProperties::k_lastFill); ++i)
        m_fillKind->addItem(fillTitle(static_cast<ObjectRecord::Fill>(i)), i);
    m_fillKind->setToolTip(tr("What fills the balloon: the fill colour, or a screentone of dots in a second "
                              "colour over it."));
    form->addRow(tr("Pattern"), m_fillKind);
    m_secondSwatch = new QPushButton(tr("Dots"), this);
    m_secondSwatch->setToolTip(tr("The pattern's own colour, over the fill colour."));
    form->addRow(tr("Pattern colour"), m_secondSwatch);
    m_tone.build(this, form, "tone", k_toneRows, k_trContext, m_values, m_populating, [this] { emit changed(); });

    connect(m_fillKind, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.fillKind = static_cast<ObjectRecord::Fill>(m_fillKind->currentData().toInt());
        m_kindTouched     = true;
        m_mixedKind       = false;
        showRows();
        emit changed();
    });
    connect(m_secondSwatch, &QPushButton::clicked, this, [this] {
        if (pickColour(m_values.fillSecond, m_secondSwatch)) {
            m_secondTouched = true;
            m_mixedSecond   = false;
        }
    });

    connect(m_fillSwatch, &QPushButton::clicked, this, [this] {
        if (pickColour(m_values.fill, m_fillSwatch)) {
            m_fillTouched = true;
            m_mixedFill   = false;   // they all take this one now
        }
    });

    syncFromValues();
}

void SkinEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;   // nothing selected: the controls keep showing what they showed

    m_values   = SkinProperties::from(*subjects.first());
    m_subjects = static_cast<int>(subjects.size());

    // What the selection disagrees about is not a value this panel may show.
    m_mixedFill = m_mixedKind = m_mixedSecond = false;
    m_tone.reset();
    for (const ObjectRecord* a : subjects) {
        const SkinProperties s = SkinProperties::from(*a);
        m_mixedFill   = m_mixedFill   || s.fill        != m_values.fill;
        m_mixedKind   = m_mixedKind   || s.fillKind    != m_values.fillKind;
        m_mixedSecond = m_mixedSecond || s.fillSecond  != m_values.fillSecond;
        m_tone.compare(s, m_values);
    }
    // Binding is not editing.
    m_fillTouched = m_kindTouched = m_secondTouched = false;

    syncFromValues();
}

void SkinEditor::applyTo(ObjectRecord& target) const
{
    // One line, and that is the point: the group's own applyTo() is the only write path, so this
    // editor cannot reach a property it does not own even by accident.
    m_values.applyTo(target);
}

void SkinEditor::applyEditedTo(ObjectRecord& target) const
{
    // Still one write path — the group's — but only the properties that were actually picked. What the
    // artist did not touch stays each object's own, which is the whole point of editing a set.
    SkinProperties t = SkinProperties::from(target);
    if (m_fillTouched)   t.fill        = m_values.fill;
    if (m_kindTouched)   t.fillKind    = m_values.fillKind;
    if (m_secondTouched) t.fillSecond  = m_values.fillSecond;
    m_tone.applyEdited(m_values, t);
    t.applyTo(target);
}

void SkinEditor::syncFromValues()
{
    m_populating = true;
    if (m_mixedFill)
        paintMixedSwatch(m_fillSwatch, palette());
    else
        paintColourSwatch(m_fillSwatch, m_values.fill);
    showCombo(m_fillKind, m_mixedKind, m_fillKind->findData(int(m_values.fillKind)));
    if (m_mixedSecond)
        paintMixedSwatch(m_secondSwatch, palette());
    else
        paintColourSwatch(m_secondSwatch, m_values.fillSecond);
    m_tone.show(m_values);
    showRows();

    m_populating = false;
}

void SkinEditor::showRows()
{
    // A selection that disagrees shows the rows at least one of them has.
    const bool tone = m_mixedKind || m_values.fillKind == SkinProperties::Fill::Screentone;
    m_form->setRowVisible(m_secondSwatch, tone);
    m_tone.setVisible(m_form, tone);
}

bool SkinEditor::pickColour(QColor& target, QPushButton* swatch)
{
    const QColor picked = QColorDialog::getColor(target, this, tr("Choose colour"));
    if (!picked.isValid())
        return false;
    target = picked;
    paintColourSwatch(swatch, picked);
    emit changed();
    emit committed();   // a dialog choice is discrete — commit it without waiting on a timer
    return true;
}

}  // namespace StripEdit
