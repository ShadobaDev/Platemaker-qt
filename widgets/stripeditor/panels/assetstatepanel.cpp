#include "assetstatepanel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "collapsiblesection.h"

namespace StripEdit {

namespace {
constexpr double k_minPercent = 1.0;      //!< Below this it is a dot nobody can grab back.
constexpr double k_maxPercent = 2000.0;   //!< A sound effect ten times the strip's width is still work.
}  // namespace

AssetStatePanel::AssetStatePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);

    m_name = new QLabel(this);
    m_name->setWordWrap(true);
    QFont bold = m_name->font();
    bold.setBold(true);
    m_name->setFont(bold);
    lay->addWidget(m_name);

    auto* form = new QFormLayout;
    m_scale    = new QDoubleSpinBox(this);
    m_scale->setRange(k_minPercent, k_maxPercent);
    m_scale->setDecimals(1);
    m_scale->setSuffix(tr(" %"));
    m_scale->setSingleStep(5.0);
    m_scale->setToolTip(tr("How big the picture is drawn, as a percentage of its own pixels. 100% is "
                           "one image pixel per strip pixel — the size it was drawn at."));
    form->addRow(tr("Size"), m_scale);
    lay->addLayout(form);

    // The words over the picture. The same editor a balloon's lettering uses, in the same collapsible
    // section, because it is the same property group — this panel differs from ③'s other one in what it
    // does *not* have, not in having its own version of what it does.
    m_text = new TextEditor(this);
    auto* textSection = new CollapsibleSection(tr("Text"), this);
    textSection->setContent(m_text);
    textSection->setExpanded(true);
    lay->addWidget(textSection);
    connect(m_text, &PropertyGroupEditor::edited, this, [this] {
        if (m_populating)
            return;
        m_text->applyTo(m_record);
        emit changed(m_record);
        m_commitTimer->start();
    });
    connect(m_text, &PropertyGroupEditor::committed, this, [this] {
        if (m_populating)
            return;
        m_text->applyTo(m_record);
        m_commitTimer->stop();
        emit committed(m_record);
    });
    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] { emit committed(m_record); });

    // The one property it shares with a balloon: blend belongs to the overlay, not to the drawing, so
    // a picture has one exactly as lettering does.
    m_blend = new BlendEditor(this);
    lay->addWidget(m_blend);
    connect(m_blend, &BlendEditor::blendPicked, this, &AssetStatePanel::blendPicked);

    auto* hint = new QLabel(tr("Imported artwork has no properties to re-type — somebody else drew it. "
                               "Its place in the stack and its page are on the object's menu."),
                            this);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(hint);

    m_delete = new QPushButton(tr("Delete"), this);
    lay->addWidget(m_delete);
    lay->addStretch(1);

    // Live while it moves, settled when it settles — the contract every panel in ③ follows, so a drag
    // on the spin box previews and a release is the one undo step.
    connect(m_scale, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_populating)
            emit scaleChanged(v);
    });
    connect(m_scale, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_populating)
            emit scaleCommitted(m_scale->value());
    });
    connect(m_delete, &QPushButton::clicked, this, &AssetStatePanel::deleteRequested);
}

void AssetStatePanel::setSelectionBlend(std::optional<Platemaker::Models::BlendMode> blend)
{
    m_blend->setBlend(blend);
}

void AssetStatePanel::showArtwork(const QString& name, double percent, const TextArtifact& record)
{
    m_populating = true;
    m_record = record;
    m_name->setText(name);
    m_scale->setValue(qBound(k_minPercent, percent, k_maxPercent));
    m_text->bindOne(m_record);
    m_commitTimer->stop();
    m_populating = false;
}

}  // namespace StripEdit
