#include "blendeditor.hpp"

#include <QComboBox>
#include <QFormLayout>

namespace StripEdit {

const QList<QPair<Platemaker::Models::BlendMode, QString>>& blendModes()
{
    using BlendMode = Platemaker::Models::BlendMode;
    static const QList<QPair<BlendMode, QString>> modes{
        {BlendMode::Over,     BlendEditor::tr("Normal")},
        {BlendMode::Multiply, BlendEditor::tr("Multiply")},
        {BlendMode::Screen,   BlendEditor::tr("Screen")},
        {BlendMode::Overlay,  BlendEditor::tr("Overlay")},
        {BlendMode::Darken,   BlendEditor::tr("Darken")},
        {BlendMode::Lighten,  BlendEditor::tr("Lighten")},
    };
    return modes;
}

BlendEditor::BlendEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_combo = new QComboBox(this);
    for (const auto& [mode, name] : blendModes())
        m_combo->addItem(name, static_cast<int>(mode));
    m_combo->setToolTip(tr("How this object is mixed with the artwork under it. Normal simply draws "
                           "it over the top."));
    form->addRow(tr("Blend"), m_combo);

    connect(m_combo, &QComboBox::activated, this, [this](int i) {
        // activated(), not currentIndexChanged(): only a human picking an entry is an edit, so binding
        // a selection never composites anything differently by itself.
        if (m_populating || i < 0)
            return;
        emit blendPicked(static_cast<Platemaker::Models::BlendMode>(m_combo->itemData(i).toInt()));
    });
}

void BlendEditor::setBlend(std::optional<Platemaker::Models::BlendMode> blend)
{
    m_populating = true;
    // No value → no current entry, and the placeholder says why. The same shape as a mixed swatch or a
    // mixed spin box: the control reports that there is no one answer rather than showing the first.
    m_combo->setCurrentIndex(blend ? m_combo->findData(static_cast<int>(*blend)) : -1);
    m_combo->setPlaceholderText(blend ? QString() : tr("Mixed"));
    m_populating = false;
}

}  // namespace StripEdit
