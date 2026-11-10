#include "properties/texteditor.hpp"
#include "geometry.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
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
 * \brief The lettering box, asking for a few lines' height rather than Qt's 192 px.
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

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::TextEditor";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::TextEditor", s)
//! The two paddings, in strip pixels. Negative lets the lettering reach past the outline.
const FieldRowSpec k_paddingRows[] = {
    {"paddingSide", PM_TR("Padding sides"), k_paddingMin, k_paddingMax, " px",
     PM_TR("Room between the balloon's outline and the lettering, left and right.\n"
           "Negative lets the lettering reach past the outline.")},
    {"paddingTop",  PM_TR("Padding top"),   k_paddingMin, k_paddingMax, " px",
     PM_TR("Room above the lettering, and as much below it while it is centred. Each new line grows\n"
           "the lettering up and down alike until it meets this; further lines go on downwards, out of\n"
           "the balloon. Negative lets the lettering reach past the outline.")},
};
//! The letters' outline, in strip pixels; 0 is none.
const FieldRowSpec k_outlineRows[] = {
    {"textOutlineWidth", PM_TR("Outline"), 0, 50, " px",
     PM_TR("A band round each letter, outside it, so the letters keep their weight. 0 is none."), 1},
};
#undef PM_TR

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

    m_paddings.build(this, form, "padding", k_paddingRows, k_trContext, m_values, m_populating,
                     [this] { emit changed(); });

    m_align = new QComboBox(this);
    m_align->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Left"),   int(Qt::AlignLeft));
    m_align->addItem(tr("Right"),  int(Qt::AlignRight));
    form->addRow(tr("Align"), m_align);

    // The letters' paint: a colour, or a halftone over it.
    m_fill.build(this, form, &TextProperties::fill, tr("Colour"), m_values, m_populating,
                 [this] { emit changed(); }, [this] { emit committed(); });

    // The outline: its width, 0 for none, then its paint, shown once there is one.
    m_outlineWidth.build(this, form, "textOutline", k_outlineRows, k_trContext, m_values, m_populating, [this] {
        showOutline();
        emit changed();
    });
    m_outline.build(this, form, &TextProperties::outline, tr("Outline colour"), m_values, m_populating,
                    [this] { emit changed(); }, [this] { emit committed(); });

    // The shadow, the balloon's own rows: solid with no offset it is a second outline, soft a glow.
    m_shadow.build(this, form, &TextProperties::shadow, &TextProperties::shadowFill, tr("Shadow"),
                   tr("The letters with their outline, grown, moved and filled under them. Solid with no\n"
                      "offset it reads as a second outline; soft with no offset, as a glow."),
                   m_values, m_populating, [this] { emit changed(); }, [this] { emit committed(); });

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

    syncFromValues();
}

void TextEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;

    m_values   = TextProperties::from(*subjects.first());
    m_subjects = static_cast<int>(subjects.size());

    m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedAlign = m_mixedLineSpacing = false;
    m_fill.reset();
    m_paddings.reset();
    m_outlineWidth.reset();
    m_outline.reset();
    m_shadow.reset();
    m_anyBalloon   = false;
    m_anyOutline   = false;
    for (const ObjectRecord* a : subjects) {
        const TextProperties t = TextProperties::from(*a);
        m_fill.compare(t, m_values);
        m_mixedFamily = m_mixedFamily || t.family    != m_values.family;
        m_mixedSize   = m_mixedSize   || t.pixelSize != m_values.pixelSize;
        m_mixedBold   = m_mixedBold   || t.bold      != m_values.bold;
        m_mixedAlign  = m_mixedAlign  || t.align     != m_values.align;
        m_mixedLineSpacing = m_mixedLineSpacing || t.lineSpacing != m_values.lineSpacing;
        m_paddings.compare(t, m_values);
        m_outlineWidth.compare(t, m_values);
        m_outline.compare(t, m_values);
        m_shadow.compare(t, m_values);
        m_anyBalloon       = m_anyBalloon       || a->hasSilhouette();
        m_anyOutline       = m_anyOutline       || t.outlineWidth > 0.0;
    }
    m_familyTouched = m_sizeTouched = m_boldTouched = m_alignTouched = false;
    m_lineSpacingTouched = false;

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
    m_fill.applyEdited(m_values, t);
    if (m_familyTouched) t.family    = m_values.family;
    if (m_sizeTouched)   t.pixelSize = m_values.pixelSize;
    if (m_boldTouched)   t.bold      = m_values.bold;
    if (m_alignTouched)  t.align     = m_values.align;
    if (m_lineSpacingTouched) t.lineSpacing = m_values.lineSpacing;
    m_paddings.applyEdited(m_values, t);
    m_outlineWidth.applyEdited(m_values, t);
    m_outline.applyEdited(m_values, t);
    m_shadow.applyEdited(m_values, t);
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
    m_paddings.show(m_values);
    // A text object has no outline to keep from. Shown when anything bound has one: a value typed for
    // a set lands on the text objects in it too, where it is carried and ignored.
    m_paddings.setVisible(m_form, m_anyBalloon && shows(Row::TextSpacing));
    showCheck(m_bold, m_mixedBold, m_values.bold);
    showCombo(m_align, m_mixedAlign, m_align->findData(m_values.align));
    m_fill.show(m_values);
    m_outlineWidth.show(m_values);
    m_outline.show(m_values);
    showOutline();
    m_shadow.show(m_values);

    // The lettering itself is the one thing a set never shares: five balloons do not share one line.
    m_body->setVisible(m_subjects <= 1 && m_bodyVisible);
    m_showWhitespace->setVisible(m_subjects <= 1 && m_bodyVisible);   // it is the box's, and goes with it
    m_populating = false;
}

void TextEditor::showOutline()
{
    // A selection where any has an outline shows its paint, as one where all agree on one does.
    m_outline.setVisible(shows(Row::TextOutline) && (m_anyOutline || m_values.outlineWidth > 0.0));
}

void TextEditor::updateRows()
{
    for (QWidget* w : {static_cast<QWidget*>(m_family), static_cast<QWidget*>(m_size), static_cast<QWidget*>(m_bold)})
        m_form->setRowVisible(w, shows(Row::TextFont));
    m_form->setRowVisible(m_lineSpacing, shows(Row::TextSpacing));
    m_paddings.setVisible(m_form, m_anyBalloon && shows(Row::TextSpacing));
    m_form->setRowVisible(m_align, shows(Row::TextAlign));
    m_fill.setParts(shows(Row::TextColour), shows(Row::TextPattern));
    m_outlineWidth.setVisible(m_form, shows(Row::TextOutline));
    m_outline.setParts(true, shows(Row::TextPattern));
    showOutline();
    m_shadow.setShown(shows(Row::TextShadow));
}

}  // namespace StripEdit
