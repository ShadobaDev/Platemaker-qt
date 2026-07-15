#include "outputformatoptionswidget.h"
#include "ui_outputformatoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

using namespace Platemaker::Models;

OutputFormatOptionsWidget::OutputFormatOptionsWidget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::OutputFormatOptionsWidget)
{
    ui->setupUi(this);

    // Format dropdown (item data = OutputFormat enum value).
    ui->comboBoxFormat->addItem(tr("PNG"),  static_cast<int>(OutputFormat::PNG));
    ui->comboBoxFormat->addItem(tr("JPEG"), static_cast<int>(OutputFormat::JPEG));
    ui->comboBoxFormat->addItem(tr("WebP"), static_cast<int>(OutputFormat::WebP));

    // JPEG subsampling — item order matches the JpegSubsampling enum.
    ui->comboBoxJpegSubsampling->addItems({tr("4:4:4 (best)"), tr("4:2:2"), tr("4:2:0 (smallest)")});

    // Ranges.
    ui->spinBoxPngCompression->setRange(0, 9);
    ui->spinBoxJpegQuality->setRange(1, 95);
    ui->spinBoxWebpQuality->setRange(0, 100);
    ui->spinBoxWebpEffort->setRange(0, 6);

    connect(ui->comboBoxFormat, &QComboBox::currentIndexChanged,
            this, &OutputFormatOptionsWidget::onFormatChanged);

    // Any option control marks the widget edited.
    connect(ui->spinBoxPngCompression, &QSpinBox::valueChanged, this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->checkBoxPngInterlaced, &QCheckBox::toggled,     this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->spinBoxJpegQuality,    &QSpinBox::valueChanged, this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->comboBoxJpegSubsampling, &QComboBox::currentIndexChanged, this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->checkBoxJpegOptimize,    &QCheckBox::toggled,   this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->checkBoxJpegProgressive, &QCheckBox::toggled,   this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->spinBoxWebpQuality,  &QSpinBox::valueChanged,   this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->checkBoxWebpLossless, &QCheckBox::toggled,      this, &OutputFormatOptionsWidget::onAnyOptionChanged);
    connect(ui->spinBoxWebpEffort,   &QSpinBox::valueChanged,   this, &OutputFormatOptionsWidget::onAnyOptionChanged);

    updateVisibility();
}

OutputFormatOptionsWidget::~OutputFormatOptionsWidget()
{
    delete ui;
}

void OutputFormatOptionsWidget::setFromProfile(const OutputProfile& profile)
{
    m_loading = true;

    // Set the format dropdown to match the profile's output format.
    const int idx = ui->comboBoxFormat->findData(static_cast<int>(profile.outputFormat));
    ui->comboBoxFormat->setCurrentIndex(idx >= 0 ? idx : 0);

    // PNG options
    ui->spinBoxPngCompression->setValue(profile.pngOptions.compression);
    ui->checkBoxPngInterlaced->setChecked(profile.pngOptions.interlaced);

    // JPEG options
    ui->spinBoxJpegQuality->setValue(profile.jpegOptions.quality);
    ui->comboBoxJpegSubsampling->setCurrentIndex(static_cast<int>(profile.jpegOptions.subsampling));
    ui->checkBoxJpegOptimize->setChecked(profile.jpegOptions.optimize);
    ui->checkBoxJpegProgressive->setChecked(profile.jpegOptions.progressive);

    // WebP options
    ui->spinBoxWebpQuality->setValue(profile.webpOptions.quality);
    ui->checkBoxWebpLossless->setChecked(profile.webpOptions.lossless);
    ui->spinBoxWebpEffort->setValue(profile.webpOptions.effort);

    m_loading = false;
    updateVisibility();
}

void OutputFormatOptionsWidget::applyToProfile(OutputProfile& profile) const
{
    profile.outputFormat = static_cast<OutputFormat>(ui->comboBoxFormat->currentData().toInt());

    // PNG options
    profile.pngOptions.compression = ui->spinBoxPngCompression->value();
    profile.pngOptions.interlaced  = ui->checkBoxPngInterlaced->isChecked();

    // JPEG options
    profile.jpegOptions.quality     = ui->spinBoxJpegQuality->value();
    profile.jpegOptions.subsampling = static_cast<JpegSubsampling>(
        ui->comboBoxJpegSubsampling->currentIndex());
    profile.jpegOptions.optimize    = ui->checkBoxJpegOptimize->isChecked();
    profile.jpegOptions.progressive = ui->checkBoxJpegProgressive->isChecked();

    // WebP options
    profile.webpOptions.quality  = ui->spinBoxWebpQuality->value();
    profile.webpOptions.lossless = ui->checkBoxWebpLossless->isChecked();
    profile.webpOptions.effort   = ui->spinBoxWebpEffort->value();
}

void OutputFormatOptionsWidget::onFormatChanged()
{
    updateVisibility();
    onAnyOptionChanged();
}

void OutputFormatOptionsWidget::onAnyOptionChanged()
{
    // Emit the edited() signal if not currently loading from a profile.
    if (!m_loading)
        emit edited();
}

void OutputFormatOptionsWidget::updateVisibility()
{
    // Show/hide the option groups based on the selected output format.
    const int fmt = ui->comboBoxFormat->currentData().toInt();
    ui->groupBoxPNG->setVisible(fmt == static_cast<int>(OutputFormat::PNG));
    ui->groupBoxJPG->setVisible(fmt == static_cast<int>(OutputFormat::JPEG));
    ui->groupBoxWebP->setVisible(fmt == static_cast<int>(OutputFormat::WebP));
}
