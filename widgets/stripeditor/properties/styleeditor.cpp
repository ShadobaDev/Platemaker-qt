#include "properties/styleeditor.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <QFrame>
#include <QSpinBox>

#include <cstring>

namespace StripEdit {

/**
 * @brief The core stretch's spin: its lowest value is *Auto*, shown with the value the shape gives.
 *
 * `specialValueText` is already taken by *Mixed* (showSpin()), so Auto is drawn by textFromValue().
 */
class StretchSpin : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;

    //! What Auto means for the bound balloons' shape; -1 when their shapes disagree.
    int shapeStretch = -1;

    //! From Auto, a step goes on from the value Auto shows, not from the -1 that stands for it.
    void stepBy(int steps) override
    {
        if (value() == StyleProperties::k_autoStretch && shapeStretch >= 0) {
            setValue(qBound(0, shapeStretch + steps, maximum()));
            return;
        }
        QSpinBox::stepBy(steps);
    }

protected:
    //! Auto is the minimum, so Qt would grey the down arrow there; it still has somewhere to go.
    StepEnabled stepEnabled() const override
    {
        StepEnabled e = QSpinBox::stepEnabled();
        if (value() == StyleProperties::k_autoStretch && shapeStretch > 0)
            e |= StepDownEnabled;
        return e;
    }

    QString textFromValue(int v) const override
    {
        if (v != StyleProperties::k_autoStretch)
            return QSpinBox::textFromValue(v) + QStringLiteral(" %");
        return shapeStretch < 0 ? StyleEditor::tr("Auto")
                                : StyleEditor::tr("Auto (%1 %)").arg(shapeStretch);
    }

    int valueFromText(const QString& text) const override
    {
        bool      ok = false;
        const int v  = QString(text).remove(QLatin1Char('%')).trimmed().toInt(&ok);
        return ok ? v : StyleProperties::k_autoStretch;
    }

    QValidator::State validate(QString& text, int&) const override
    {
        if (text.startsWith(StyleEditor::tr("Auto")) || text == specialValueText())
            return QValidator::Acceptable;
        bool      ok = false;
        const int v  = QString(text).remove(QLatin1Char('%')).trimmed().toInt(&ok);
        if (!ok)
            return QValidator::Intermediate;
        return v >= minimum() && v <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
    }
};

namespace {

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::StyleEditor";

//! What the panel calls a flash field, and what its spin offers. Lengths in strip pixels.
#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::StyleEditor", s)
const FieldRowSpec k_flashRows[] = {
    {"flashOuterPitch",        PM_TR("Outer spacing:"),  1, 100, " px", PM_TR("One stroke every this many pixels along the outside of the edge.")},
    {"flashInnerPitch",        PM_TR("Inner spacing:"),  1, 100, " px", PM_TR("One stroke every this many pixels along the inside of the edge.")},
    {"flashOuterLength",       PM_TR("Outer length:"),   0, 200, " px", PM_TR("How far an outer stroke reaches out from the edge.")},
    {"flashInnerLength",       PM_TR("Inner length:"),   0, 200, " px", PM_TR("How far an inner stroke reaches in from the edge.")},
    {"flashOuterBase",         PM_TR("Outer width:"),    0, 50,  " px", PM_TR("An outer stroke's width where it leaves the edge.")},
    {"flashInnerBase",         PM_TR("Inner width:"),    0, 50,  " px", PM_TR("An inner stroke's width where it leaves the edge.")},
    {"flashOuterPitchJitter",  PM_TR("Outer scatter:"),  0, 100, " %",  PM_TR("How much each outer stroke's place varies, as a share of the spacing.")},
    {"flashInnerPitchJitter",  PM_TR("Inner scatter:"),  0, 100, " %",  PM_TR("How much each inner stroke's place varies, as a share of the spacing.")},
    {"flashOuterLengthJitter", PM_TR("Outer variance:"), 0, 100, " %",  PM_TR("How much each outer stroke's length varies.")},
    {"flashInnerLengthJitter", PM_TR("Inner variance:"), 0, 100, " %",  PM_TR("How much each inner stroke's length varies.")},
    {"flashCoreStretch",       PM_TR("Direction:"),      StyleProperties::k_autoStretch, 100, "",
     PM_TR("What the strokes point away from: at 0 % the centre; more stretches it into a line along the "
           "longer side, which stands the strokes square to a long straight edge. Auto takes the shape's "
           "own, and follows it when the shape changes; a value set here stays.")},
};

//! The brush's numbers. Its width is the line's own, the *Width* row above them.
const FieldRowSpec k_brushRows[] = {
    {"brushStrokes", PM_TR("Strokes:"), 1, 12,  "",    PM_TR("How many strokes go round the edge.")},
    {"brushOverlap", PM_TR("Overlap:"), 0, 100, " %",  PM_TR("How far each stroke runs past where the next begins, as a share of its length.")},
    {"brushTaper",   PM_TR("Taper:"),   0, 50,  " %",  PM_TR("How much of a stroke, at each end, narrows to its point.")},
    {"brushSwell",   PM_TR("Swell:"),   0, 100, " %",  PM_TR("How much a stroke's width varies along it. Its width is the outline's stroke width.")},
    {"brushDrift",   PM_TR("Drift:"),   0, 20,  " px", PM_TR("How far a stroke wanders off the edge."), 1},
};

//! The dry brush's numbers. Its width is the line's own too, shared among the bristles.
const FieldRowSpec k_drybrushRows[] = {
    {"drybrushCore",     PM_TR("Outline:"),  0, 100, " %", PM_TR("How wide the unbroken line in the middle is, as a share of the width. It keeps the fill apart from what is round it; the hairs share the rest.")},
    {"drybrushBristles", PM_TR("Bristles:"), 1, 40,  "",   PM_TR("How many hairs across the line, half each side of its thin core: more makes them finer.")},
    {"drybrushDryness",  PM_TR("Dryness:"),  0, 100, " %", PM_TR("How dry the brush is: the share of gaps in the hairs by the core. They thin out away from it; the core always stays.")},
};

#undef PM_TR

} // namespace

StyleEditor::StyleEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    // The line's kind, its colour and width, then the kind's own rows. A field added to the list and not
    // given a row above stops a debug build here.
    m_line = new QComboBox(this);
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i)
        m_line->addItem(lineTitle(static_cast<ObjectRecord::Line>(i)), i);
    m_line->setToolTip(tr("How the edge is drawn: a solid line, a flash's strokes ringing it, a few "
                          "brush strokes laid along it, or a dry brush: a thin line with hairs thinning out either side."));
    m_form->addRow(tr("Line:"), m_line);
    m_strokeWidth = new QSpinBox(this);
    m_strokeWidth->setRange(k_strokeWidthMin, k_strokeWidthMax);
    m_strokeWidth->setSuffix(tr(" px"));
    m_strokeWidth->setToolTip(tr("How wide the outline is, and a brush's strokes. 0 draws no line."));
    m_form->addRow(tr("Width:"), m_strokeWidth);
    // The line's paint: the outline's, or a flash's or a brush's strokes'.
    m_stroke.build(this, m_form, &StyleProperties::stroke, tr("Colour:"), m_values, m_populating,
                   [this] { emit changed(); }, [this] { emit committed(); });
    m_flash.build(this, m_form, "flash", k_flashRows, k_trContext, m_values, m_populating,
                  [this] { emit changed(); }, [this](const char* key) -> QSpinBox* {
                      if (std::strcmp(key, "flashCoreStretch") != 0)
                          return nullptr;
                      return m_stretch = new StretchSpin(this);
                  });
    m_brush.build(this, m_form, "brush", k_brushRows, k_trContext, m_values, m_populating,
                  [this] { emit changed(); });
    m_drybrush.build(this, m_form, "drybrush", k_drybrushRows, k_trContext, m_values, m_populating,
                     [this] { emit changed(); });

    connect(m_line, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.line = static_cast<ObjectRecord::Line>(m_line->currentData().toInt());
        m_lineTouched = true;
        m_mixedLine   = false;
        showRows();
        emit changed();
    });
    connect(m_strokeWidth, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_strokeWidth, k_strokeWidthMin))
            m_mixedWidth = false;   // they have taken a position, so the sentinel goes
        m_values.strokeWidth = v;
        m_widthTouched       = true;
        emit changed();
    });


    syncFromValues();
}

void StyleEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = StyleProperties::from(*subjects.first());

    m_mixedLine = m_mixedWidth = false;
    m_stroke.reset();
    m_flash.reset();
    m_brush.reset();
    m_drybrush.reset();
    bool sameShape = true;
    for (const ObjectRecord* a : subjects) {
        const StyleProperties v = StyleProperties::from(*a);
        m_mixedLine   = m_mixedLine   || v.line        != m_values.line;
        m_stroke.compare(v, m_values);
        m_mixedWidth  = m_mixedWidth  || v.strokeWidth != m_values.strokeWidth;
        m_flash.compare(v, m_values);
        m_brush.compare(v, m_values);
        m_drybrush.compare(v, m_values);
        sameShape = sameShape && a->shape.kind == subjects.first()->shape.kind;
    }
    m_lineTouched = m_widthTouched = false;
    if (m_stretch)
        m_stretch->shapeStretch = sameShape ? Geometry::shapeInfo(subjects.first()->shape.kind).flashStretch : -1;

    syncFromValues();
}

void StyleEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void StyleEditor::applyEditedTo(ObjectRecord& target) const
{
    StyleProperties t = StyleProperties::from(target);
    if (m_lineTouched)   t.line        = m_values.line;
    m_stroke.applyEdited(m_values, t);
    if (m_widthTouched)  t.strokeWidth = m_values.strokeWidth;
    m_flash.applyEdited(m_values, t);
    m_brush.applyEdited(m_values, t);
    m_drybrush.applyEdited(m_values, t);
    t.applyTo(target);
}

void StyleEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_line, m_mixedLine, m_line->findData(int(m_values.line)));
    showSpin(m_strokeWidth, m_mixedWidth, m_values.strokeWidth, k_strokeWidthMin, k_strokeWidthMax);
    m_stroke.show(m_values);
    m_flash.show(m_values);
    m_brush.show(m_values);
    m_drybrush.show(m_values);
    showRows();
    m_populating = false;
}

void StyleEditor::showRows()
{
    // A selection that disagrees shows the rows at least one of them has.
    m_flash.setVisible(m_form, m_mixedLine || m_values.line == ObjectRecord::Line::Flash);
    m_brush.setVisible(m_form, m_mixedLine || m_values.line == ObjectRecord::Line::Brush);
    m_drybrush.setVisible(m_form, m_mixedLine || m_values.line == ObjectRecord::Line::Drybrush);
}

}  // namespace StripEdit
