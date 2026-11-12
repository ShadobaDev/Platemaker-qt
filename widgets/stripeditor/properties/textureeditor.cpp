#include "properties/textureeditor.hpp"
#include "styles.hpp"

#include <QComboBox>
#include <QFormLayout>

namespace StripEdit {

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
    m_sections.build(this, m_form, m_values, m_populating, [this] { emit changed(); });

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
    m_sections.reset();
    for (const ObjectRecord* a : subjects) {
        const TextureProperties t = TextureProperties::from(*a);
        m_mixedKind = m_mixedKind || t.kind != m_values.kind;
        m_sections.compare(t, m_values);
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
    m_sections.applyEdited(m_values, t);
    t.applyTo(target);
}

void TextureEditor::syncFromValues()
{
    m_populating = true;
    showCombo(m_kind, m_mixedKind, m_kind->findData(int(m_values.kind)));
    m_sections.show(m_values);
    showRows();
    m_populating = false;
}

void TextureEditor::showRows()
{
    // A selection that disagrees shows the rows at least one of them has.
    m_sections.showSections(m_form, true, m_mixedKind, Geometry::textureInfo(m_values.kind).sections);
}

}  // namespace StripEdit
