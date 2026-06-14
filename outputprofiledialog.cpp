#include "outputprofiledialog.h"
#include "ui_outputprofiledialog.h"

#include <platemaker/models/output_profile.hpp>

#include <QString>

// ---------------------------------------------------------------------------

OutputProfileDialog::OutputProfileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OutputProfileDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Show/hide format option panels when format radio changes
    connect(ui->radioButtonPNG,  &QRadioButton::toggled, this, &OutputProfileDialog::onFormatChanged);
    connect(ui->radioButtonJPEG, &QRadioButton::toggled, this, &OutputProfileDialog::onFormatChanged);
    connect(ui->radioButtonWEBP, &QRadioButton::toggled, this, &OutputProfileDialog::onFormatChanged);

    connect(ui->buttonSave,   &QPushButton::clicked, this, &OutputProfileDialog::onSaveClicked);

    // Set initial panel visibility
    onFormatChanged();
}

OutputProfileDialog::~OutputProfileDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void OutputProfileDialog::setProfile(const Platemaker::Models::OutputProfile &profile)
{
    ui->lineEditName->setText(QString::fromStdString(profile.name));

    switch (profile.outputFormat) {
        case Platemaker::Models::OutputFormat::JPEG: ui->radioButtonJPEG->setChecked(true); break;
        case Platemaker::Models::OutputFormat::WebP: ui->radioButtonWEBP->setChecked(true); break;
        default:                     ui->radioButtonPNG->setChecked(true);  break;
    }

    // JPEG options
    ui->spinBoxJpegQuality->setValue(profile.jpegOptions.quality);
    //const QString sub = QString::fromStdString(profile.jpegOptions.subsampling);
    //const int subIdx  = ui->comboBoxJpegSubsampling->findText(sub);
    const int subIdx = (int)profile.jpegOptions.subsampling;
    if (subIdx >= 0) ui->comboBoxJpegSubsampling->setCurrentIndex(subIdx);
    ui->checkBoxJpegOptimize->setChecked(profile.jpegOptions.optimize);
    ui->checkBoxJpegProgressive->setChecked(profile.jpegOptions.progressive);

    // WEBP options
    //ui->spinBoxWebpQuality->setValue(profile.webpOptions.quality);
    //ui->spinBoxWebpCompression->setValue(profile.webpOptions.compression);
    //ui->checkBoxWebpLossless->setChecked(profile.webpOptions.lossless);

    // Slicing
    ui->spinBoxTargetWidth->setValue(profile.targetWidth);
    ui->spinBoxSliceHeight->setValue(profile.sliceHeight);
    ui->spinBoxStartIndex->setValue(profile.startIndex);

    switch (profile.lastSlicePolicy) {
        case Platemaker::Models::LastSlicePolicy::PadWhite: ui->radioButtonPadWhite->setChecked(true); break;
        case Platemaker::Models::LastSlicePolicy::Crop:     ui->radioButtonCrop->setChecked(true);     break;
        default:                            ui->radioButtonKeepAsIs->setChecked(true); break;
    }

    onFormatChanged();
}

Platemaker::Models::OutputProfile OutputProfileDialog::profile() const
{
    Platemaker::Models::OutputProfile p;

    p.name = ui->lineEditName->text().trimmed().toStdString();

    if      (ui->radioButtonJPEG->isChecked()) p.outputFormat = Platemaker::Models::OutputFormat::JPEG;
    else if (ui->radioButtonWEBP->isChecked()) p.outputFormat = Platemaker::Models::OutputFormat::WebP;
    else                                        p.outputFormat = Platemaker::Models::OutputFormat::PNG;

    // JPEG
    p.jpegOptions.quality     = ui->spinBoxJpegQuality->value();
    p.jpegOptions.subsampling = (Platemaker::Models::JpegSubsampling)ui->comboBoxJpegSubsampling->currentText().toInt();
    p.jpegOptions.optimize    = ui->checkBoxJpegOptimize->isChecked();
    p.jpegOptions.progressive = ui->checkBoxJpegProgressive->isChecked();

    // WEBP
    //p.webpOptions.quality     = ui->spinBoxWebpQuality->value();
   // p.webpOptions.compression = ui->spinBoxWebpCompression->value();
   // p.webpOptions.lossless    = ui->checkBoxWebpLossless->isChecked();

    // Slicing
    p.targetWidth  = ui->spinBoxTargetWidth->value();
    p.sliceHeight  = ui->spinBoxSliceHeight->value();
    p.startIndex   = ui->spinBoxStartIndex->value();

    if      (ui->radioButtonPadWhite->isChecked()) p.lastSlicePolicy = Platemaker::Models::LastSlicePolicy::PadWhite;
    else if (ui->radioButtonCrop->isChecked())     p.lastSlicePolicy = Platemaker::Models::LastSlicePolicy::Crop;
    else                                            p.lastSlicePolicy = Platemaker::Models::LastSlicePolicy::KeepAsIs;

    return p;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void OutputProfileDialog::onSaveClicked()
{
    if (ui->lineEditName->text().trimmed().isEmpty()) {
        ui->lineEditName->setFocus();
        ui->lineEditName->setStyleSheet("border: 1px solid #e06060;");
        return;
    }
    accept();
}

void OutputProfileDialog::onFormatChanged()
{
    ui->frameJpegOptions->setVisible(ui->radioButtonJPEG->isChecked());
    ui->frameWebpOptions->setVisible(ui->radioButtonWEBP->isChecked());

    // Let the dialog resize itself to fit the visible content
    adjustSize();
}
