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
 * \brief The core stretch's spin: its lowest value is *Auto*, shown with the value the shape gives.
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
        if (value() == FlashParams::k_autoStretch && shapeStretch >= 0) {
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
        if (value() == FlashParams::k_autoStretch && shapeStretch > 0)
            e |= StepDownEnabled;
        return e;
    }

    QString textFromValue(int v) const override
    {
        if (v != FlashParams::k_autoStretch)
            return QSpinBox::textFromValue(v) + QStringLiteral(" %");
        return shapeStretch < 0 ? StyleEditor::tr("Auto")
                                : StyleEditor::tr("Auto (%1 %)").arg(shapeStretch);
    }

    int valueFromText(const QString& text) const override
    {
        bool      ok = false;
        const int v  = QString(text).remove(QLatin1Char('%')).trimmed().toInt(&ok);
        return ok ? v : FlashParams::k_autoStretch;
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

StyleEditor::StyleEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    // The line's kind, its colour and width, then the kinds' own rows. A section's number with no row in its
    // section() (styles.cpp) stops a debug build here.
    m_line = new QComboBox(this);
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i)
        m_line->addItem(lineTitle(static_cast<ObjectRecord::Line>(i)), i);
    m_line->setToolTip(tr("How the edge is drawn: a solid line, a flash's strokes ringing it, a few "
                          "brush strokes laid along it, a dry brush: a thin line with hairs thinning out either side, "
                          "dashes, a whisper, several lines, one inside the other, a sketch: short pen strokes, "
                          "dry ink: the line eaten by the paper, or an explosion: a dry brush's ring in a halo "
                          "of hairs, the flash's outer strokes made thin and dense."));
    m_form->addRow(tr("Line:"), m_line);
    m_strokeWidth = new QSpinBox(this);
    m_strokeWidth->setRange(k_strokeWidthMin, k_strokeWidthMax);
    m_strokeWidth->setSuffix(tr(" px"));
    m_strokeWidth->setToolTip(tr("How wide the outline is, and a brush's strokes. 0 draws no line."));
    m_form->addRow(tr("Width:"), m_strokeWidth);
    // The line's paint: the outline's, or a flash's or a brush's strokes'.
    m_stroke.build(this, m_form, &StyleProperties::stroke, tr("Line colour:"), m_values, m_populating,
                   [this] { emit changed(); }, [this] { emit committed(); });
    // Every kind's numbers, a section each; the flash's direction reads "Auto (n %)".
    m_sections.build(this, m_form, m_values, m_populating, [this] { emit changed(); },
                     [this](const char* key) -> QSpinBox* {
                         if (std::strcmp(key, "flashCoreStretch") != 0)
                             return nullptr;
                         return m_stretch = new StretchSpin(this);
                     });

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
    m_sections.reset();
    bool sameShape = true;
    for (const ObjectRecord* a : subjects) {
        const StyleProperties v = StyleProperties::from(*a);
        m_mixedLine   = m_mixedLine   || v.line        != m_values.line;
        m_stroke.compare(v, m_values);
        m_mixedWidth  = m_mixedWidth  || v.strokeWidth != m_values.strokeWidth;
        m_sections.compare(v, m_values);
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
    m_sections.applyEdited(m_values, t);
    t.applyTo(target);
}

void StyleEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_line, m_mixedLine, m_line->findData(int(m_values.line)));
    showSpin(m_strokeWidth, m_mixedWidth, m_values.strokeWidth, k_strokeWidthMin, k_strokeWidthMax);
    m_stroke.show(m_values);
    m_sections.show(m_values);
    showRows();
    m_populating = false;
}

void StyleEditor::updateRows()
{
    m_form->setRowVisible(m_line, shows(Row::LineKind));
    m_form->setRowVisible(m_strokeWidth, shows(Row::LineWidth));
    m_stroke.setParts(shows(Row::LineColour), shows(Row::LinePattern));
    showRows();
}

void StyleEditor::showRows()
{
    // A selection that disagrees shows every kind's rows; else the sections the line reads.
    const Geometry::LineInfo& line = Geometry::lineInfo(m_values.line);
    m_sections.showSections(m_form, shows(Row::LineKindRows), m_mixedLine, line.sections, line.hides);
}

}  // namespace StripEdit
