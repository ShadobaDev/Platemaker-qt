#include "skineditor.hpp"

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

namespace StripEdit {

SkinEditor::SkinEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = new QFormLayout(this);
    // No margins: this sits inside the host panel's own group box, so its rows should line up with
    // the rows around them rather than being visibly boxed off.
    form->setContentsMargins(0, 0, 0, 0);

    m_fillSwatch   = new QPushButton(tr("Fill"),   this);
    m_strokeSwatch = new QPushButton(tr("Stroke"), this);
    auto* colourRow = new QHBoxLayout;
    colourRow->setContentsMargins(0, 0, 0, 0);
    colourRow->addWidget(m_fillSwatch);
    colourRow->addWidget(m_strokeSwatch);
    form->addRow(tr("Colours"), colourRow);

    m_strokeWidth = new QSpinBox(this);
    m_strokeWidth->setRange(k_strokeWidthMin, k_strokeWidthMax);
    m_strokeWidth->setSuffix(tr(" px"));
    form->addRow(tr("Stroke width"), m_strokeWidth);

    connect(m_strokeWidth, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_strokeWidth, k_strokeWidthMin))
            m_mixedWidth = false;   // they have taken a position, so the sentinel goes
        m_values.strokeWidth = v;
        m_widthTouched       = true;
        emit edited();
    });
    connect(m_fillSwatch, &QPushButton::clicked, this, [this] {
        if (pickColour(m_values.fill, m_fillSwatch)) {
            m_fillTouched = true;
            m_mixedFill   = false;   // they all take this one now
        }
    });
    connect(m_strokeSwatch, &QPushButton::clicked, this, [this] {
        if (pickColour(m_values.stroke, m_strokeSwatch)) {
            m_strokeTouched = true;
            m_mixedStroke   = false;
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
    m_mixedFill = m_mixedStroke = m_mixedWidth = false;
    for (const Artifact* a : subjects) {
        const SkinProperties s = SkinProperties::from(*a);
        m_mixedFill   = m_mixedFill   || s.fill        != m_values.fill;
        m_mixedStroke = m_mixedStroke || s.stroke      != m_values.stroke;
        m_mixedWidth  = m_mixedWidth  || s.strokeWidth != m_values.strokeWidth;
    }
    m_fillTouched = m_strokeTouched = m_widthTouched = false;   // binding is not editing

    syncFromValues();
}

void SkinEditor::applyTo(Artifact& target) const
{
    // One line, and that is the point: the group's own applyTo() is the only write path, so this
    // editor cannot reach a property it does not own even by accident.
    m_values.applyTo(target);
}

void SkinEditor::applyEditedTo(Artifact& target) const
{
    // Still one write path — the group's — but only the properties that were actually picked. What the
    // artist did not touch stays each object's own, which is the whole point of editing a set.
    SkinProperties t = SkinProperties::from(target);
    if (m_fillTouched)   t.fill        = m_values.fill;
    if (m_strokeTouched) t.stroke      = m_values.stroke;
    if (m_widthTouched)  t.strokeWidth = m_values.strokeWidth;
    t.applyTo(target);
}

void SkinEditor::syncFromValues()
{
    m_populating = true;
    showSpin(m_strokeWidth, m_mixedWidth, m_values.strokeWidth, k_strokeWidthMin, k_strokeWidthMax);
    if (m_mixedFill)
        paintMixedSwatch(m_fillSwatch, palette());
    else
        paintColourSwatch(m_fillSwatch, m_values.fill);
    if (m_mixedStroke)
        paintMixedSwatch(m_strokeSwatch, palette());
    else
        paintColourSwatch(m_strokeSwatch, m_values.stroke);

    m_populating = false;
}

bool SkinEditor::pickColour(QColor& target, QPushButton* swatch)
{
    const QColor picked = QColorDialog::getColor(target, this, tr("Choose colour"));
    if (!picked.isValid())
        return false;
    target = picked;
    paintColourSwatch(swatch, picked);
    emit edited();
    emit committed();   // a dialog choice is discrete — commit it without waiting on a timer
    return true;
}

}  // namespace StripEdit
