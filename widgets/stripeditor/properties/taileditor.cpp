#include "properties/taileditor.hpp"
#include "styles.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>

#include <cstring>

namespace StripEdit {

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
    for (int i = 0; i <= int(TailLook::k_lastStyle); ++i)
        m_style->addItem(QObject::tr(Geometry::tailStyleInfo(TailLook::Style(i)).title), i);
    m_style->setToolTip(tr("One taper, or a chain of ellipses getting smaller towards the tip, a thought's. "
                           "A tail added to this balloon later takes the style picked last."));
    form->addRow(tr("Style:"), m_style);

    m_tip = new QComboBox(this);
    for (int i = 0; i <= int(TailLook::k_lastTip); ++i)
        m_tip->addItem(QObject::tr(Geometry::tailTipInfo(TailLook::Tip(i)).title), i);
    m_tip->setToolTip(tr("How the tail ends: in a point, rounded, or cut straight across."));
    form->addRow(tr("Tip:"), m_tip);

    // An edit of a number may show or hide another's row (the breaks, their jog).
    m_sections.build(this, form, m_look, m_populating, [this] {
        showRows();
        emit changed();
    });

    // Each control records that *it* was moved, and a *Mixed* one gives up its sentinel when it is: a set
    // of tails takes only what the artist touched (applyEditedToTail()).
    const auto moved = [this](bool Own::* field, auto restore) {
        return [this, field, restore] {
            if (m_populating)
                return;
            restore();
            m_touched.*field = true;
            emit changed();
        };
    };
    connect(m_width, &QSpinBox::valueChanged, this,
            moved(&Own::width, [this] { restoreSpin(m_width, k_tailWidthMinPx); }));
    connect(m_bend, &QSpinBox::valueChanged, this,
            moved(&Own::bend, [this] { restoreSpin(m_bend, -k_tailBendPercent); }));
    connect(m_style, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_look.style     = TailLook::Style(m_style->currentData().toInt());
        m_touched.style  = true;
        m_mixed.style    = false;
        showRows();
        emit changed();
    });
    connect(m_tip, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_look.tip    = TailLook::Tip(m_tip->currentData().toInt());
        m_touched.tip = true;
        m_mixed.tip   = false;
        showRows();
        emit changed();
    });
    showRows();
}

void TailEditor::showRows()
{
    // A selection that disagrees about the style shows every section, and its tip; about the tip, the
    // sections of every tip.
    const Geometry::TailStyleInfo& style = Geometry::tailStyleInfo(m_look.style);
    const bool                     tip   = m_mixed.style || style.tipped;
    m_form->setRowVisible(m_tip, tip);

    Fields::SectionNames names = style.sections;
    const auto add = [&names](const char* name) {
        for (const char*& n : names) {
            if (n && std::strcmp(n, name) == 0)
                return;
            if (!n) {
                n = name;
                return;
            }
        }
        Q_ASSERT_X(false, "TailEditor", "a style and its tip read more sections than SectionNames holds");
    };
    if (style.tipped)
        for (int i = 0; i <= int(TailLook::k_lastTip); ++i)
            if (m_mixed.tip || TailLook::Tip(i) == m_look.tip)
                for (const char* n : Geometry::tailTipInfo(TailLook::Tip(i)).sections)
                    if (n)
                        add(n);
    m_sections.showSections(m_form, m_look, true, m_mixed.style, names);
}

void TailEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_sections.reset();
    show(TailProperties::from(*subjects.first(), m_index).tail, Own{});
}

void TailEditor::bindTails(const QList<TailProperties>& tails)
{
    if (tails.isEmpty())
        return;
    const Tail& first = tails.first().tail;
    Own         mixed;
    m_sections.reset();
    for (const TailProperties& p : tails) {
        mixed.width = mixed.width || p.tail.baseWidth != first.baseWidth;
        mixed.bend  = mixed.bend || p.tail.bend != first.bend;
        mixed.style = mixed.style || p.tail.look.style != first.look.style;
        mixed.tip   = mixed.tip || p.tail.look.tip != first.look.tip;
        m_sections.compare(p.tail.look, first.look);
    }
    show(first, mixed);
}

void TailEditor::show(const Tail& values, const Own& mixed)
{
    m_populating = true;
    m_look       = values.look;
    m_mixed      = mixed;
    showSpin(m_width, mixed.width, qRound(values.baseWidth), k_tailWidthMinPx, k_tailWidthMaxPx);
    showSpin(m_bend, mixed.bend, qRound(values.bend * k_tailBendPercent), -k_tailBendPercent, k_tailBendPercent);
    showCombo(m_style, mixed.style, m_style->findData(int(m_look.style)));
    showCombo(m_tip, mixed.tip, m_tip->findData(int(m_look.tip)));
    m_sections.show(m_look);
    showRows();
    m_touched    = Own{};
    m_populating = false;
}

void TailEditor::applyEditedToTail(ObjectRecord& target, int index) const
{
    TailProperties next = TailProperties::from(target, index);   // the tip, and what was not touched
    if (m_touched.width) next.tail.baseWidth  = m_width->value();
    if (m_touched.bend)  next.tail.bend       = m_bend->value() / static_cast<double>(k_tailBendPercent);
    if (m_touched.style) next.tail.look.style = m_look.style;
    if (m_touched.tip)   next.tail.look.tip   = m_look.tip;
    m_sections.applyEdited(m_look, next.tail.look);
    next.applyTo(target);
}

void TailEditor::applyTo(ObjectRecord& target) const
{
    TailProperties next = TailProperties::from(target, m_index);   // the tip as it is now
    next.tail.baseWidth = m_width->value();
    next.tail.bend      = m_bend->value() / static_cast<double>(k_tailBendPercent);
    next.tail.look      = m_look;
    next.applyTo(target);   // a look changed here becomes the balloon's next one
}

}  // namespace StripEdit
