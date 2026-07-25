#include "licencedialog.h"
#include "ui_licencedialog.h"

#include <QFile>
#include <QFontDatabase>
#include <QTextBrowser>
#include <QTextStream>

LicenceDialog::LicenceDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::LicenceDialog)
{
    ui->setupUi(this);

    // Licence texts are plain text with hard-wrapped lines; a fixed-width font keeps their layout,
    // and NoWrap avoids double-wrapping the already-wrapped paragraphs. (Set here rather than in the
    // .ui because the system fixed font is only known at runtime.)
    ui->textLicence->setLineWrapMode(QTextBrowser::NoWrap);
    ui->textLicence->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

LicenceDialog::~LicenceDialog()
{
    delete ui;
}

void LicenceDialog::showLicence(const QString& componentLabel, const QString& licenceFilePath)
{
    setWindowTitle(componentLabel);

    QFile file(licenceFilePath);
    if (!licenceFilePath.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        ui->textLicence->setPlainText(in.readAll());
    } else {
        // The SBOM still records the licence by SPDX id even when the text file is absent (e.g. a
        // build run without the credits/ copy); point the user at it rather than showing nothing.
        ui->textLicence->setPlainText(
            tr("The licence text for this component is not bundled with this build.\n\n"
               "See credits/sbom.spdx.json next to the application for the full component list "
               "and licence identifiers."));
    }

    exec();
}
