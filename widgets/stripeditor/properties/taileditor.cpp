#include "properties/taileditor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>

namespace StripEdit {

namespace {

//! A Circles tail's bounds: ellipses, and their pitch and offset in balloon pixels.
constexpr int    k_circlesMax = 12;
constexpr double k_pitchMax   = 200.0;
constexpr double k_offsetMax  = 200.0;

} // namespace

TailEditor::TailEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* form = m_form = new QFormLayout(this);
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

    m_style = new QComboBox(this);
    m_style->addItem(tr("Plain"), int(TailLook::Style::Plain));
    m_style->addItem(tr("Circles"), int(TailLook::Style::Circles));
    m_style->setToolTip(tr("One taper, or a chain of ellipses getting smaller towards the tip, a thought's. "
                           "A tail added to this balloon later takes the style picked last."));
    form->addRow(tr("Style:"), m_style);

    m_circles = new QSpinBox(this);
    m_circles->setRange(1, k_circlesMax);
    m_circles->setToolTip(tr("How many ellipses make the tail. The last one ends at the tip."));
    form->addRow(tr("Number:"), m_circles);

    m_pitch = new QDoubleSpinBox(this);
    m_pitch->setDecimals(1);
    m_pitch->setRange(-k_pitchMax, k_pitchMax);
    m_pitch->setSuffix(tr(" px"));
    m_pitch->setToolTip(tr("The gap between two ellipses. Below 0 they overlap."));
    form->addRow(tr("Pitch:"), m_pitch);

    m_offset = new QDoubleSpinBox(this);
    m_offset->setDecimals(1);
    m_offset->setRange(-k_offsetMax, k_offsetMax);
    m_offset->setSuffix(tr(" px"));
    m_offset->setToolTip(tr("Where the first ellipse starts: above 0 it sinks into the balloon and melts into "
                            "it, below 0 it leaves a gap."));
    form->addRow(tr("Starting offset:"), m_offset);

    const auto controlMoved = [this] {
        if (!m_populating)
            emit changed();
    };
    connect(m_width,   &QSpinBox::valueChanged, this, controlMoved);
    connect(m_bend,    &QSpinBox::valueChanged, this, controlMoved);
    connect(m_circles, &QSpinBox::valueChanged, this, controlMoved);
    connect(m_pitch,   &QDoubleSpinBox::valueChanged, this, controlMoved);
    connect(m_offset,  &QDoubleSpinBox::valueChanged, this, controlMoved);
    connect(m_style, &QComboBox::currentIndexChanged, this, [this] {
        showRows();
        if (!m_populating)
            emit changed();
    });
    showRows();
}

void TailEditor::showRows()
{
    const bool circles = m_style->currentData().toInt() == int(TailLook::Style::Circles);
    m_form->setRowVisible(m_circles, circles);
    m_form->setRowVisible(m_pitch, circles);
    m_form->setRowVisible(m_offset, circles);
}

void TailEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const TailProperties values = TailProperties::from(*subjects.first(), m_index);

    m_populating = true;
    {
        const QSignalBlocker b1(m_width), b2(m_bend), b3(m_style), b4(m_circles), b5(m_pitch), b6(m_offset);
        m_width->setValue(qRound(values.tail.baseWidth));
        m_bend->setValue(qRound(values.tail.bend * k_tailBendPercent));
        m_style->setCurrentIndex(m_style->findData(int(values.tail.look.style)));
        m_circles->setValue(values.tail.look.circles);
        m_pitch->setValue(values.tail.look.pitch);
        m_offset->setValue(values.tail.look.offset);
    }
    showRows();
    m_populating = false;
}

void TailEditor::applyTo(ObjectRecord& target) const
{
    TailProperties next = TailProperties::from(target, m_index);   // the tip as it is now
    next.tail.baseWidth = m_width->value();
    next.tail.bend      = m_bend->value() / static_cast<double>(k_tailBendPercent);
    next.tail.look.style   = static_cast<TailLook::Style>(m_style->currentData().toInt());
    next.tail.look.circles = m_circles->value();
    next.tail.look.pitch   = m_pitch->value();
    next.tail.look.offset  = m_offset->value();
    next.applyTo(target);   // a look changed here becomes the balloon's next one
}

}  // namespace StripEdit
