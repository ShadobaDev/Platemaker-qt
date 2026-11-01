#include "properties/skineditor.hpp"

#include <QFormLayout>

namespace StripEdit {

SkinEditor::SkinEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    m_form = new QFormLayout(this);
    // No margins: this sits inside the host panel's own group box, so its rows should line up with
    // the rows around them rather than being visibly boxed off.
    m_form->setContentsMargins(0, 0, 0, 0);
    m_fill.build(this, m_form, &SkinProperties::fill, tr("Colour"), m_values, m_populating,
                 [this] { emit changed(); }, [this] { emit committed(); });
    syncFromValues();
}

void SkinEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;   // nothing selected: the controls keep showing what they showed
    m_values = SkinProperties::from(*subjects.first());
    // What the selection disagrees about is not a value this panel may show; binding is not editing.
    m_fill.reset();
    for (const ObjectRecord* a : subjects)
        m_fill.compare(SkinProperties::from(*a), m_values);
    syncFromValues();
}

void SkinEditor::applyTo(ObjectRecord& target) const
{
    // One line, and that is the point: the group's own applyTo() is the only write path, so this
    // editor cannot reach a property it does not own even by accident.
    m_values.applyTo(target);
}

void SkinEditor::applyEditedTo(ObjectRecord& target) const
{
    // Still one write path — the group's — but only what was actually picked: what the artist did not
    // touch stays each object's own, which is the whole point of editing a set.
    SkinProperties t = SkinProperties::from(target);
    m_fill.applyEdited(m_values, t);
    t.applyTo(target);
}

void SkinEditor::syncFromValues()
{
    m_populating = true;
    m_fill.show(m_values);
    m_populating = false;
}

}  // namespace StripEdit
