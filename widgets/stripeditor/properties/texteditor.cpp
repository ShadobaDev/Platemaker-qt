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
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <tuple>
#include <utility>

namespace StripEdit {

namespace {

//! Lines of lettering the box asks room for; it grows past them when the panel has room to spare.
constexpr int k_bodyLines = 4;

//! The Direction combo's entries: TextProperties::vertical and columnsLeft as one choice.
enum Direction { Across, DownColumnsRight, DownColumnsLeft };

Direction directionOf(const TextProperties& t)
{
    return !t.vertical ? Across : t.columnsLeft ? DownColumnsLeft : DownColumnsRight;
}

void setDirection(TextProperties& t, int d)
{
    t.vertical    = d != Across;
    t.columnsLeft = d == DownColumnsLeft;
}

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

/**
 * @name Where a run's overrides ride on the box's letters (TextEditor::putRuns()): character format properties
 *       that nothing draws (the box's look is LetteringLook's).
 * @{
 */
constexpr int k_runFamily = QTextFormat::UserProperty;
constexpr int k_runSize   = QTextFormat::UserProperty + 1;
constexpr int k_runBold   = QTextFormat::UserProperty + 2;
constexpr int k_runItalic = QTextFormat::UserProperty + 3;
constexpr int k_runFill   = QTextFormat::UserProperty + 4;
constexpr int k_runSpacing = QTextFormat::UserProperty + 5;
/** @} */

//! @p base with what @p f, a letter's format in the box, overrides.
TextProperties withFormat(TextProperties base, const QTextCharFormat& f)
{
    if (f.hasProperty(k_runFamily))
        base.family = f.stringProperty(k_runFamily);
    if (f.hasProperty(k_runSize))
        base.pixelSize = f.intProperty(k_runSize);
    if (f.hasProperty(k_runBold))
        base.bold = f.boolProperty(k_runBold);
    if (f.hasProperty(k_runItalic))
        base.italic = f.boolProperty(k_runItalic);
    if (f.hasProperty(k_runFill))
        base.fill = f.property(k_runFill).value<Paint>();
    if (f.hasProperty(k_runSpacing))
        base.letterSpacing = f.intProperty(k_runSpacing);
    return base;
}

//! Every fragment of @p doc that has letters in [@p from, @p to), with where it starts and ends there.
template <class F>
void eachFragment(const QTextDocument& doc, int from, int to, F&& f)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int           start    = qMax(from, fragment.position());
            const int           end      = qMin(to, fragment.position() + fragment.length());
            if (start < end)
                f(fragment.charFormat(), start, end);
        }
}

/**
 * \brief How the box shows its letters (TextEditor::lookOf()): a look over the document, never in it, so it is
 *        neither text, nor a run, nor a step to undo.
 */
class LetteringLook : public QSyntaxHighlighter
{
public:
    LetteringLook(QTextDocument* doc, std::function<QTextCharFormat(const QTextCharFormat&)> look)
        : QSyntaxHighlighter(doc), m_look(std::move(look))
    {
    }

protected:
    void highlightBlock(const QString&) override
    {
        const QTextBlock b = currentBlock();
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            setFormat(fragment.position() - b.position(), fragment.length(), m_look(fragment.charFormat()));
        }
    }

private:
    std::function<QTextCharFormat(const QTextCharFormat&)> m_look;
};

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::TextEditor";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::TextEditor", s)
//! The letters' outline, in strip pixels; 0 is none.
const Fields::RowSpec k_outlineRows[] = {
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
    m_look = new LetteringLook(m_body->document(), [this](const QTextCharFormat& f) { return lookOf(f); });

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

    m_size = new MixedSpinBox(this);
    m_size->setRange(k_textSizeMin, k_textSizeMax);
    m_size->setSuffix(tr(" px"));
    // Strip-scale pixels: the same number the render uses, so a size chosen here means the same thing
    // in the output. It is not a point size and does not follow the screen's DPI.
    m_size->setToolTip(tr("Height in output pixels, at the project's target width."));
    form->addRow(tr("Size"), m_size);

    m_bold = new QCheckBox(tr("Bold"), this);
    form->addRow(QString(), m_bold);

    m_italic = new QCheckBox(tr("Italic"), this);
    form->addRow(QString(), m_italic);

    m_lineSpacing = new QSpinBox(this);
    m_lineSpacing->setRange(k_lineSpacingMin, k_lineSpacingMax);
    m_lineSpacing->setSingleStep(5);
    m_lineSpacing->setSuffix(tr(" %"));
    m_lineSpacing->setToolTip(tr("Distance between lines, as a percentage of the font's own."));
    form->addRow(tr("Line spacing"), m_lineSpacing);

    m_letterSpacing = new QSpinBox(this);
    m_letterSpacing->setRange(k_letterSpacingMin, k_letterSpacingMax);
    m_letterSpacing->setSingleStep(10);
    m_letterSpacing->setSuffix(tr(" \u2030"));
    m_letterSpacing->setToolTip(tr("Room between letters, in thousandths of the font size: 0 is the font's own,\n"
                                   "negative sets them closer. Of letters selected, theirs alone."));
    form->addRow(tr("Letter spacing"), m_letterSpacing);

    m_kerning = new QCheckBox(tr("Kerning"), this);
    m_kerning->setToolTip(tr("Set pairs of letters as the font's own kerning says. Off, each letter takes\n"
                             "its own width: for a font whose kerning is poor."));
    form->addRow(QString(), m_kerning);

    m_direction = new QComboBox(this);
    m_direction->addItem(tr("Across"), int(Across));
    m_direction->addItem(tr("Down, columns to the right"), int(DownColumnsRight));
    m_direction->addItem(tr("Down, columns to the left"), int(DownColumnsLeft));
    m_direction->setToolTip(tr("Lines across, or each line a column, its letters upright one under the other."));
    form->addRow(tr("Direction"), m_direction);

    m_align = new QComboBox(this);
    m_align->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Left"),   int(Qt::AlignLeft));
    m_align->addItem(tr("Right"),  int(Qt::AlignRight));
    form->addRow(tr("Align"), m_align);

    m_valign = new QComboBox(this);
    m_valign->addItem(tr("Middle"), int(Qt::AlignVCenter));
    m_valign->addItem(tr("Top"),    int(Qt::AlignTop));
    m_valign->addItem(tr("Bottom"), int(Qt::AlignBottom));
    m_valign->setToolTip(tr("Where the lines sit in their frame, up and down."));
    form->addRow(tr("Vertical"), m_valign);

    m_warp = new QCheckBox(tr("Warp"), this);
    // The words' state, not a switch: a warp is made on the strip, by bending the curve edit mode offers.
    m_warp->setEnabled(false);
    m_warp->setToolTip(tr("Whether the lettering is bent along a curve. Double-click the text to edit it: drag\n"
                          "the curve through its middle to bend it, double-click a node to straighten it."));
    form->addRow(QString(), m_warp);

    // The letters' paint: a colour, or a halftone over it.
    m_fill.build(this, form, &TextProperties::fill, tr("Colour"), m_values, m_populating,
                 [this] { rowChanged(); }, [this] { emit committed(); });

    // The outline: its width, 0 for none, then its paint, shown once there is one.
    m_outlineWidth.build(this, form, "textOutline", k_outlineRows, k_trContext, m_values, m_populating, [this] {
        showOutline();
        rowChanged();
    });
    m_outline.build(this, form, &TextProperties::outline, tr("Outline colour"), m_values, m_populating,
                    [this] { rowChanged(); }, [this] { emit committed(); });

    // The shadow, the balloon's own rows: solid with no offset it is a second outline, soft a glow.
    m_shadow.build(this, form, &TextProperties::shadow, &TextProperties::shadowFill, tr("Shadow"),
                   tr("The letters with their outline, grown, moved and filled under them. Solid with no\n"
                      "offset it reads as a second outline; soft with no offset, as a glow."),
                   m_values, m_populating, [this] { rowChanged(); }, [this] { emit committed(); });

    // Each control records that *it* was the one moved. Bound to a set, only what was moved is written,
    // and everything else stays each object's own — see applyEditedTo().
    // The runs ride on the letters as formats, so typing carries them along: the body and the runs are read back
    // together.
    const auto bodyEdited = [this] {
        if (m_populating)
            return;
        TextProperties& lettering = m_onLetters ? m_lettering : m_values;
        lettering                 = fromBox(lettering);
        if (!m_onLetters)
            m_shown = m_values;
        refreshLook();
        emit changed();
    };
    connect(m_body, &QPlainTextEdit::textChanged, this, bodyEdited);
    connect(m_body, &QPlainTextEdit::selectionChanged, this, [this] { showSelection(); });
    // Once the author has put the caret in the box, it is the caret's lettering the rows show and set.
    connect(m_body, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_populating)
            return;
        m_caretActive = true;
        showSelection();
    });

    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (m_populating)
            return;
        m_values.family = f.family();
        m_familyTouched = true;
        m_mixedFamily   = false;
        rowChanged();
    });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_size, k_textSizeMin))
            m_mixedSize = false;
        m_values.pixelSize = v;
        m_sizeTouched      = true;
        rowChanged();
    });
    connect(m_lineSpacing, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_lineSpacing, k_lineSpacingMin))
            m_mixedLineSpacing = false;
        m_values.lineSpacing = v;
        m_lineSpacingTouched = true;
        rowChanged();
    });
    connect(m_letterSpacing, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_letterSpacing, k_letterSpacingMin))
            m_mixedLetterSpacing = false;
        m_values.letterSpacing = v;
        m_letterSpacingTouched = true;
        rowChanged();
    });
    connect(m_kerning, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_kerning->setTristate(false);
        m_values.kerning = on;
        m_kerningTouched = true;
        m_mixedKerning   = false;
        rowChanged();
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_bold->setTristate(false);   // they have taken a position; there is no third state to return to
        m_values.bold = on;
        m_boldTouched = true;
        m_mixedBold   = false;
        rowChanged();
    });
    connect(m_italic, &QCheckBox::toggled, this, [this](bool on) {
        if (m_populating)
            return;
        m_italic->setTristate(false);
        m_values.italic = on;
        m_italicTouched = true;
        m_mixedItalic   = false;
        rowChanged();
    });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.align = m_align->currentData().toInt();
        m_alignTouched = true;
        m_mixedAlign   = false;
        rowChanged();
    });
    connect(m_direction, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        setDirection(m_values, m_direction->currentData().toInt());
        m_directionTouched = true;
        m_mixedDirection   = false;
        rowChanged();
    });
    connect(m_valign, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.valign = m_valign->currentData().toInt();
        m_valignTouched = true;
        m_mixedValign   = false;
        rowChanged();
    });

    syncFromValues();
}

void TextEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;

    QList<TextProperties> texts;
    for (const Composition* a : subjects)
        for (const Words* w : spokenFor(*a))
            texts << w->text;
    if (texts.isEmpty())
        texts << TextProperties{};   // nothing lettered yet: the defaults, as for an empty composition
    m_values    = texts.first();
    m_subjects  = static_cast<int>(texts.size());
    m_onLetters = false;
    m_atCaret   = false;

    m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedItalic = m_mixedAlign = m_mixedValign = false;
    m_mixedLineSpacing = m_mixedLetterSpacing = m_mixedKerning = m_mixedDirection = m_mixedWarp = false;
    m_fill.reset();
    m_leastSize = m_greatestSize = m_values.pixelSize;
    m_outlineWidth.reset();
    m_outline.reset();
    m_shadow.reset();
    m_anyOutline   = false;
    for (const TextProperties& t : std::as_const(texts)) {
        m_fill.compare(t, m_values);
        m_mixedFamily = m_mixedFamily || t.family    != m_values.family;
        m_mixedSize   = m_mixedSize   || t.pixelSize != m_values.pixelSize;
        m_leastSize    = qMin(m_leastSize, t.pixelSize);
        m_greatestSize = qMax(m_greatestSize, t.pixelSize);
        m_mixedBold   = m_mixedBold   || t.bold      != m_values.bold;
        m_mixedItalic = m_mixedItalic || t.italic    != m_values.italic;
        m_mixedAlign  = m_mixedAlign  || t.align     != m_values.align;
        m_mixedValign = m_mixedValign || t.valign    != m_values.valign;
        m_mixedLineSpacing = m_mixedLineSpacing || t.lineSpacing != m_values.lineSpacing;
        m_mixedLetterSpacing = m_mixedLetterSpacing || t.letterSpacing != m_values.letterSpacing;
        m_mixedKerning       = m_mixedKerning || t.kerning != m_values.kerning;
        m_mixedDirection     = m_mixedDirection || directionOf(t) != directionOf(m_values);
        m_mixedWarp          = m_mixedWarp || t.warp.isEmpty() != m_values.warp.isEmpty();
        m_outlineWidth.compare(t, m_values);
        m_outline.compare(t, m_values);
        m_shadow.compare(t, m_values);
        m_anyOutline       = m_anyOutline       || t.outlineWidth > 0.0;
    }
    m_familyTouched = m_sizeTouched = m_boldTouched = m_italicTouched = m_alignTouched = m_valignTouched = false;
    m_lineSpacingTouched = m_letterSpacingTouched = m_kerningTouched = m_directionTouched = false;

    syncFromValues();
    showSelection();   // letters still selected in the box are the rows' subject again, after a rebinding
}

QList<const Words*> TextEditor::spokenFor(const Composition& c) const
{
    QList<const Words*> out;
    for (const Words& w : c.words)
        if (m_words == 0 || w.id == m_words)
            out << &w;
    return out;
}

void TextEditor::applyTo(Composition& target) const
{
    // Several words are a set: each keeps its own line, and takes what was touched.
    if (spokenFor(target).size() > 1) {
        applyEditedTo(target);
        return;
    }
    for (Words& w : target.words)
        if (m_words == 0 || w.id == m_words) {
            w.text = values();
            return;
        }
}

void TextEditor::applyEditedTo(Composition& target) const
{
    for (Words& w : target.words)
        if (m_words == 0 || w.id == m_words)
            applyEdited(w.text);
}

void TextEditor::applyEdited(TextProperties& t) const
{
    // The lettering itself is never written to a set: five balloons do not share one line of dialogue.
    m_fill.applyEdited(m_values, t);
    if (m_familyTouched) t.family    = m_values.family;
    if (m_sizeTouched)   t.pixelSize = m_values.pixelSize;
    if (m_boldTouched)   t.bold      = m_values.bold;
    if (m_italicTouched) t.italic    = m_values.italic;
    if (m_alignTouched)  t.align     = m_values.align;
    if (m_valignTouched) t.valign    = m_values.valign;
    if (m_lineSpacingTouched) t.lineSpacing = m_values.lineSpacing;
    if (m_letterSpacingTouched) t.letterSpacing = m_values.letterSpacing;
    if (m_kerningTouched) t.kerning = m_values.kerning;
    if (m_directionTouched) setDirection(t, directionOf(m_values));
    m_outlineWidth.applyEdited(m_values, t);
    m_outline.applyEdited(m_values, t);
    m_shadow.applyEdited(m_values, t);
    // What was set for them all is each words' own, whole (C40): their runs no longer set it.
    for (TextRun& r : t.runs) {
        if (m_familyTouched)
            r.family.reset();
        if (m_sizeTouched)
            r.pixelSize.reset();
        if (m_boldTouched)
            r.bold.reset();
        if (m_italicTouched)
            r.italic.reset();
        if (m_letterSpacingTouched)
            r.letterSpacing.reset();
    }   // a run's fill stays: the fill's rows say nothing of having been touched
    tidyRuns(t);
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

void TextEditor::syncFromValues(bool box)
{
    m_populating = true;
    {
        const QSignalBlocker b1(m_body), b2(m_family);

        // Guarded: setPlainText resets the caret, and the selection the rows may be showing.
        const TextProperties& lettering = values();
        if (box && m_body->toPlainText() != lettering.body) {
            m_body->setPlainText(lettering.body);
            m_caretActive = false;   // other text: the caret has not been put in it yet
        }
        // The whole box, not only its runs: one format over all the letters reads back as the words' own font,
        // no run, and so would match words with no runs while the box still held another object's font.
        if (box && fromBox(lettering) != lettering)
            putRuns(lettering);

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
    m_size->setSpread(m_leastSize, m_greatestSize);
    showSpin(m_size, m_mixedSize, m_values.pixelSize, k_textSizeMin, k_textSizeMax);
    showSpin(m_lineSpacing, m_mixedLineSpacing, m_values.lineSpacing, k_lineSpacingMin, k_lineSpacingMax);
    showSpin(m_letterSpacing, m_mixedLetterSpacing, m_values.letterSpacing, k_letterSpacingMin, k_letterSpacingMax);
    // A text object has no outline to keep from. Shown when anything bound has one: a value typed for
    // a set lands on the text objects in it too, where it is carried and ignored.
    showCheck(m_bold, m_mixedBold, m_values.bold);
    showCheck(m_italic, m_mixedItalic, m_values.italic);
    showCheck(m_kerning, m_mixedKerning, m_values.kerning);
    showCombo(m_direction, m_mixedDirection, m_direction->findData(int(directionOf(m_values))));
    showCombo(m_align, m_mixedAlign, m_align->findData(m_values.align));
    showCombo(m_valign, m_mixedValign, m_valign->findData(m_values.valign));
    showCheck(m_warp, m_mixedWarp, !m_values.warp.isEmpty());
    m_fill.show(m_values);
    m_outlineWidth.show(m_values);
    m_outline.show(m_values);
    showOutline();
    m_shadow.show(m_values);

    // The lettering itself is the one thing a set never shares: five balloons do not share one line.
    m_body->setVisible(m_subjects <= 1 && m_bodyVisible);
    m_showWhitespace->setVisible(m_subjects <= 1 && m_bodyVisible);   // it is the box's, and goes with it
    m_shown      = m_values;
    m_populating = false;
    refreshLook();
}

QTextCharFormat TextEditor::runFormat(int fields, const TextProperties& from)
{
    QTextCharFormat f;
    if (fields & RunFamily)
        f.setProperty(k_runFamily, from.family);
    if (fields & RunSize)
        f.setProperty(k_runSize, from.pixelSize);
    if (fields & RunBold)
        f.setProperty(k_runBold, from.bold);
    if (fields & RunItalic)
        f.setProperty(k_runItalic, from.italic);
    if (fields & RunFill)
        f.setProperty(k_runFill, QVariant::fromValue(from.fill));
    if (fields & RunSpacing)
        f.setProperty(k_runSpacing, from.letterSpacing);
    return f;
}

void TextEditor::rowChanged()
{
    const int runFields = runFieldsApart(m_shown, m_values);
    if (m_onLetters) {
        // At the caret, what was set is the lettering of the letters typed next: nothing changes until one is.
        // On letters selected, it is their run.
        if (m_atCaret) {
            if (runFields != 0)
                m_body->mergeCurrentCharFormat(runFormat(runFields, m_values));
        } else {
            setOnSelection(runFields, m_values);
        }
        // The rest of the lettering (alignment, spacing, outline, shadow) is the words' own, set from the rows.
        TextProperties next = m_values;
        next.body           = m_lettering.body;
        next.family         = m_lettering.family;
        next.pixelSize      = m_lettering.pixelSize;
        next.bold           = m_lettering.bold;
        next.italic         = m_lettering.italic;
        next.fill           = m_lettering.fill;
        next.letterSpacing  = m_lettering.letterSpacing;
        next                = fromBox(next);
        const bool same     = next == m_lettering;
        m_lettering         = next;
        m_shown             = m_values;
        refreshLook();
        if (!same)
            emit changed();
        return;
    }
    if (m_subjects <= 1 && runFields != 0) {
        // The words as the panel bound them, the box not yet used: what was set is theirs, whole.
        clearFromRuns(runFields);
        m_values = fromBox(m_values);
    }
    m_shown = m_values;
    refreshLook();
    emit changed();
}

void TextEditor::showSelection()
{
    if (m_populating)
        return;
    const QTextCursor cursor   = m_body->textCursor();
    const bool        one      = m_subjects <= 1;
    const bool        selected = one && cursor.hasSelection();
    const bool        atCaret  = one && !selected && m_caretActive;
    if (!selected && !atCaret) {
        if (!m_onLetters)
            return;
        m_onLetters = false;
        m_atCaret   = false;
        m_values    = m_lettering;
        m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedItalic = m_mixedLetterSpacing = false;
        m_fill.reset();
        m_leastSize = m_greatestSize = m_values.pixelSize;
        syncFromValues(false);
        return;
    }
    if (!m_onLetters) {
        m_lettering = m_values;
        m_onLetters = true;
    }
    m_atCaret = atCaret;
    // Each letter as the lettering it is drawn in: the rows show the first, Mixed where they differ. At the caret,
    // the lettering the next letter typed gets.
    QList<TextProperties> parts;
    if (selected)
        eachFragment(*m_body->document(), cursor.selectionStart(), cursor.selectionEnd(),
                     [&](const QTextCharFormat& f, int, int) { parts << withFormat(m_lettering, f); });
    else
        parts << withFormat(m_lettering, m_body->currentCharFormat());
    m_values = parts.isEmpty() ? m_lettering : parts.first();
    m_mixedFamily = m_mixedSize = m_mixedBold = m_mixedItalic = m_mixedLetterSpacing = false;
    m_fill.reset();
    m_leastSize = m_greatestSize = m_values.pixelSize;
    for (const TextProperties& p : std::as_const(parts)) {
        m_leastSize    = qMin(m_leastSize, p.pixelSize);
        m_greatestSize = qMax(m_greatestSize, p.pixelSize);
        m_mixedFamily = m_mixedFamily || p.family != m_values.family;
        m_mixedSize   = m_mixedSize || p.pixelSize != m_values.pixelSize;
        m_mixedBold   = m_mixedBold || p.bold != m_values.bold;
        m_mixedItalic = m_mixedItalic || p.italic != m_values.italic;
        m_mixedLetterSpacing = m_mixedLetterSpacing || p.letterSpacing != m_values.letterSpacing;
        m_fill.compare(p, m_values);
    }
    // The rows only: the box is where the text comes from, and a caret moved by a letter typed arrives before the
    // letter is read back (QPlainTextEdit::cursorPositionChanged comes before textChanged).
    syncFromValues(false);
}

int TextEditor::runFieldsApart(const TextProperties& a, const TextProperties& b)
{
    return (a.family != b.family ? RunFamily : 0) | (a.pixelSize != b.pixelSize ? RunSize : 0)
         | (a.bold != b.bold ? RunBold : 0) | (a.italic != b.italic ? RunItalic : 0) | (a.fill != b.fill ? RunFill : 0)
         | (a.letterSpacing != b.letterSpacing ? RunSpacing : 0);
}

void TextEditor::setOnSelection(int fields, const TextProperties& from)
{
    if (fields == 0)
        return;
    const QSignalBlocker quiet(m_body);   // the runs are read back by the caller, once
    QTextCursor          cursor = m_body->textCursor();
    cursor.mergeCharFormat(runFormat(fields, from));
}

void TextEditor::clearFromRuns(int fields)
{
    QTextDocument* doc = m_body->document();
    QList<std::tuple<QTextCharFormat, int, int>> fragments;
    eachFragment(*doc, 0, doc->characterCount(), [&](const QTextCharFormat& f, int from, int to) {
        fragments.append({f, from, to});
    });
    const QSignalBlocker quiet(m_body);
    for (auto [f, from, to] : fragments) {
        for (const auto& [field, property] : {std::pair{RunFamily, k_runFamily}, std::pair{RunSize, k_runSize},
                                              std::pair{RunBold, k_runBold}, std::pair{RunItalic, k_runItalic},
                                              std::pair{RunFill, k_runFill}, std::pair{RunSpacing, k_runSpacing}})
            if (fields & field)
                f.clearProperty(property);
        QTextCursor c(doc);
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        c.setCharFormat(f);
    }
}

TextProperties TextEditor::fromBox(TextProperties base) const
{
    base.body = m_body->toPlainText();
    base.runs.clear();
    const QTextDocument* doc = m_body->document();
    eachFragment(*doc, 0, doc->characterCount(), [&](const QTextCharFormat& f, int from, int to) {
        TextRun r;
        r.from   = from;
        r.length = to - from;
        if (f.hasProperty(k_runFamily))
            r.family = f.stringProperty(k_runFamily);
        if (f.hasProperty(k_runSize))
            r.pixelSize = f.intProperty(k_runSize);
        if (f.hasProperty(k_runBold))
            r.bold = f.boolProperty(k_runBold);
        if (f.hasProperty(k_runItalic))
            r.italic = f.boolProperty(k_runItalic);
        if (f.hasProperty(k_runFill))
            r.fill = f.property(k_runFill).value<Paint>();
        if (f.hasProperty(k_runSpacing))
            r.letterSpacing = f.intProperty(k_runSpacing);
        if (r.overrides())
            base.runs.append(r);
    });
    tidyRuns(base);
    return base;
}

void TextEditor::putRuns(const TextProperties& t)
{
    const QSignalBlocker quiet(m_body);
    QTextCursor          all(m_body->document());
    all.select(QTextCursor::Document);
    all.setCharFormat(QTextCharFormat());
    for (const TextRun& r : t.runs) {
        TextProperties with = t;
        with.family         = r.family.value_or(t.family);
        with.pixelSize      = r.pixelSize.value_or(t.pixelSize);
        with.bold           = r.bold.value_or(t.bold);
        with.italic         = r.italic.value_or(t.italic);
        with.fill           = r.fill.value_or(t.fill);
        with.letterSpacing  = r.letterSpacing.value_or(t.letterSpacing);
        const int fields = (r.family ? RunFamily : 0) | (r.pixelSize ? RunSize : 0) | (r.bold ? RunBold : 0)
                         | (r.italic ? RunItalic : 0) | (r.fill ? RunFill : 0) | (r.letterSpacing ? RunSpacing : 0);
        QTextCursor run(m_body->document());
        run.setPosition(r.from);
        run.setPosition(r.from + r.length, QTextCursor::KeepAnchor);
        run.mergeCharFormat(runFormat(fields, with));
    }
}

QTextCharFormat TextEditor::lookOf(const QTextCharFormat& f) const
{
    const TextProperties t = withFormat(values(), f);
    QFont                font = m_body->font();
    if (!t.family.isEmpty())
        font.setFamily(t.family);
    // A rank, not the size: the sizes in use, smallest first, one pixel apart from the box's own.
    const auto rank = std::lower_bound(m_lookSizes.cbegin(), m_lookSizes.cend(), t.pixelSize) - m_lookSizes.cbegin();
    font.setPixelSize(QFontInfo(m_body->font()).pixelSize() + int(rank));
    font.setBold(t.bold);
    font.setItalic(t.italic);
    QTextCharFormat look;
    look.setFont(font);
    if (f.hasProperty(k_runFill))   // a run's colour only: the words' own may be the box's white
        look.setForeground(t.fill.colour);
    return look;
}

void TextEditor::refreshLook()
{
    const TextProperties& t = values();
    QList<int>            sizes{t.pixelSize};
    for (const TextRun& r : t.runs)
        if (r.pixelSize)
            sizes << *r.pixelSize;
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    m_lookSizes = sizes;
    const QSignalBlocker quiet(m_body);
    m_look->rehighlight();
}

void TextEditor::showOutline()
{
    // A selection where any has an outline shows its paint, as one where all agree on one does.
    m_outline.setVisible(shows(Row::TextOutline) && (m_anyOutline || m_values.outlineWidth > 0.0));
}

void TextEditor::updateRows()
{
    for (QWidget* w : {static_cast<QWidget*>(m_family), static_cast<QWidget*>(m_size), static_cast<QWidget*>(m_bold),
                       static_cast<QWidget*>(m_italic)})
        m_form->setRowVisible(w, shows(Row::TextFont));
    m_form->setRowVisible(m_lineSpacing, shows(Row::TextSpacing));
    m_form->setRowVisible(m_letterSpacing, shows(Row::TextSpacing));
    m_form->setRowVisible(m_kerning, shows(Row::TextSpacing));
    m_form->setRowVisible(m_direction, shows(Row::TextAlign));
    m_form->setRowVisible(m_align, shows(Row::TextAlign));
    m_form->setRowVisible(m_valign, shows(Row::TextAlign));
    m_form->setRowVisible(m_warp, shows(Row::TextAlign));
    m_fill.setParts(shows(Row::TextColour), shows(Row::TextPattern));
    m_outlineWidth.setVisible(m_form, shows(Row::TextOutline));
    m_outline.setParts(true, shows(Row::TextPattern));
    showOutline();
    m_shadow.setShown(shows(Row::TextShadow));
}

}  // namespace StripEdit
