#include "properties/textureeditor.hpp"

#include <QComboBox>
#include <QFormLayout>

namespace StripEdit {

namespace {

//! The context the rows' strings are translated in: this panel's own.
constexpr char k_trContext[] = "StripEdit::TextureEditor";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::TextureEditor", s)
//! Rough's numbers. Lengths in strip pixels, with a decimal: a blur's useful range is around a pixel.
const FieldRowSpec k_roughRows[] = {
    // A step of 0.005: a wave's length is 1 / grain, so a change shows in proportion, and a thousandth
    // at the Marker's 0.03 is a wave 3 % shorter, nothing the eye sees.
    {"roughFrequency",    PM_TR("Grain:"),        0.001, 0.5, "",
     PM_TR("How fine the wobble is: about one wave every 1 / grain pixels, so 0.03 is a wave every 33 px "
           "and 0.07 one every 14 px. Higher gives smaller, closer waves."), 3, 0.005},
    {"roughOctaves",      PM_TR("Detail:"),       1,     8,   "",    PM_TR("How many finer wobbles ride on the first.")},
    {"roughDisplacement", PM_TR("Displacement:"), 0,     50,  " px", PM_TR("How far the outline wanders off its line."), 1},
    {"roughBlur",         PM_TR("Blur:"),         0,     10,  " px", PM_TR("How much the balloon softens after the wobble, as ink bleeding into paper. 0 keeps it sharp."), 1},
};
#undef PM_TR

} // namespace

TextureEditor::TextureEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    m_kind = new QComboBox(this);
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i)
        m_kind->addItem(textureTitle(static_cast<ObjectRecord::Texture>(i)), i);
    m_kind->setToolTip(tr("What is done to the balloon as it is rendered, line and shadow and all, never to "
                          "the words. Rough makes the edge wander, as a felt-tip or ink does; it is shown "
                          "here exactly as it will be baked, because the library draws it."));
    m_form->addRow(tr("Texture:"), m_kind);
    m_rough.build(this, m_form, "rough", k_roughRows, k_trContext, m_values, m_populating,
                  [this] { emit changed(); });

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.kind = static_cast<ObjectRecord::Texture>(m_kind->currentData().toInt());
        m_kindTouched = true;
        m_mixedKind   = false;
        showRows();
        emit changed();
    });

    syncFromValues();
}

void TextureEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values    = TextureProperties::from(*subjects.first());
    m_mixedKind = false;
    m_rough.reset();
    for (const ObjectRecord* a : subjects) {
        const TextureProperties t = TextureProperties::from(*a);
        m_mixedKind = m_mixedKind || t.kind != m_values.kind;
        m_rough.compare(t, m_values);
    }
    m_kindTouched = false;
    syncFromValues();
}

void TextureEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void TextureEditor::applyEditedTo(ObjectRecord& target) const
{
    TextureProperties t = TextureProperties::from(target);
    if (m_kindTouched)
        t.kind = m_values.kind;
    m_rough.applyEdited(m_values, t);
    t.applyTo(target);
}

void TextureEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_kind, m_mixedKind, m_kind->findData(int(m_values.kind)));
    m_rough.show(m_values);
    showRows();
    m_populating = false;
}

void TextureEditor::showRows()
{
    // A selection that disagrees shows the rows at least one of them has.
    m_rough.setVisible(m_form, m_mixedKind || m_values.kind == ObjectRecord::Texture::Rough);
}

}  // namespace StripEdit
