#include "styleeditor.hpp"

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
    m_kind->addItem(tr("Clean"),  int(Artifact::Style::Clean));
    m_kind->addItem(tr("Marker"), int(Artifact::Style::Marker));
    m_kind->addItem(tr("Ink"),    int(Artifact::Style::Ink));
    m_kind->setToolTip(tr("Roughens the outline as it is rendered. Shown here exactly as it will "
                          "be baked, because the library draws it."));
    form->addRow(tr("Line style:"), m_kind);

    m_amount = new QSpinBox(this);
    m_amount->setRange(k_styleAmountMin, k_styleAmountMax);
    m_amount->setSuffix(tr(" %"));
    m_amount->setToolTip(tr("How strong the line style is. 100% is the preset's own strength."));
    form->addRow(tr("Style amount:"), m_amount);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.kind = static_cast<Artifact::Style>(m_kind->currentData().toInt());
        m_kindTouched = true;
        m_mixedKind   = false;
        m_amount->setEnabled(m_values.kind != Artifact::Style::Clean);
        emit edited();
    });
    connect(m_amount, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_amount, k_styleAmountMin))
            m_mixedAmount = false;
        m_values.amount = v / 100.0;
        m_amountTouched = true;
        emit edited();
    });

    syncFromValues();
}

void StyleEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = StyleProperties::from(*subjects.first());

    m_mixedKind = m_mixedAmount = false;
    for (const Artifact* a : subjects) {
        const StyleProperties v = StyleProperties::from(*a);
        m_mixedKind   = m_mixedKind   || v.kind   != m_values.kind;
        m_mixedAmount = m_mixedAmount || !qFuzzyCompare(v.amount + 1.0, m_values.amount + 1.0);
    }
    m_kindTouched = m_amountTouched = false;

    syncFromValues();
}

void StyleEditor::applyTo(Artifact& target) const
{
    m_values.applyTo(target);
}

void StyleEditor::applyEditedTo(Artifact& target) const
{
    StyleProperties t = StyleProperties::from(target);
    if (m_kindTouched)   t.kind   = m_values.kind;
    if (m_amountTouched) t.amount = m_values.amount;
    t.applyTo(target);
}

void StyleEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_kind, m_mixedKind, m_kind->findData(int(m_values.kind)));
    showSpin(m_amount, m_mixedAmount, qRound(m_values.amount * 100.0), k_styleAmountMin, k_styleAmountMax);
    // Clean emits no filter at all, so there is no strength to scale — but a selection that disagrees
    // about the style has something to scale in at least one of them.
    m_amount->setEnabled(m_mixedKind || m_values.kind != Artifact::Style::Clean);
    m_populating = false;
}

}  // namespace StripEdit
