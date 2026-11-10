#include "outputprofiledialog.hpp"
#include "ui_outputprofiledialog.h"
#include "outputformatoptionswidget.hpp"

#include <platemaker/models/output_profile.hpp>

#include <QLocale>
#include <QMessageBox>
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
    ui->verticalLayoutFormat->insertWidget(0, m_formatOptions);   // above the "saved: …" note of the .ui

    connect(ui->buttonSave, &QPushButton::clicked, this, &OutputProfileDialog::onSaveClicked);

    addSavedNotes();
    connect(ui->buttonRevert, &QPushButton::clicked, this, &OutputProfileDialog::onRevertClicked);
    connect(ui->lineEditName, &QLineEdit::textChanged, this, &OutputProfileDialog::refreshSaved);
    connect(m_formatOptions, &OutputFormatOptionsWidget::edited, this, &OutputProfileDialog::refreshSaved);
    for (QSpinBox* spin : {ui->spinBoxTargetWidth, ui->spinBoxSliceHeight, ui->spinBoxStartIndex})
        connect(spin, &QSpinBox::valueChanged, this, &OutputProfileDialog::refreshSaved);
    connect(ui->spinBoxDpi, &QDoubleSpinBox::valueChanged, this, &OutputProfileDialog::refreshSaved);
    for (QRadioButton* radio : {ui->radioButtonKeepAsIs, ui->radioButtonPadWhite, ui->radioButtonCrop})
        connect(radio, &QRadioButton::toggled, this, &OutputProfileDialog::refreshSaved);
    refreshSaved();
}

OutputProfileDialog::~OutputProfileDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void OutputProfileDialog::setProfile(const Platemaker::Models::OutputProfile &profile, ProfileSource source)
{
    // Set the profile to be edited in the dialog. Call before exec() to populate fields when editing an existing profile.
    // Kept whole, so profile() hands back what this dialog does not edit.
    m_profile = profile;
    m_saved   = source == ProfileSource::Saved;
    ui->lineEditName->setText(QString::fromStdString(profile.name));

    // Format + per-format options (shared widget).
    m_formatOptions->setFromProfile(profile);

    // Slicing
    ui->spinBoxTargetWidth->setValue(profile.targetWidth);
    ui->spinBoxSliceHeight->setValue(profile.sliceHeight);
    ui->spinBoxStartIndex->setValue(profile.startIndex);
    ui->spinBoxDpi->setValue(profile.dpi);

    // Last slice policy radio buttons
    switch (profile.lastSlicePolicy) {
        case Platemaker::Models::LastSlicePolicy::PadWhite: ui->radioButtonPadWhite->setChecked(true); break;
        case Platemaker::Models::LastSlicePolicy::Crop:     ui->radioButtonCrop->setChecked(true);     break;
        default:                            ui->radioButtonKeepAsIs->setChecked(true); break;
    }
    refreshSaved();
}

Platemaker::Models::OutputProfile OutputProfileDialog::profile() const
{
    Platemaker::Models::OutputProfile p = m_profile;   // the fields this dialog does not show, unchanged

    p.name = ui->lineEditName->text().trimmed().toStdString();

    // Format + per-format options (shared widget).
    m_formatOptions->applyToProfile(p);

    // Slicing
    p.targetWidth  = ui->spinBoxTargetWidth->value();
    p.sliceHeight  = ui->spinBoxSliceHeight->value();
    p.startIndex   = ui->spinBoxStartIndex->value();
    p.dpi          = ui->spinBoxDpi->value();

    // Last slice policy radio buttons
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
    // Validate the name field before accepting the dialog. If empty, focus and highlight it.
    if (ui->lineEditName->text().trimmed().isEmpty()) {
        ui->lineEditName->setFocus();
        ui->lineEditName->setStyleSheet("border: 1px solid #e06060;");
        return;
    }
    accept();
}

void OutputProfileDialog::onRevertClicked()
{
    const auto answer = QMessageBox::question(this, tr("Revert"),
        tr("Put every field back to the saved profile? Your changes here are lost."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes)
        setProfile(m_profile, ProfileSource::Saved);
}

void OutputProfileDialog::refreshSaved()
{
    const bool differs = m_saved ? m_savedNotes.refresh(m_profile, profile()) : false;
    if (!m_saved)
        m_savedNotes.clearAll();
    ui->buttonRevert->setVisible(m_saved);
    ui->buttonRevert->setEnabled(differs);
}

void OutputProfileDialog::addSavedNotes()
{
    using namespace Platemaker::Models;
    const auto number = [](int v) { return QString::number(v); };   // as a QSpinBox shows it
    const auto yes    = [](bool on, const QString& what) { return on ? QStringLiteral(", ") + what : QString(); };
    // The format and the options of that format: the options group the format box shows.
    const auto format = [=](const OutputProfile& p) {
        switch (p.outputFormat) {
        case OutputFormat::JPEG: {
            static const char* const subsampling[] = {"4:4:4", "4:2:2", "4:2:0"};   // JpegSubsampling's order
            const auto& o = p.jpegOptions;
            return tr("JPEG, quality %1, %2").arg(o.quality)
                       .arg(QLatin1String(subsampling[static_cast<int>(o.subsampling)]))
                   + yes(o.optimize, tr("optimized")) + yes(o.progressive, tr("progressive"));
        }
        case OutputFormat::WebP: {
            const auto& o = p.webpOptions;
            return (o.lossless ? tr("WebP, lossless") : tr("WebP, quality %1").arg(o.quality))
                   + tr(", effort %1").arg(o.effort);
        }
        case OutputFormat::PNG:
            break;
        }
        return tr("PNG, compression %1").arg(p.pngOptions.compression)
               + yes(p.pngOptions.interlaced, tr("interlaced"));
    };
    // A policy in the words of its radio button.
    const auto policy = [this](const OutputProfile& p) {
        const QRadioButton* radio = p.lastSlicePolicy == LastSlicePolicy::PadWhite ? ui->radioButtonPadWhite
                                  : p.lastSlicePolicy == LastSlicePolicy::Crop     ? ui->radioButtonCrop
                                                                                   : ui->radioButtonKeepAsIs;
        return QString(radio->text()).remove(QLatin1Char('&'));
    };

    m_savedNotes.add(ui->labelSavedName, [](const OutputProfile& p) { return QString::fromStdString(p.name); });
    m_savedNotes.add(ui->labelSavedFormat, format);
    m_savedNotes.add(ui->labelSavedTargetWidth, [=](const OutputProfile& p) { return number(p.targetWidth); });
    m_savedNotes.add(ui->labelSavedSliceHeight, [=](const OutputProfile& p) { return number(p.sliceHeight); });
    m_savedNotes.add(ui->labelSavedStartIndex, [=](const OutputProfile& p) { return number(p.startIndex); });
    m_savedNotes.add(ui->labelSavedDpi, [this](const OutputProfile& p) {
        QLocale shown = ui->spinBoxDpi->locale();   // as the spin box shows it
        shown.setNumberOptions(QLocale::OmitGroupSeparator);
        return shown.toString(p.dpi, 'f', ui->spinBoxDpi->decimals());
    });
    m_savedNotes.add(ui->labelSavedLastSlice, policy);
}
