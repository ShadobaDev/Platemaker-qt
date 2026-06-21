#include "outputprofiledialog.h"
#include "ui_outputprofiledialog.h"
#include "outputformatoptionswidget.h"

#include <platemaker/models/output_profile.hpp>

#include <QString>

// ---------------------------------------------------------------------------

OutputProfileDialog::OutputProfileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OutputProfileDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Shared format + options editor lives inside the "Output format" group.
    m_formatOptions = new OutputFormatOptionsWidget(this);
    ui->verticalLayoutFormat->addWidget(m_formatOptions);

    connect(ui->buttonSave, &QPushButton::clicked, this, &OutputProfileDialog::onSaveClicked);
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

    // Format + per-format options (shared widget).
    m_formatOptions->setFromProfile(profile);

    // Slicing
    ui->spinBoxTargetWidth->setValue(profile.targetWidth);
    ui->spinBoxSliceHeight->setValue(profile.sliceHeight);
    ui->spinBoxStartIndex->setValue(profile.startIndex);

    switch (profile.lastSlicePolicy) {
        case Platemaker::Models::LastSlicePolicy::PadWhite: ui->radioButtonPadWhite->setChecked(true); break;
        case Platemaker::Models::LastSlicePolicy::Crop:     ui->radioButtonCrop->setChecked(true);     break;
        default:                            ui->radioButtonKeepAsIs->setChecked(true); break;
    }
}

Platemaker::Models::OutputProfile OutputProfileDialog::profile() const
{
    Platemaker::Models::OutputProfile p;

    p.name = ui->lineEditName->text().trimmed().toStdString();

    // Format + per-format options (shared widget).
    m_formatOptions->applyToProfile(p);

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
