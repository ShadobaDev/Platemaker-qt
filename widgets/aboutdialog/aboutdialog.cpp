#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include "licencedialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QString>
#include <QTextBrowser>
#include <QUrl>
#include <QUrlQuery>

#include <platemaker/infrastructure/build_info/build_info.hpp>

// This app owns two facts, so they stay CMake compile definitions (updating either is a build-file
// edit, no code hunt):
//   PLATEMAKER_GUI_VERSION — this app's project() version
//   PLATEMAKER_GUI_LICENCE / PLATEMAKER_QT_LICENCE — the GUI's own licence, and Qt's (linked directly)
// Everything about libplatemaker and its dependencies comes from the lib at runtime instead
// (buildInfo() / linkedComponents()), so the GUI never asserts facts about code it does not own.
// Fallbacks keep the dialog building even if a define is missing.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif
#ifndef PLATEMAKER_GUI_LICENCE
#define PLATEMAKER_GUI_LICENCE "unknown"
#endif
#ifndef PLATEMAKER_QT_LICENCE
#define PLATEMAKER_QT_LICENCE "unknown"
#endif

namespace {
constexpr auto kRepoUrl    = "https://github.com/ShadobaDev/Platemaker-qt";
constexpr auto kLibRepoUrl = "https://github.com/ShadobaDev/PlateMaker";
constexpr auto kQtRepoUrl  = "https://github.com/qt/qtbase";
constexpr auto kWikiUrl    = "https://github.com/ShadobaDev/Platemaker-qt/wiki/Manual";

// Custom link scheme for "open the licence text of this component" (vs an external GitHub link).
constexpr auto kLicenceScheme = "pm-licence";

QString orUnknown(QString v)
{
    return v.isEmpty() ? QStringLiteral("unknown") : v;
}

// Maps an SPDX identifier to the licence-text file shipped in credits/licenses/. Files are named by
// SPDX id and deduplicated by identical text: Qt's LGPL-3.0-only shares the LGPL-3.0 body the lib
// ships as LGPL-3.0-or-later.txt; nlohmann/json's MIT carries its own copyright, so it is a
// component-qualified file. An unmapped id yields an empty string → the viewer's "not bundled" notice.
QString licenceFileForSpdx(const QString& spdx)
{
    if (spdx == QLatin1String("LGPL-2.1-or-later")) return QStringLiteral("LGPL-2.1-or-later.txt");
    if (spdx == QLatin1String("LGPL-3.0-or-later")) return QStringLiteral("LGPL-3.0-or-later.txt");
    if (spdx == QLatin1String("LGPL-3.0-only"))     return QStringLiteral("LGPL-3.0-or-later.txt");
    if (spdx == QLatin1String("GPL-3.0-only"))      return QStringLiteral("GPL-3.0-only.txt");
    if (spdx == QLatin1String("MIT"))               return QStringLiteral("nlohmann_json.MIT.txt");
    return QString();
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

    // The lib reports its own identity and its dependencies at runtime, from the DLL actually
    // loaded — so libplatemaker, libvips and nlohmann/json are never described by values this
    // repo hardcodes about someone else's code.
    const Platemaker::Infrastructure::BuildInfo libBuild = Platemaker::Infrastructure::buildInfo();

    // --- About tab ---
    // The third-party rows are not decoration: Qt and libvips are used under the LGPL, which
    // requires stating that they are used and under which licence. The component name links to its
    // GitHub project; the licence links to its full text (shipped in credits/licenses/).
    //
    // Cell values are data, not prose, so they stay outside tr() — only the headings are translated.
    const auto row = [](const QString& component, const QString& url, const QString& version,
                        const QString& spdx) {
        const QString name = url.isEmpty()
            ? component.toHtmlEscaped()
            : QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), component.toHtmlEscaped());
        // pm-licence:<spdx>?c=<component> — the query keeps the component name for the dialog title.
        const QString licenceHref =
            QStringLiteral("%1:%2?c=%3").arg(QLatin1String(kLicenceScheme), spdx, component);
        const QString licence =
            QStringLiteral("<a href=\"%1\">%2</a>").arg(licenceHref.toHtmlEscaped(), spdx.toHtmlEscaped());
        return QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
            .arg(name, version.toHtmlEscaped(), licence);
    };

    QString table =
        QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4'>")
        + tr("<tr><th align='left'>Component</th>"
             "<th align='left'>Version</th>"
             "<th align='left'>Licence</th></tr>")
        + row(QStringLiteral("Platemaker"),    QString::fromLatin1(kRepoUrl),    guiVer,
              QStringLiteral(PLATEMAKER_GUI_LICENCE))
        + row(QStringLiteral("libplatemaker"), QString::fromLatin1(kLibRepoUrl),
              orUnknown(QString::fromStdString(libBuild.version)),
              orUnknown(QString::fromStdString(libBuild.licence)))
        + row(QStringLiteral("Qt"),            QString::fromLatin1(kQtRepoUrl),  QStringLiteral(QT_VERSION_STR),
              QStringLiteral(PLATEMAKER_QT_LICENCE));

    // The lib's own linked dependencies (libvips, nlohmann/json), reported by the lib with the
    // runtime version and a GitHub url the lib guarantees.
    for (const auto& c : Platemaker::Infrastructure::linkedComponents()) {
        table += row(QString::fromStdString(c.name), QString::fromStdString(c.url),
                     orUnknown(QString::fromStdString(c.version)),
                     QString::fromStdString(c.licence));
    }
    table += QStringLiteral("</table>");

    ui->textAbout->setHtml(
        tr("<h2>Platemaker</h2>"
           "<p>Comic artist canvas tool — pre-processing and post-processing "
           "for webtoon-style publishing.</p>")
        + table
        + QStringLiteral("<p><a href=\"%1\">%1</a></p>").arg(QString::fromLatin1(kRepoUrl)));

    // Links on the About tab are handled here (not auto-followed): a licence link opens the licence
    // viewer, a component link opens its GitHub page. setOpenLinks(false) stops QTextBrowser from
    // navigating away from the About text; every click arrives as anchorClicked instead.
    ui->textAbout->setOpenLinks(false);
    ui->textAbout->setOpenExternalLinks(false);
    connect(ui->textAbout, &QTextBrowser::anchorClicked, this, [this](const QUrl& link) {
        if (link.scheme() == QLatin1String(kLicenceScheme)) {
            const QString spdx      = link.path();
            const QString component = QUrlQuery(link).queryItemValue(QStringLiteral("c"));
            const QString file      = licenceFileForSpdx(spdx);
            const QString path = file.isEmpty()
                ? QString()
                : QDir(QCoreApplication::applicationDirPath())
                      .filePath(QStringLiteral("credits/licenses/") + file);
            LicenceDialog dlg(this);
            dlg.showLicence(QStringLiteral("%1 — %2").arg(component, spdx), path);
            return;
        }
        // External links are GitHub only (the table renders no other host); guard anyway so a stray
        // link can never hand an untrusted URL to the browser.
        if (link.host() == QLatin1String("github.com"))
            QDesktopServices::openUrl(link);
    });

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
        "<a href=\"%1\">%1</a></p>")
        .arg(QString::fromLatin1(kWikiUrl)));

    ui->tabWidget->setCurrentIndex(static_cast<int>(initial));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
