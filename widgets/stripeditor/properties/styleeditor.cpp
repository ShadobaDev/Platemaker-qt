#include "styleeditor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>

namespace StripEdit {

StyleEditor::StyleEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_kind = new QComboBox(this);
    m_kind->addItem(tr("Clean"),  int(TextArtifact::Style::Clean));
    m_kind->addItem(tr("Marker"), int(TextArtifact::Style::Marker));
    m_kind->addItem(tr("Ink"),    int(TextArtifact::Style::Ink));
    m_kind->setToolTip(tr("Roughens the outline as it is rendered. Shown here exactly as it will "
                          "be baked, because the library draws it."));
    form->addRow(tr("Line style:"), m_kind);

    m_amount = new QSpinBox(this);
    m_amount->setRange(0, 200);
    m_amount->setSuffix(tr(" %"));
    m_amount->setToolTip(tr("How strong the line style is. 100% is the preset's own strength."));
    form->addRow(tr("Style amount:"), m_amount);

    const auto changed = [this] {
        if (m_populating)
            return;
        m_values.kind   = static_cast<TextArtifact::Style>(m_kind->currentData().toInt());
        m_values.amount = m_amount->value() / 100.0;
        m_amount->setEnabled(m_values.kind != TextArtifact::Style::Clean);
        emit edited();
    };
    connect(m_kind,   &QComboBox::currentIndexChanged, this, changed);
    connect(m_amount, &QSpinBox::valueChanged,         this, changed);

    syncFromValues();
}

void StyleEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = StyleProperties::from(*subjects.first());
    syncFromValues();
}

void StyleEditor::applyTo(TextArtifact& target) const
{
    m_values.applyTo(target);
}

void StyleEditor::syncFromValues()
{
    m_populating = true;
    {
        const QSignalBlocker b1(m_kind), b2(m_amount);
        m_kind->setCurrentIndex(m_kind->findData(int(m_values.kind)));
        m_amount->setValue(qRound(m_values.amount * 100.0));
    }
    // Clean emits no filter at all, so there is no strength to scale.
    m_amount->setEnabled(m_values.kind != TextArtifact::Style::Clean);
    m_populating = false;
}

}  // namespace StripEdit
