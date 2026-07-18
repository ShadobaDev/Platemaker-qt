#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include <QApplication>
#include <QIcon>
#include <QString>

// Versions and licences are injected as compile definitions by CMake (see CMakeLists.txt),
// so updating either is a build-file edit rather than a search through the UI code:
//   PLATEMAKER_GUI_VERSION — this app's project() version
//   PLATEMAKER_LIB_VERSION — libplatemaker version from find_package (platemaker_VERSION)
//   PLATEMAKER_*_LICENCE   — licence of each linked component
// Fallbacks keep the dialog building even if a define is missing.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif
#ifndef PLATEMAKER_LIB_VERSION
#define PLATEMAKER_LIB_VERSION "unknown"
#endif
#ifndef PLATEMAKER_GUI_LICENCE
#define PLATEMAKER_GUI_LICENCE "unknown"
#endif
#ifndef PLATEMAKER_LIB_LICENCE
#define PLATEMAKER_LIB_LICENCE "unknown"
#endif
#ifndef PLATEMAKER_QT_LICENCE
#define PLATEMAKER_QT_LICENCE "unknown"
#endif
#ifndef PLATEMAKER_VIPS_LICENCE
#define PLATEMAKER_VIPS_LICENCE "unknown"
#endif
#ifndef PLATEMAKER_JSON_LICENCE
#define PLATEMAKER_JSON_LICENCE "unknown"
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
    //
    // Hidden when there is no icon to show: an empty QLabel still claims its share of the
    // layout, which read as a stray band of padding above the text. Note the window icon
    // is currently loaded from a relative filesystem path in main.cpp, so it silently
    // fails whenever the working directory is not the install dir — see docs/TODO.md.
    const QIcon appIcon = qApp->windowIcon();
    const bool hasIcon = !appIcon.isNull();
    ui->labelIcon->setVisible(hasIcon);
    if (hasIcon)
        ui->labelIcon->setPixmap(appIcon.pixmap(64, 64));

    // This tab used to be a QLabel, which meant styling it to imitate the QTextBrowsers on
    // the other two tabs — frame, viewport colour, inner margin — and it never quite
    // matched. It is a QTextBrowser now, so the look comes for free and stays consistent.

    const QString guiVer = orUnknown(qApp->applicationVersion().isEmpty()
                                         ? QStringLiteral(PLATEMAKER_GUI_VERSION)
                                         : qApp->applicationVersion());
    const QString libVer = orUnknown(QStringLiteral(PLATEMAKER_LIB_VERSION));

    // --- About tab ---
    // The third-party rows are not decoration: Qt and libvips are used under the LGPL,
    // which requires stating that they are used and under which licence.
    //
    // One row per linked component. Cell values are data, not prose, so they stay outside
    // tr() — only the headings are translated.
    const auto row = [](const QString& component, const QString& version,
                        const QString& licence) {
        return QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
            .arg(component.toHtmlEscaped(), version.toHtmlEscaped(),
                 licence.toHtmlEscaped());
    };

    const QString table =
        QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4'>")
        + tr("<tr><th align='left'>Component</th>"
             "<th align='left'>Version</th>"
             "<th align='left'>Licence</th></tr>")
        + row(QStringLiteral("Platemaker"),    guiVer,
              QStringLiteral(PLATEMAKER_GUI_LICENCE))
        + row(QStringLiteral("libplatemaker"), libVer,
              QStringLiteral(PLATEMAKER_LIB_LICENCE))
        + row(QStringLiteral("Qt"),            QStringLiteral(QT_VERSION_STR),
              QStringLiteral(PLATEMAKER_QT_LICENCE))
        // The lib's own dependencies. No versions here: the lib links both PRIVATE, so
        // nothing about them reaches the GUI — see the linkedComponents() item in the
        // lib's TODO, which is what will fill these in properly.
        + row(QStringLiteral("libvips"),        orUnknown({}),
              QStringLiteral(PLATEMAKER_VIPS_LICENCE))
        + row(QStringLiteral("nlohmann/json"),  orUnknown({}),
              QStringLiteral(PLATEMAKER_JSON_LICENCE))
        + QStringLiteral("</table>");

    ui->textAbout->setHtml(
        tr("<h2>Platemaker</h2>"
           "<p>Comic artist canvas tool — pre-processing and post-processing "
           "for Webtoon-style publishing.</p>"
           "<p>Image processing is powered by "
           "<a href=\"https://www.libvips.org\">libvips</a>.</p>")
        + table
        + QStringLiteral("<p><a href=\"%1\">%1</a></p>")
              .arg(QString::fromLatin1(kRepoUrl)));

    // --- Authors tab ---
    ui->textAuthors->setHtml(tr(
        "<h3>Authors</h3>"
        "<p>Bartłomiej Mucha (ShadobaDev) <a href='mailto:shadobadev@gmail.com'>shadobadev@gmail.com</a></p>"
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
