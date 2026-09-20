#include "assetstatepanel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

    auto* hint = new QLabel(tr("Imported artwork has no properties to re-type — somebody else drew it. "
                               "Its place in the stack, its blend and its page are on the object's menu."),
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

void AssetStatePanel::showArtwork(const QString& name, double percent)
{
    m_populating = true;
    m_name->setText(name);
    m_scale->setValue(qBound(k_minPercent, percent, k_maxPercent));
    m_populating = false;
}

}  // namespace StripEdit
