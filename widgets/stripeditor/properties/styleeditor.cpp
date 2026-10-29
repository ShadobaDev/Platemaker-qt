#include "properties/styleeditor.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>

#include <cstring>
#include <type_traits>

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

//! What the panel calls a flash field, and what its spin offers. Lengths in strip pixels.
struct FlashRowSpec
{
    const char* key;
    const char* title;
    int         min, max;
    const char* suffix;
    const char* tip;
};

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::StyleEditor", s)
const FlashRowSpec k_flashRows[] = {
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
#undef PM_TR

const FlashRowSpec* flashRowSpec(const char* key)
{
    for (const FlashRowSpec& s : k_flashRows)
        if (std::strcmp(s.key, key) == 0)
            return &s;
    return nullptr;
}

} // namespace

StyleEditor::StyleEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    m_kind = new QComboBox(this);
    for (int i = 0; i <= int(StyleProperties::k_lastKind); ++i)
        m_kind->addItem(styleTitle(static_cast<ObjectRecord::Style>(i)), i);
    m_kind->setToolTip(tr("How the outline is drawn. Marker and Ink roughen it as it is rendered, shown "
                          "here exactly as it will be baked, because the library draws them. Flash rings "
                          "it with strokes instead of a solid line."));
    m_form->addRow(tr("Line style:"), m_kind);

    m_amount = new QSpinBox(this);
    m_amount->setRange(k_styleAmountMin, k_styleAmountMax);
    m_amount->setSuffix(tr(" %"));
    m_amount->setToolTip(tr("How strong the line style is. 100% is the preset's own strength."));
    m_form->addRow(tr("Style amount:"), m_amount);

    // One row per flash field, in the field list's order: a field added to the list and not given a row
    // above stops a debug build here.
    StyleProperties::fields([&](const char* key, auto member) {
        if (std::strncmp(key, "flash", 5) != 0)
            return;
        const FlashRowSpec* spec = flashRowSpec(key);
        Q_ASSERT_X(spec, "StyleEditor", "a flash field with no row in k_flashRows");
        if (!spec)
            return;

        QSpinBox* spin = nullptr;
        if (std::strcmp(key, "flashCoreStretch") == 0)
            spin = m_stretch = new StretchSpin(this);
        else
            spin = new QSpinBox(this);
        spin->setRange(spec->min, spec->max);
        spin->setSuffix(QString::fromLatin1(spec->suffix));
        spin->setToolTip(tr(spec->tip));
        m_form->addRow(tr(spec->title), spin);

        using T = std::remove_reference_t<decltype(m_values.*member)>;
        FlashRow row;
        row.spin = spin;
        row.min  = spec->min;
        row.get  = [member](const StyleProperties& v) { return int(qRound(double(v.*member))); };
        row.set  = [member](StyleProperties& v, int x) { v.*member = T(x); };
        m_flash.push_back(row);

        const size_t index = m_flash.size() - 1;
        connect(spin, &QSpinBox::valueChanged, this, [this, index](int v) {
            if (m_populating)
                return;
            FlashRow& r = m_flash[index];
            if (restoreSpin(r.spin, r.min))
                r.mixed = false;
            r.set(m_values, v);
            r.touched = true;
            emit changed();
        });
    });

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.kind = static_cast<ObjectRecord::Style>(m_kind->currentData().toInt());
        m_kindTouched = true;
        m_mixedKind   = false;
        showRows();
        emit changed();
    });
    connect(m_amount, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_populating)
            return;
        if (restoreSpin(m_amount, k_styleAmountMin))
            m_mixedAmount = false;
        m_values.amount = v / 100.0;
        m_amountTouched = true;
        emit changed();
    });

    syncFromValues();
}

void StyleEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = StyleProperties::from(*subjects.first());

    m_mixedKind = m_mixedAmount = false;
    for (FlashRow& r : m_flash)
        r.mixed = r.touched = false;
    bool sameShape = true;
    for (const ObjectRecord* a : subjects) {
        const StyleProperties v = StyleProperties::from(*a);
        m_mixedKind   = m_mixedKind   || v.kind   != m_values.kind;
        m_mixedAmount = m_mixedAmount || !qFuzzyCompare(v.amount + 1.0, m_values.amount + 1.0);
        for (FlashRow& r : m_flash)
            r.mixed = r.mixed || r.get(v) != r.get(m_values);
        sameShape = sameShape && a->shape.kind == subjects.first()->shape.kind;
    }
    m_kindTouched = m_amountTouched = false;
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
    if (m_kindTouched)   t.kind   = m_values.kind;
    if (m_amountTouched) t.amount = m_values.amount;
    for (const FlashRow& r : m_flash)
        if (r.touched)
            r.set(t, r.get(m_values));
    t.applyTo(target);
}

void StyleEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_kind, m_mixedKind, m_kind->findData(int(m_values.kind)));
    showSpin(m_amount, m_mixedAmount, qRound(m_values.amount * 100.0), k_styleAmountMin, k_styleAmountMax);
    for (const FlashRow& r : m_flash)
        showSpin(r.spin, r.mixed, r.get(m_values), r.min, r.spin->maximum());
    showRows();
    m_populating = false;
}

void StyleEditor::showRows()
{
    // A selection that disagrees about the style shows what at least one of them has.
    const Geometry::StyleInfo& info  = Geometry::styleInfo(m_values.kind);
    const bool                 flash = m_mixedKind || m_values.kind == ObjectRecord::Style::Flash;
    // The amount scales a filter. A flash has none (its rows describe it whole), so the row goes; Clean
    // keeps it, disabled, as before.
    m_form->setRowVisible(m_amount, m_mixedKind || !info.ink);
    m_amount->setEnabled(m_mixedKind || info.noise);
    for (const FlashRow& r : m_flash)
        m_form->setRowVisible(r.spin, flash);
}

}  // namespace StripEdit
