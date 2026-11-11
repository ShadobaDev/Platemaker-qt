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
//! A tip's width, in per cent of the base width.
constexpr int    k_tipWidthMax = 100;

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

    m_tip = new QComboBox(this);
    m_tip->addItem(tr("Sharp"), int(TailLook::Tip::Sharp));
    m_tip->addItem(tr("Round"), int(TailLook::Tip::Round));
    m_tip->addItem(tr("Cut"), int(TailLook::Tip::Cut));
    m_tip->setToolTip(tr("How the tail ends: in a point, rounded, or cut straight across."));
    form->addRow(tr("Tip:"), m_tip);

    m_tipWidth = new QSpinBox(this);
    m_tipWidth->setRange(0, k_tipWidthMax);
    m_tipWidth->setSuffix(tr(" %"));
    m_tipWidth->setToolTip(tr("How wide a round or cut end is, against the tail's width where it leaves the "
                              "balloon."));
    form->addRow(tr("Tip width:"), m_tipWidth);

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

    // Each control records that *it* was moved, and a *Mixed* one gives up its sentinel when it is: a set
    // of tails takes only what the artist touched (applyEditedToTail()).
    const auto moved = [this](bool Fields::* field, auto restore) {
        return [this, field, restore] {
            if (m_populating)
                return;
            restore();
            m_touched.*field = true;
            emit changed();
        };
    };
    connect(m_width, &QSpinBox::valueChanged, this,
            moved(&Fields::width, [this] { restoreSpin(m_width, k_tailWidthMinPx); }));
    connect(m_bend, &QSpinBox::valueChanged, this,
            moved(&Fields::bend, [this] { restoreSpin(m_bend, -k_tailBendPercent); }));
    connect(m_circles, &QSpinBox::valueChanged, this,
            moved(&Fields::circles, [this] { restoreSpin(m_circles, 1); }));
    connect(m_pitch, &QDoubleSpinBox::valueChanged, this,
            moved(&Fields::pitch, [this] { restoreSpin(m_pitch, -k_pitchMax); }));
    connect(m_offset, &QDoubleSpinBox::valueChanged, this,
            moved(&Fields::offset, [this] { restoreSpin(m_offset, -k_offsetMax); }));
    connect(m_tipWidth, &QSpinBox::valueChanged, this,
            moved(&Fields::tipWidth, [this] { restoreSpin(m_tipWidth, 0); }));
    for (auto [combo, field] : {std::pair{m_style, &Fields::style}, std::pair{m_tip, &Fields::tip}}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this, field] {
            showRows();
            if (m_populating)
                return;
            m_touched.*field = true;
            emit changed();
        });
    }
    showRows();
}

void TailEditor::showRows()
{
    const bool circles = m_style->currentData().toInt() == int(TailLook::Style::Circles);
    m_form->setRowVisible(m_circles, circles);
    m_form->setRowVisible(m_pitch, circles);
    m_form->setRowVisible(m_offset, circles);
    m_form->setRowVisible(m_tip, !circles);
    m_form->setRowVisible(m_tipWidth, !circles && m_tip->currentData().toInt() != int(TailLook::Tip::Sharp));
}

void TailEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    show(TailProperties::from(*subjects.first(), m_index), Fields{});
}

void TailEditor::bindTails(const QList<TailProperties>& tails)
{
    if (tails.isEmpty())
        return;
    const Tail& first = tails.first().tail;
    Fields      mixed;
    for (const TailProperties& p : tails) {
        mixed.width   = mixed.width || p.tail.baseWidth != first.baseWidth;
        mixed.bend    = mixed.bend || p.tail.bend != first.bend;
        mixed.style   = mixed.style || p.tail.look.style != first.look.style;
        mixed.circles = mixed.circles || p.tail.look.circles != first.look.circles;
        mixed.pitch   = mixed.pitch || p.tail.look.pitch != first.look.pitch;
        mixed.offset  = mixed.offset || p.tail.look.offset != first.look.offset;
        mixed.tip      = mixed.tip || p.tail.look.tip != first.look.tip;
        mixed.tipWidth = mixed.tipWidth || p.tail.look.tipWidth != first.look.tipWidth;
    }
    show(tails.first(), mixed);
}

void TailEditor::show(const TailProperties& values, const Fields& mixed)
{
    m_populating = true;
    showSpin(m_width, mixed.width, qRound(values.tail.baseWidth), k_tailWidthMinPx, k_tailWidthMaxPx);
    showSpin(m_bend, mixed.bend, qRound(values.tail.bend * k_tailBendPercent), -k_tailBendPercent, k_tailBendPercent);
    showCombo(m_style, mixed.style, m_style->findData(int(values.tail.look.style)));
    showSpin(m_circles, mixed.circles, values.tail.look.circles, 1, k_circlesMax);
    showSpin(m_pitch, mixed.pitch, values.tail.look.pitch, -k_pitchMax, k_pitchMax);
    showSpin(m_offset, mixed.offset, values.tail.look.offset, -k_offsetMax, k_offsetMax);
    showCombo(m_tip, mixed.tip, m_tip->findData(int(values.tail.look.tip)));
    showSpin(m_tipWidth, mixed.tipWidth, values.tail.look.tipWidth, 0, k_tipWidthMax);
    showRows();
    m_touched    = Fields{};
    m_populating = false;
}

void TailEditor::applyEditedToTail(ObjectRecord& target, int index) const
{
    TailProperties next = TailProperties::from(target, index);   // the tip, and what was not touched
    if (m_touched.width)   next.tail.baseWidth    = m_width->value();
    if (m_touched.bend)    next.tail.bend         = m_bend->value() / static_cast<double>(k_tailBendPercent);
    if (m_touched.style)   next.tail.look.style   = static_cast<TailLook::Style>(m_style->currentData().toInt());
    if (m_touched.circles) next.tail.look.circles = m_circles->value();
    if (m_touched.pitch)   next.tail.look.pitch   = m_pitch->value();
    if (m_touched.offset)  next.tail.look.offset  = m_offset->value();
    if (m_touched.tip)      next.tail.look.tip      = static_cast<TailLook::Tip>(m_tip->currentData().toInt());
    if (m_touched.tipWidth) next.tail.look.tipWidth = m_tipWidth->value();
    next.applyTo(target);
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
    next.tail.look.tip      = static_cast<TailLook::Tip>(m_tip->currentData().toInt());
    next.tail.look.tipWidth = m_tipWidth->value();
    next.applyTo(target);   // a look changed here becomes the balloon's next one
}

}  // namespace StripEdit
