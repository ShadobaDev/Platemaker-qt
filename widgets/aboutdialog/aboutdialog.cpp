#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include <QApplication>
#include <QIcon>
#include <QString>

// Versions are injected as compile definitions by CMake (see CMakeLists.txt):
//   PLATEMAKER_GUI_VERSION — this app's project() version
//   PLATEMAKER_LIB_VERSION — libplatemaker version from find_package (platemaker_VERSION)
// Fallbacks keep the dialog building even if a define is missing.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif
#ifndef PLATEMAKER_LIB_VERSION
#define PLATEMAKER_LIB_VERSION "unknown"
#endif

namespace {
constexpr auto kRepoUrl = "https://github.com/ShadobaDev/Platemaker-qt";

QString orUnknown(QString v)
{
    return v.isEmpty() ? QStringLiteral("unknown") : v;
}
} // namespace

AboutDialog::AboutDialog(Tab initial, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    // App icon (reuses the window icon set in main.cpp — no file dependency here).
    const QIcon appIcon = qApp->windowIcon();
    if (!appIcon.isNull())
        ui->labelIcon->setPixmap(appIcon.pixmap(64, 64));

    const QString guiVer = orUnknown(qApp->applicationVersion().isEmpty()
                                         ? QStringLiteral(PLATEMAKER_GUI_VERSION)
                                         : qApp->applicationVersion());
    const QString libVer = orUnknown(QStringLiteral(PLATEMAKER_LIB_VERSION));

    // --- About tab ---
    ui->labelAbout->setText(tr(
        "<h2>Platemaker</h2>"
        "<p>Comic artist canvas tool — pre-processing and post-processing "
        "for Webtoon-style publishing.</p>"
        "<p><b>Platemaker:</b> %1<br>"
        "<b>libplatemaker:</b> %2<br>"
        "<b>Qt:</b> %3</p>"
        "<p>Licensed under GPL-3.0.<br>"
        "<a href=\"%4\">%4</a></p>")
        .arg(guiVer, libVer, QStringLiteral(QT_VERSION_STR), QString::fromLatin1(kRepoUrl)));

    // --- Authors tab ---
    ui->textAuthors->setHtml(tr(
        "<h3>Authors</h3>"
        "<p>Bartłomiej Mucha</p>"
        "<p>With thanks to all contributors. See "
        "<a href=\"%1/blob/main/CONTRIBUTING.md\">CONTRIBUTING.md</a> and "
        "<a href=\"%1/blob/main/CLA.md\">CLA.md</a>.</p>")
        .arg(QString::fromLatin1(kRepoUrl)));

    // --- Manual tab (mock — no manual yet) ---
    ui->textManual->setHtml(tr(
        "<h3>User manual</h3>"
        "<p><i>Coming soon.</i></p>"
        "<p>In the meantime, see the project page for documentation:<br>"
        "<a href=\"%1\">%1</a></p>")
        .arg(QString::fromLatin1(kRepoUrl)));

    ui->tabWidget->setCurrentIndex(static_cast<int>(initial));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
