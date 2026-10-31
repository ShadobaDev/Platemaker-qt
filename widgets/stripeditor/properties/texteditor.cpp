#include "properties/texteditor.hpp"
#include "geometry.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTextDocument>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>

namespace StripEdit {

namespace {

//! Lines of lettering the box asks room for; it grows past them when the panel has room to spare.
constexpr int k_bodyLines = 4;

/**
 * @brief The lettering box, asking for a few lines' height rather than Qt's 192 px.
 *
 * OBJECT STATE is laid out at its rows' *preferred* heights: the shape tiles reflow with the width, and
 * a scroll area sizes a page with such a row by its height for that width, which sums the preferred
 * heights. A plain text edit prefers 256 × 192, so a one-line balloon's box took a third of the panel
 * and brought up the scroll bar.
 */
class LetteringBox : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;

    QSize sizeHint() const override
    {
        QSize s = QPlainTextEdit::sizeHint();
        s.setHeight(fontMetrics().lineSpacing() * k_bodyLines
                    + qCeil(2 * document()->documentMargin()) + 2 * frameWidth());
        return s;
    }
};

} // namespace

TextEditor::TextEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_body = new LetteringBox(this);
    m_body->setPlaceholderText(tr("Type the line…"));
    m_body->setMinimumHeight(70);
    lay->addWidget(m_body);

    // Spaces, tabs and line ends made visible — in this box only. The balloon is always drawn as it
    // will render, and whitespace is text there like anything else: a trailing empty line is a line.
    // A viewer's preference, so it is remembered for the application, not stored in the workspace.
    m_showWhitespace = new QCheckBox(tr("Show whitespace"), this);
    m_showWhitespace->setToolTip(tr("Show spaces (·), tabs (→) and line ends (¶) in this box.\n"
                                    "Only here — the balloon is drawn as it will render."));
    lay->addWidget(m_showWhitespace);
    const auto showWhitespace = [this](bool on) {
        QTextOption o = m_body->document()->defaultTextOption();
        QTextOption::Flags flags = o.flags();
        flags.setFlag(QTextOption::ShowTabsAndSpaces, on);
        flags.setFlag(QTextOption::ShowLineAndParagraphSeparators, on);
        o.setFlags(flags);
        m_body->document()->setDefaultTextOption(o);
    };
    connect(m_showWhitespace, &QCheckBox::toggled, this, [showWhitespace](bool on) {
        showWhitespace(on);
        QSettings().setValue(QStringLiteral("stripEditor/showWhitespace"), on);
    });
    const bool remembered = QSettings().value(QStringLiteral("stripEditor/showWhitespace"), false).toBool();
    m_showWhitespace->setChecked(remembered);
    showWhitespace(remembered);   // toggled() does not fire when the remembered value is the default

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(form);
    m_form = form;

    m_family = new QFontComboBox(this);
    // A font combo asks for the width of the longest family name it knows, which is what pushed the
    // panel past its narrowest and brought up a horizontal scroll bar. It takes the room it is given.
    m_family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_family->setMinimumContentsLength(8);
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

    m_lineSpacing = new QSpinBox(this);
    m_lineSpacing->setRange(k_lineSpacingMin, k_lineSpacingMax);
    m_lineSpacing->setSingleStep(5);
    m_lineSpacing->setSuffix(tr(" %"));
    m_lineSpacing->setToolTip(tr("Distance between lines, as a percentage of the font's own."));
    form->addRow(tr("Line spacing"), m_lineSpacing);

    m_padding = new QSpinBox(this);
    m_padding->setRange(k_paddingMin, k_paddingMax);
    m_padding->setSuffix(tr(" px"));
    m_padding->setToolTip(tr("Room between the balloon's outline and the lettering.\n"
                             "Negative lets the lettering reach past the outline."));
    form->addRow(tr("Padding"), m_padding);

    m_align = new QComboBox(this);
    m_align->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Left"),   int(Qt::AlignLeft));
    m_align->addItem(tr("Right"),  int(Qt::AlignRight));
    form->addRow(tr("Align"), m_align);

    m_swatch = new QPushButton(tr("Colour"), this);
    form->addRow(tr("Colour"), m_swatch);

    // Each control records that *it* was the one moved. Bound to a set, only what was moved is written,
    // and everything else stays each object's own — see applyEditedTo().
    const auto bodyEdited = [this] {
        if (m_populating)
            return;
        m_values.body = m_body->toPlainText();
        emit changed();
    };
    connect(m_body, &QPlainTextEdit::textChanged, this, bodyEdited);

    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (m_populating)
            return;
        m_values.family = f.family();
        m_familyTouched = true;
        m_mixedFamily   = false;
        emit changed();
    });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_size, k_textSizeMin))
            m_mixedSize = false;
        m_values.pixelSize = v;
        m_sizeTouched      = true;
        emit changed();
    });
    connect(m_lineSpacing, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_lineSpacing, k_lineSpacingMin))
            m_mixedLineSpacing = false;
        m_values.lineSpacing = v;
        m_lineSpacingTouched = true;
        emit changed();
    });
    connect(m_padding, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_padding, k_paddingMin))
            m_mixedPadding = false;
        m_values.padding = v;
        m_paddingTouched = true;
        emit changed();
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_bold->setTristate(false);   // they have taken a position; there is no third state to return to
        m_values.bold = on;
        m_boldTouched = true;
        m_mixedBold   = false;
        emit changed();
    });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.align = m_align->currentData().toInt();
        m_alignTouched = true;
        m_mixedAlign   = false;
        emit changed();
    });

    connect(m_swatch, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(m_values.colour, this, tr("Choose colour"));
        if (!picked.isValid())
            return;
        m_values.colour = picked;
        m_colourTouched = true;
        m_mixedColour   = false;   // they all take this one now
        paintColourSwatch(m_swatch, picked);
        emit changed();
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

    m_mixedColour = m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedAlign = m_mixedLineSpacing = false;
    m_mixedPadding = false;
    m_anyBalloon   = false;
    for (const ObjectRecord* a : subjects) {
        const TextProperties t = TextProperties::from(*a);
        m_mixedColour = m_mixedColour || t.colour    != m_values.colour;
        m_mixedFamily = m_mixedFamily || t.family    != m_values.family;
        m_mixedSize   = m_mixedSize   || t.pixelSize != m_values.pixelSize;
        m_mixedBold   = m_mixedBold   || t.bold      != m_values.bold;
        m_mixedAlign  = m_mixedAlign  || t.align     != m_values.align;
        m_mixedLineSpacing = m_mixedLineSpacing || t.lineSpacing != m_values.lineSpacing;
        m_mixedPadding     = m_mixedPadding     || t.padding     != m_values.padding;
        m_anyBalloon       = m_anyBalloon       || a->hasSilhouette();
    }
    m_colourTouched = m_familyTouched = m_sizeTouched = m_boldTouched = m_alignTouched = false;
    m_lineSpacingTouched = m_paddingTouched = false;

    syncFromValues();
}

void TextEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void TextEditor::applyEditedTo(ObjectRecord& target) const
{
    // The lettering itself is never written to a set: five balloons do not share one line of dialogue.
    TextProperties t = TextProperties::from(target);
    if (m_colourTouched) t.colour    = m_values.colour;
    if (m_familyTouched) t.family    = m_values.family;
    if (m_sizeTouched)   t.pixelSize = m_values.pixelSize;
    if (m_boldTouched)   t.bold      = m_values.bold;
    if (m_alignTouched)  t.align     = m_values.align;
    if (m_lineSpacingTouched) t.lineSpacing = m_values.lineSpacing;
    if (m_paddingTouched)     t.padding     = m_values.padding;
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
    m_showWhitespace->setVisible(m_body->isVisibleTo(this));
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
        // An empty family is not "leave the combo alone" — it is the default font, and has to be shown
        // as that. Skipping it left whatever the combo held from the previous object (a balloon's
        // Segoe Script on a fresh text object in Segoe UI), and picking that same font again then did
        // nothing, because a combo does not report choosing what it already shows.
        if (m_mixedFamily)
            m_family->setCurrentIndex(-1);
        else
            m_family->setCurrentFont(QFont(m_values.family.isEmpty() ? Geometry::defaultFamily()
                                                                     : m_values.family));
    }
    showSpin(m_size, m_mixedSize, m_values.pixelSize, k_textSizeMin, k_textSizeMax);
    showSpin(m_lineSpacing, m_mixedLineSpacing, m_values.lineSpacing, k_lineSpacingMin, k_lineSpacingMax);
    showSpin(m_padding, m_mixedPadding, m_values.padding, k_paddingMin, k_paddingMax);
    // A text object has no outline to keep from. Shown when anything bound has one: a value typed for
    // a set lands on the text objects in it too, where it is carried and ignored.
    m_form->setRowVisible(m_padding, m_anyBalloon);
    showCheck(m_bold, m_mixedBold, m_values.bold);
    showCombo(m_align, m_mixedAlign, m_align->findData(m_values.align));
    if (m_mixedColour)
        paintMixedSwatch(m_swatch, palette());
    else
        paintColourSwatch(m_swatch, m_values.colour);

    // The lettering itself is the one thing a set never shares: five balloons do not share one line.
    m_body->setVisible(m_subjects <= 1 && m_bodyVisible);
    m_showWhitespace->setVisible(m_subjects <= 1 && m_bodyVisible);   // it is the box's, and goes with it
    m_populating = false;
}

}  // namespace StripEdit
