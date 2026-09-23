#include "texteditor.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace StripEdit {

TextEditor::TextEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_body = new QPlainTextEdit(this);
    m_body->setPlaceholderText(tr("Type the line…"));
    m_body->setMinimumHeight(70);
    lay->addWidget(m_body);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(form);

    m_family = new QFontComboBox(this);
    form->addRow(tr("Font"), m_family);

    m_size = new QSpinBox(this);
    m_size->setRange(k_textSizeMin, k_textSizeMax);
    m_size->setSuffix(tr(" px"));
    // Strip-scale pixels: the same number the render uses, so a size chosen here means the same thing
    // in the output. It is not a point size and does not follow the screen's DPI.
    m_size->setToolTip(tr("Height in output pixels, at the project's target width."));
    form->addRow(tr("Size"), m_size);

    m_bold = new QCheckBox(tr("Bold"), this);
    form->addRow(QString(), m_bold);

    m_align = new QComboBox(this);
    m_align->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Left"),   int(Qt::AlignLeft));
    m_align->addItem(tr("Right"),  int(Qt::AlignRight));
    form->addRow(tr("Align"), m_align);

    m_swatch = new QPushButton(tr("Colour"), this);
    form->addRow(tr("Colour"), m_swatch);

    // Each control records that *it* was the one moved. Bound to a set, only what was moved is written,
    // and everything else stays each object's own — see applyEditedTo().
    const auto changed = [this] {
        if (m_populating)
            return;
        m_values.body = m_body->toPlainText();
        emit edited();
    };
    connect(m_body, &QPlainTextEdit::textChanged, this, changed);

    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (m_populating)
            return;
        m_values.family = f.family();
        m_familyTouched = true;
        m_mixedFamily   = false;
        emit edited();
    });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_size, k_textSizeMin))
            m_mixedSize = false;
        m_values.pixelSize = v;
        m_sizeTouched      = true;
        emit edited();
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_bold->setTristate(false);   // they have taken a position; there is no third state to return to
        m_values.bold = on;
        m_boldTouched = true;
        m_mixedBold   = false;
        emit edited();
    });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.align = m_align->currentData().toInt();
        m_alignTouched = true;
        m_mixedAlign   = false;
        emit edited();
    });

    connect(m_swatch, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(m_values.colour, this, tr("Choose colour"));
        if (!picked.isValid())
            return;
        m_values.colour = picked;
        m_colourTouched = true;
        m_mixedColour   = false;   // they all take this one now
        paintColourSwatch(m_swatch, picked);
        emit edited();
        emit committed();   // a dialog choice is discrete — commit it without waiting on a timer
    });

    syncFromValues();
}

void TextEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;

    m_values   = TextProperties::from(*subjects.first());
    m_subjects = static_cast<int>(subjects.size());

    m_mixedColour = m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedAlign = false;
    for (const Artifact* a : subjects) {
        const TextProperties t = TextProperties::from(*a);
        m_mixedColour = m_mixedColour || t.colour    != m_values.colour;
        m_mixedFamily = m_mixedFamily || t.family    != m_values.family;
        m_mixedSize   = m_mixedSize   || t.pixelSize != m_values.pixelSize;
        m_mixedBold   = m_mixedBold   || t.bold      != m_values.bold;
        m_mixedAlign  = m_mixedAlign  || t.align     != m_values.align;
    }
    m_colourTouched = m_familyTouched = m_sizeTouched = m_boldTouched = m_alignTouched = false;

    syncFromValues();
}

void TextEditor::applyTo(Artifact& target) const
{
    m_values.applyTo(target);
}

void TextEditor::applyEditedTo(Artifact& target) const
{
    // The lettering itself is never written to a set: five balloons do not share one line of dialogue.
    TextProperties t = TextProperties::from(target);
    if (m_colourTouched) t.colour    = m_values.colour;
    if (m_familyTouched) t.family    = m_values.family;
    if (m_sizeTouched)   t.pixelSize = m_values.pixelSize;
    if (m_boldTouched)   t.bold      = m_values.bold;
    if (m_alignTouched)  t.align     = m_values.align;
    t.applyTo(target);
}

void TextEditor::focusContent()
{
    m_body->setFocus(Qt::OtherFocusReason);
    m_body->selectAll();   // a duplicate arrives with the original's line; typing should replace it
}

void TextEditor::setContentVisible(bool on)
{
    m_bodyVisible = on;
    m_body->setVisible(on && m_subjects <= 1);
}

void TextEditor::setContentEnabled(bool on)
{
    m_body->setEnabled(on);
}

void TextEditor::syncFromValues()
{
    m_populating = true;
    {
        const QSignalBlocker b1(m_body), b2(m_family);

        if (m_body->toPlainText() != m_values.body)
            m_body->setPlainText(m_values.body);   // guarded: setPlainText resets the caret

        // A font combo is a list of fonts, so "Mixed" cannot be an entry in it: no current entry, and
        // the placeholder says which state that is.
        m_family->setPlaceholderText(tr("Mixed"));
        if (m_mixedFamily)
            m_family->setCurrentIndex(-1);
        else if (!m_values.family.isEmpty())
            m_family->setCurrentFont(QFont(m_values.family));
    }
    showSpin(m_size, m_mixedSize, m_values.pixelSize, k_textSizeMin, k_textSizeMax);
    showCheck(m_bold, m_mixedBold, m_values.bold);
    showCombo(m_align, m_mixedAlign, m_align->findData(m_values.align));
    if (m_mixedColour)
        paintMixedSwatch(m_swatch, palette());
    else
        paintColourSwatch(m_swatch, m_values.colour);

    // The lettering itself is the one thing a set never shares: five balloons do not share one line.
    m_body->setVisible(m_subjects <= 1 && m_bodyVisible);
    m_populating = false;
}

}  // namespace StripEdit
