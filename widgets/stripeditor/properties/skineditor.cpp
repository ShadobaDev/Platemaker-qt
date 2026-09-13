#include "skineditor.h"

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
    m_strokeWidth->setRange(0, 40);
    m_strokeWidth->setSuffix(tr(" px"));
    form->addRow(tr("Stroke width"), m_strokeWidth);

    connect(m_strokeWidth, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        m_values.strokeWidth = v;
        emit edited();
    });
    connect(m_fillSwatch,   &QPushButton::clicked, this, [this] { pickColour(m_values.fill,   m_fillSwatch); });
    connect(m_strokeSwatch, &QPushButton::clicked, this, [this] { pickColour(m_values.stroke, m_strokeSwatch); });

    syncFromValues();
}

void SkinEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;   // nothing selected: the controls keep showing what they showed
    m_values = SkinProperties::from(*subjects.first());
    syncFromValues();
}

void SkinEditor::applyTo(TextArtifact& target) const
{
    // One line, and that is the point: the group's own applyTo() is the only write path, so this
    // editor cannot reach a property it does not own even by accident.
    m_values.applyTo(target);
}

void SkinEditor::syncFromValues()
{
    m_populating = true;
    {
        const QSignalBlocker block(m_strokeWidth);
        m_strokeWidth->setValue(m_values.strokeWidth);
    }
    paintColourSwatch(m_fillSwatch,   m_values.fill);
    paintColourSwatch(m_strokeSwatch, m_values.stroke);
    m_populating = false;
}

void SkinEditor::pickColour(QColor& target, QPushButton* swatch)
{
    const QColor picked = QColorDialog::getColor(target, this, tr("Choose colour"));
    if (!picked.isValid())
        return;
    target = picked;
    paintColourSwatch(swatch, picked);
    emit edited();
    emit committed();   // a dialog choice is discrete — commit it without waiting on a timer
}

}  // namespace StripEdit
