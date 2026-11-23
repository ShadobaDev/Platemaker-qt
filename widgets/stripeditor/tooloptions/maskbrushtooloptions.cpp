#include "tooloptions/maskbrushtooloptions.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>

namespace StripEdit {

namespace {

const auto k_eraseKey = QStringLiteral("stripEditor/maskBrush/erase");
const auto k_sizeKey  = QStringLiteral("stripEditor/maskBrush/size");

constexpr int k_defaultSizePx = 40;     //!< About a letter's height on a page.
constexpr int k_maxSizePx     = 1000;   //!< A panel's width, about: past it a mask is drawn by its nodes.

}  // namespace

MaskBrushToolOptions::MaskBrushToolOptions(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);

    auto* paints = new QHBoxLayout;
    m_brush      = new QRadioButton(tr("Brush"), this);
    m_brush->setToolTip(tr("Hides the selected object where you paint."));
    m_eraser = new QRadioButton(tr("Eraser"), this);
    m_eraser->setToolTip(tr("Shows the selected object again where you paint."));
    paints->addWidget(m_brush);
    paints->addWidget(m_eraser);
    paints->addStretch(1);
    form->addRow(tr("Paints"), paints);

    m_size = new QSpinBox(this);
    m_size->setRange(1, k_maxSizePx);
    m_size->setSuffix(tr(" px"));
    m_size->setToolTip(tr("The brush's diameter on the strip, whatever the zoom."));
    form->addRow(tr("Size"), m_size);

    const QSettings settings;
    (settings.value(k_eraseKey, false).toBool() ? m_eraser : m_brush)->setChecked(true);
    m_size->setValue(settings.value(k_sizeKey, k_defaultSizePx).toInt());

    connect(m_brush, &QRadioButton::toggled, this, [this] {
        QSettings().setValue(k_eraseKey, !hides());
        emit changed();
    });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int px) {
        QSettings().setValue(k_sizeKey, px);
        emit changed();
    });
}

bool MaskBrushToolOptions::hides() const
{
    return m_brush->isChecked();
}

int MaskBrushToolOptions::size() const
{
    return m_size->value();
}

}  // namespace StripEdit
