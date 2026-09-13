#include "texteditor.h"

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
    m_size->setRange(6, 400);
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

    const auto changed = [this] {
        if (m_populating)
            return;
        m_values.body      = m_body->toPlainText();
        m_values.family    = m_family->currentFont().family();
        m_values.pixelSize = m_size->value();
        m_values.bold      = m_bold->isChecked();
        m_values.align     = m_align->currentData().toInt();
        emit edited();
    };
    connect(m_body,   &QPlainTextEdit::textChanged,        this, changed);
    connect(m_family, &QFontComboBox::currentFontChanged,  this, changed);
    connect(m_size,   &QSpinBox::valueChanged,             this, changed);
    connect(m_bold,   &QCheckBox::toggled,                 this, changed);
    connect(m_align,  &QComboBox::currentIndexChanged,     this, changed);

    connect(m_swatch, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(m_values.colour, this, tr("Choose colour"));
        if (!picked.isValid())
            return;
        m_values.colour = picked;
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
    m_values = TextProperties::from(*subjects.first());
    syncFromValues();
}

void TextEditor::applyTo(TextArtifact& target) const
{
    m_values.applyTo(target);
}

void TextEditor::focusContent()
{
    m_body->setFocus(Qt::OtherFocusReason);
    m_body->selectAll();   // a duplicate arrives with the original's line; typing should replace it
}

void TextEditor::setContentVisible(bool on)
{
    m_body->setVisible(on);
}

void TextEditor::setContentEnabled(bool on)
{
    m_body->setEnabled(on);
}

void TextEditor::syncFromValues()
{
    m_populating = true;
    {
        const QSignalBlocker b1(m_body),  b2(m_family), b3(m_size);
        const QSignalBlocker b4(m_bold),  b5(m_align);

        if (m_body->toPlainText() != m_values.body)
            m_body->setPlainText(m_values.body);   // guarded: setPlainText resets the caret

        if (!m_values.family.isEmpty())
            m_family->setCurrentFont(QFont(m_values.family));
        m_size->setValue(m_values.pixelSize);
        m_bold->setChecked(m_values.bold);
        const int alignIdx = m_align->findData(m_values.align);
        if (alignIdx >= 0)
            m_align->setCurrentIndex(alignIdx);
    }
    paintColourSwatch(m_swatch, m_values.colour);
    m_populating = false;
}

}  // namespace StripEdit
