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

namespace StripEdit {

/**
 * @brief What the balloon says, and how it is set.
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

    void bind(const Subjects& subjects) override;
    void applyTo(ObjectRecord& target) const override;
    void applyEditedTo(ObjectRecord& target) const override;

    [[nodiscard]] const TextProperties& values() const { return m_values; }

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusContent();

    //! Hides the text box alone, for a surface describing an object that does not exist yet.
    void setContentVisible(bool on);

    //! Greys the text box alone: with nothing selected there is nothing to type into, while the
    //! typography still describes what the next object will be.
    void setContentEnabled(bool on);

private:
    void syncFromValues();
    //! The outline's paint rows, shown while anything bound, or the value shown, has an outline.
    void showOutline();

    QPlainTextEdit* m_body   = nullptr;
    QFontComboBox*  m_family = nullptr;
    QSpinBox*       m_size   = nullptr;
    QCheckBox*      m_bold   = nullptr;
    QComboBox*      m_align  = nullptr;
    QSpinBox*       m_lineSpacing = nullptr;
    FieldRows<TextProperties> m_paddings;   //!< Side and top; only while something bound has a balloon to keep from.
    PaintRows<TextProperties> m_fill;           //!< The letters' paint.
    FieldRows<TextProperties> m_outlineWidth;   //!< The letters' outline; 0 is none.
    PaintRows<TextProperties> m_outline;        //!< ...and its paint.
    ShadowRows<TextProperties> m_shadow;        //!< The letters' shadow, the balloon's rows.
    QFormLayout*    m_form = nullptr;
    QCheckBox*      m_showWhitespace = nullptr;   //!< The text box's own view; never the balloon's.

    TextProperties m_values;
    bool m_populating = false;

    // --- bound to a set ---
    int  m_subjects      = 0;
    bool m_mixedFamily   = false;
    bool m_mixedSize     = false;
    bool m_mixedBold     = false;
    bool m_mixedAlign    = false;
    bool m_familyTouched = false;
    bool m_sizeTouched   = false;
    bool m_boldTouched   = false;
    bool m_alignTouched  = false;
    bool m_bodyVisible   = true;   //!< What setContentVisible() was last told; a set hides the box too.
    bool m_mixedLineSpacing   = false;
    bool m_lineSpacingTouched = false;
    bool m_anyBalloon         = true;   //!< Whether the padding rows mean anything for what is bound.
    bool m_anyOutline         = false;  //!< Whether anything bound has an outline (showOutline()).
};

}  // namespace StripEdit

#endif // STRIPEDIT_TEXTEDITOR_HPP
