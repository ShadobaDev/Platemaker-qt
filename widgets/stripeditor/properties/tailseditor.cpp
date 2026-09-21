#include "tailseditor.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>

#include "shapeeditor.h"   // shapeSpeaks()

namespace StripEdit {

TailsEditor::TailsEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_enabled = new QCheckBox(tr("Tail"), this);
    m_enabled->setToolTip(tr("Drag the round handle on the bubble to aim it — including outside it."));
    form->addRow(QString(), m_enabled);

    // Aiming is a drag on the strip; these are the two things a drag cannot say.
    m_width = new QSpinBox(this);
    m_width->setRange(k_tailWidthMinPx, k_tailWidthMaxPx);
    m_width->setSuffix(tr(" px"));
    m_width->setToolTip(tr("How wide the tail is where it leaves the bubble."));
    form->addRow(tr("Tail width:"), m_width);

    m_bend = new QSpinBox(this);
    m_bend->setRange(-k_tailBendPercent, k_tailBendPercent);
    m_bend->setSuffix(tr(" %"));
    m_bend->setToolTip(tr("Curves the tail sideways. 0 is straight."));
    form->addRow(tr("Tail bend:"), m_bend);

    const auto changed = [this] {
        if (m_populating)
            return;
        syncEnabled();
        emit edited();
    };
    connect(m_enabled, &QCheckBox::toggled,     this, changed);
    connect(m_width,   &QSpinBox::valueChanged, this, changed);
    connect(m_bend,    &QSpinBox::valueChanged, this, changed);

    syncEnabled();
}

void TailsEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const TextArtifact& a = *subjects.first();
    m_values        = TailsProperties::from(a);
    m_box           = a.box;
    m_shapeCanSpeak = a.hasSilhouette();

    m_populating = true;
    {
        const QSignalBlocker b1(m_enabled), b2(m_width), b3(m_bend);
        const bool has = !m_values.items.isEmpty();
        m_enabled->setChecked(has);
        if (has) {
            m_width->setValue(qRound(m_values.items.first().baseWidth));
            m_bend->setValue(qRound(m_values.items.first().bend * 100.0));
        }
    }
    m_populating = false;
    syncEnabled();
}

void TailsEditor::applyTo(TextArtifact& target) const
{
    TailsProperties next = m_values;

    // A shapeless artifact has nothing to grow a tail from, whatever the checkbox says.
    const bool want = m_enabled->isChecked() && target.hasSilhouette();
    if (!want) {
        next.items.clear();
    } else {
        if (next.items.isEmpty()) {
            Tail t;
            t.tip = firstTailTip(target.box);
            next.items.append(t);
        }
        for (Tail& t : next.items) {
            t.baseWidth = m_width->value();
            t.bend      = m_bend->value() / 100.0;
        }
    }
    next.applyTo(target);
}

void TailsEditor::applyToNew(TextArtifact& target) const
{
    TailsProperties next;
    if (m_enabled->isChecked() && target.hasSilhouette()) {
        Tail t;
        t.tip       = firstTailTip(target.box);
        t.baseWidth = m_width->value();
        t.bend      = m_bend->value() / 100.0;
        next.items  = {t};
    }
    next.applyTo(target);
}

void TailsEditor::shapeChanged(TextArtifact::Shape kind)
{
    m_shapeCanSpeak = kind != TextArtifact::Shape::None;
    const QSignalBlocker block(m_enabled);
    m_enabled->setChecked(shapeSpeaks(kind));
    syncEnabled();
}

void TailsEditor::syncEnabled()
{
    const bool want = m_enabled->isChecked() && m_shapeCanSpeak;
    m_width->setEnabled(want);
    m_bend->setEnabled(want);
}

}  // namespace StripEdit
