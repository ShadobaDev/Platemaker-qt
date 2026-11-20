#ifndef STRIPEDIT_TEXTEDITOR_HPP
#define STRIPEDIT_TEXTEDITOR_HPP

#include "properties/fieldrows.hpp"
#include "properties/paintrows.hpp"
#include "properties/shadowrows.hpp"
#include "properties/propertygroupeditor.hpp"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPlainTextEdit;
class QFormLayout;
class QSpinBox;

namespace StripEdit { class MixedSpinBox; }
class QTextCharFormat;
class QSyntaxHighlighter;

namespace StripEdit {

/**
 * \brief What the balloon says, and how it is set.
 *
 * Content and typography in one editor because one person sets both in one pass, and because the split
 * that matters is elsewhere: a **preset** carries the typography and never the content.
 *
 * The colour here is the lettering's, never the balloon's. Editing a frame is not editing its
 * lettering, which is why the colour tool next door has two swatches and this one has a third.
 *
 * Text is edited **here**, not with a caret on the strip. That is a deliberate simplification — an
 * in-scene editor means reimplementing selection, carets and IME on a QGraphicsItem — and it costs
 * nothing in liveness: the strip redraws on every keystroke either way.
 */
class TextEditor : public PropertyGroupEditor
{
    Q_OBJECT

public:
    explicit TextEditor(QWidget* parent = nullptr);

    [[nodiscard]] PropertyGroup group() const override { return PropertyGroup::Text; }
    [[nodiscard]] QList<Row> rows() const override
    {
        return {Row::TextFont,    Row::TextColour,  Row::TextPattern, Row::TextAlign,
                Row::TextSpacing, Row::TextOutline, Row::TextShadow};
    }

    void bind(const Subjects& subjects) override;
    void applyTo(Composition& target) const override;
    void applyEditedTo(Composition& target) const override;

    //! The lettering bound, as edited: the words' own, their body and their runs.
    [[nodiscard]] const TextProperties& values() const { return m_onLetters ? m_lettering : m_values; }

    /**
     * \brief Which words of each composition it speaks for: those of id @p id (a words picked on its own), or,
     *        for 0, every words — their lettering as a set's, the text box only where there is one words.
     */
    void setWords(int id) { m_words = id; }

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusContent();

    //! Hides the text box alone, for a surface describing an object that does not exist yet.
    void setContentVisible(bool on);

    //! Greys the text box alone: with nothing selected there is nothing to type into, while the
    //! typography still describes what the next object will be.
    void setContentEnabled(bool on);

protected:
    void updateRows() override;

private:
    //! What a run may set, one bit each (TextRun's fields).
    enum RunField { RunFamily = 1, RunSize = 2, RunBold = 4, RunItalic = 8, RunFill = 16, RunSpacing = 32 };

    //! The rows shown from m_values, and with @p box the text box from the lettering (its text and runs).
    void syncFromValues(bool box = true);
    //! A row moved: at the caret, the next letters'; on letters selected, theirs; or else the words' own (C41).
    void rowChanged();
    //! The box's caret or selection moved: the rows show the letters selected, the caret's, or the words again.
    void showSelection();
    //! The run fields @p a and @p b differ in (RunField bits).
    [[nodiscard]] static int runFieldsApart(const TextProperties& a, const TextProperties& b);
    //! The format that marks letters with @p fields of @p from as a run's (and shows bold, italic, the colour).
    [[nodiscard]] static QTextCharFormat runFormat(int fields, const TextProperties& from);
    //! @p fields of @p from set on the letters selected in the box, as their run.
    void setOnSelection(int fields, const TextProperties& from);
    //! @p fields taken off every letter in the box: the words' own value is theirs again.
    void clearFromRuns(int fields);
    //! @p base with the box's text and runs, tidy (tidyRuns(): a run over the whole text is the words' own).
    [[nodiscard]] TextProperties fromBox(TextProperties base) const;
    //! How the box shows a letter of format @p f: its family, bold, italic, its size's rank, a run's colour.
    [[nodiscard]] QTextCharFormat lookOf(const QTextCharFormat& f) const;
    //! The box shown again: the sizes' ranks from the lettering now (C41).
    void refreshLook();
    //! @p t's runs put on the box's letters, as formats; the letters in no run plain.
    void putRuns(const TextProperties& t);
    //! The words of @p c this editor speaks for (setWords()).
    [[nodiscard]] QList<const Words*> spokenFor(const Composition& c) const;
    //! @p t with what was touched since the binding written over it.
    void applyEdited(TextProperties& t) const;
    //! The outline's paint rows, shown while anything bound, or the value shown, has an outline.
    void showOutline();

    QPlainTextEdit* m_body   = nullptr;
    QFontComboBox*  m_family = nullptr;
    MixedSpinBox*   m_size   = nullptr;
    QCheckBox*      m_bold   = nullptr;
    QCheckBox*      m_italic = nullptr;
    QComboBox*      m_direction = nullptr;   //!< TextProperties::vertical and columnsLeft as one choice.
    QComboBox*      m_align  = nullptr;
    QComboBox*      m_valign = nullptr;
    QSpinBox*       m_lineSpacing = nullptr;
    QSpinBox*       m_letterSpacing = nullptr;
    QCheckBox*      m_kerning = nullptr;
    PaintRows<TextProperties> m_fill;           //!< The letters' paint.
    FieldRows<TextProperties> m_outlineWidth;   //!< The letters' outline; 0 is none.
    PaintRows<TextProperties> m_outline;        //!< ...and its paint.
    ShadowRows<TextProperties> m_shadow;        //!< The letters' shadow, the balloon's rows.
    QFormLayout*    m_form = nullptr;
    QCheckBox*      m_showWhitespace = nullptr;   //!< The text box's own view; never the balloon's.

    TextProperties m_values;     //!< What the rows show and set: the words', or the letters' (m_onLetters).
    TextProperties m_shown;      //!< m_values as last shown: what a row's change is measured from.
    TextProperties m_lettering;  //!< The words' own while the rows are the letters' (m_values is then theirs).
    bool m_onLetters   = false;  //!< The rows are the letters selected in the box, or the caret's.
    bool m_atCaret     = false;  //!< ...the caret's: what the next letters typed get.
    bool m_caretActive = false;  //!< The author has put the caret in this text (cleared when other text is shown).
    QSyntaxHighlighter* m_look = nullptr;   //!< The box's look (lookOf()).
    QList<int>          m_lookSizes;        //!< The sizes in use, smallest first: lookOf()'s ranks.
    bool m_populating = false;

    // --- bound to a set ---
    int  m_subjects      = 0;      //!< How many words it is bound to: one shows the text box.
    int  m_words         = 0;      //!< See setWords().
    bool m_mixedFamily   = false;
    bool m_mixedSize     = false;
    int  m_leastSize     = 0;       //!< The least and the greatest size bound, or selected: where Mixed steps from.
    int  m_greatestSize  = 0;
    bool m_mixedBold     = false;
    bool m_mixedItalic   = false;
    bool m_mixedAlign    = false;
    bool m_mixedValign   = false;
    bool m_familyTouched = false;
    bool m_sizeTouched   = false;
    bool m_boldTouched   = false;
    bool m_italicTouched = false;
    bool m_alignTouched  = false;
    bool m_valignTouched = false;
    bool m_bodyVisible   = true;   //!< What setContentVisible() was last told; a set hides the box too.
    bool m_mixedLineSpacing   = false;
    bool m_lineSpacingTouched = false;
    bool m_mixedLetterSpacing   = false;
    bool m_letterSpacingTouched = false;
    bool m_mixedKerning         = false;
    bool m_kerningTouched       = false;
    bool m_mixedDirection       = false;
    bool m_directionTouched     = false;
    bool m_anyOutline         = false;  //!< Whether anything bound has an outline (showOutline()).
};

}  // namespace StripEdit

#endif // STRIPEDIT_TEXTEDITOR_HPP
