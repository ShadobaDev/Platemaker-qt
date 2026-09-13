#include "tailseditor.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QPushButton>
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
    m_width->setRange(4, 400);
    m_width->setSuffix(tr(" px"));
    m_width->setToolTip(tr("How wide the tail is where it leaves the bubble."));
    form->addRow(tr("Tail width:"), m_width);

    m_bend = new QSpinBox(this);
    m_bend->setRange(-100, 100);
    m_bend->setSuffix(tr(" %"));
    m_bend->setToolTip(tr("Curves the tail sideways. 0 is straight."));
    form->addRow(tr("Tail bend:"), m_bend);

    m_add = new QPushButton(tr("Add another tail"), this);
    m_add->setToolTip(tr("For a sound with more than one source. Drag each handle to aim it."));
    form->addRow(QString(), m_add);

    const auto changed = [this] {
        if (m_populating)
            return;
        syncEnabled();
        emit edited();
    };
    connect(m_enabled, &QCheckBox::toggled,     this, changed);
    connect(m_width,   &QSpinBox::valueChanged, this, changed);
    connect(m_bend,    &QSpinBox::valueChanged, this, changed);

    connect(m_add, &QPushButton::clicked, this, [this] {
        // A new tail starts opposite the last one so it is visible rather than stacked on top of it.
        Tail t;
        t.baseWidth = m_width->value();
        t.bend      = m_bend->value() / 100.0;
        t.tip       = m_values.items.isEmpty()
            ? QPointF(m_box.width() * 0.28, m_box.height() * 1.25)
            : QPointF(m_box.width() - m_values.items.last().tip.x(), m_values.items.last().tip.y());
        m_values.items.append(t);
        m_enabled->setChecked(true);
        syncEnabled();
        emit edited();
    });

    syncEnabled();
}

void TailsEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const TextArtifact& a = *subjects.first();
    m_values        = TailsProperties::from(a);
    m_box           = a.box;
    m_shapeCanSpeak = a.shape.kind != TextArtifact::Shape::None;

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
    const bool want = m_enabled->isChecked() && target.shape.kind != TextArtifact::Shape::None;
    if (!want) {
        next.items.clear();
    } else {
        if (next.items.isEmpty()) {
            Tail t;
            t.tip = QPointF(target.box.width() * 0.28, target.box.height() * 1.25);
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
    if (m_enabled->isChecked() && target.shape.kind != TextArtifact::Shape::None) {
        Tail t;
        t.tip       = QPointF(target.box.width() * 0.28, target.box.height() * 1.25);
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

void TailsEditor::setAddVisible(bool on)
{
    m_add->setVisible(on);
}

void TailsEditor::syncEnabled()
{
    const bool want = m_enabled->isChecked() && m_shapeCanSpeak;
    m_width->setEnabled(want);
    m_bend->setEnabled(want);
    m_add->setEnabled(want);
}

}  // namespace StripEdit
