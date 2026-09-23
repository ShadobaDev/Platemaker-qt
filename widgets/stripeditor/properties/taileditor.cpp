#include "taileditor.hpp"

#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>

namespace StripEdit {

TailEditor::TailEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_width = new QSpinBox(this);
    m_width->setRange(k_tailWidthMinPx, k_tailWidthMaxPx);
    m_width->setSuffix(tr(" px"));
    m_width->setToolTip(tr("How wide this tail is where it leaves the balloon."));
    form->addRow(tr("Width:"), m_width);

    m_bend = new QSpinBox(this);
    m_bend->setRange(-k_tailBendPercent, k_tailBendPercent);
    m_bend->setSuffix(tr(" %"));
    m_bend->setToolTip(tr("Curves this tail sideways. 0 is straight."));
    form->addRow(tr("Bend:"), m_bend);

    const auto changed = [this] {
        if (!m_populating)
            emit edited();
    };
    connect(m_width, &QSpinBox::valueChanged, this, changed);
    connect(m_bend,  &QSpinBox::valueChanged, this, changed);
}

void TailEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const TailProperties values = TailProperties::from(*subjects.first(), m_index);

    m_populating = true;
    {
        const QSignalBlocker b1(m_width), b2(m_bend);
        m_width->setValue(qRound(values.tail.baseWidth));
        m_bend->setValue(qRound(values.tail.bend * k_tailBendPercent));
    }
    m_populating = false;
}

void TailEditor::applyTo(Artifact& target) const
{
    TailProperties next = TailProperties::from(target, m_index);   // the tip as it is now
    next.tail.baseWidth = m_width->value();
    next.tail.bend      = m_bend->value() / static_cast<double>(k_tailBendPercent);
    next.applyTo(target);
}

}  // namespace StripEdit
