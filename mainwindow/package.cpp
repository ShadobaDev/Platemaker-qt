/**
 * @file package.cpp
 * @brief *File → Export package… / Open package…*: the library's package of the workspace, plus what only
 *        the GUI knows.
 *
 * The library plans every file the model names (WorkspacePackager::plan()). Two kinds of file the model
 * does not name are added here, so the package is editable on the other machine and not only renderable:
 *  - **the picture behind a lettered picture** — named by its record, beside the wrapper, never by a path
 *    in the model (the same rule collectOverlayFiles() follows);
 *  - **fonts** — the workspace's own `fonts/`, whole, and the installed files of every family a bubble
 *    names. The platform default (a record with no family) is not packed: it was not chosen, and on a
 *    machine without it the missing-fonts advisory says so.
 */

#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include "fontfiles.hpp"
#include "workspacefolder.hpp"

#include <platemaker/infrastructure/build_info/build_info.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSet>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <memory>
#include <optional>

namespace {

using Platemaker::Infrastructure::PackagePlan;
using Platemaker::Infrastructure::WorkspacePackager;

//! What a package file is called.
constexpr char k_packageSuffix[] = ".platemaker.zip";

//! The progress bar counts in these steps, whatever the byte count — an int cannot hold every package's.
constexpr int k_progressSteps = 1000;

using Platemaker::Infrastructure::PackageManifest;
using Platemaker::Infrastructure::PackageProgress;
using Platemaker::Infrastructure::UnpackedPackage;
using Token = Platemaker::Infrastructure::CancellationToken;

//! How a job run behind a progress dialog ended: finished, cancelled (neither set), or failed.
struct Outcome {
    bool    finished = false;
    QString error;
};

/**
 * Runs \p job on a worker behind a window-modal progress dialog with a Cancel button, and waits for it.
 * \p job reports through the progress sink it is handed, checks the token between chunks, and returns false
 * when it was cancelled; an exception it throws becomes the outcome's error.
 */
Outcome behindProgress(QWidget *parent, const QString &label,
                       std::function<bool(const PackageProgress &, const Token *)> job)
{
    QProgressDialog progress(label, MainWindow::tr("Cancel"), 0, k_progressSteps, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    auto cancel = std::make_shared<Token>();
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, [cancel] { cancel->cancel(); });

    // The dialog outlives the worker (the loop below waits for it), and a call still queued when the dialog
    // goes is dropped with it, so a plain pointer is enough.
    QProgressDialog        *bar = &progress;
    QFutureWatcher<Outcome> watcher;
    QEventLoop              wait;
    QObject::connect(&watcher, &QFutureWatcher<Outcome>::finished, &wait, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([job = std::move(job), cancel, bar]() {
        Outcome out;
        try {
            out.finished = job(
                [bar](std::uint64_t done, std::uint64_t total) {
                    const int step = total == 0 ? k_progressSteps
                                                : static_cast<int>(done * k_progressSteps / total);
                    QMetaObject::invokeMethod(bar, [bar, step] { bar->setValue(step); }, Qt::QueuedConnection);
                },
                cancel.get());
        } catch (const std::exception &e) {
            out.error = QString::fromUtf8(e.what());
        }
        return out;
    }));
    wait.exec();
    progress.reset();
    return watcher.result();
}

//! A file compared as a file, not as a spelling.
QString identity(const QString &path)
{
    const QFileInfo fi(path);
    const QString   canonical = fi.canonicalFilePath();
    return (canonical.isEmpty() ? fi.absoluteFilePath() : canonical).toLower();
}

//! What the GUI adds to a plan, and what it could not.
struct GuiPart {
    QStringList fonts;            //!< Font files packed, as targets.
    QStringList fontsNotPacked;   //!< Families a bubble names that no file could be found for.
    QStringList picturesMissing;  //!< Pictures behind lettered pictures that are not on disk.
};

//! Adds one file to \p plan unless its place is taken. The same file at the same place is not an error.
class Appender {
public:
    explicit Appender(PackagePlan &plan) : m_plan(plan)
    {
        for (const auto &f : plan.files) {
            const QString target = QString::fromStdString(f.target);
            m_taken.insert(target.toLower(), identity(QString::fromStdString(f.source)));
            m_targetOf.insert(identity(QString::fromStdString(f.source)), target);
        }
    }

    //! Where the plan put \p source, or empty.
    QString targetOf(const QString &source) const { return m_targetOf.value(identity(source)); }

    //! False when \p target already holds a different file.
    bool add(const QString &source, const QString &target)
    {
        const QString id = identity(source);
        if (const auto it = m_taken.constFind(target.toLower()); it != m_taken.constEnd())
            return *it == id;
        m_taken.insert(target.toLower(), id);
        m_targetOf.insert(id, target);
        m_plan.files.push_back({QDir::fromNativeSeparators(source).toStdString(), target.toStdString()});
        return true;
    }

private:
    PackagePlan            &m_plan;
    QHash<QString, QString> m_taken;      //!< target (lower case) → the file there
    QHash<QString, QString> m_targetOf;   //!< file → its target
};

} // namespace

void MainWindow::onExportPackage()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // --- Where: beside the workspace's folder, not inside it — the folder is what the package holds ---
    const QFileInfo ws(m_workspacePath);
    QString         name = ws.fileName();
    name.chop(QString::fromLatin1(k_workspaceFilePattern).size() - 1);   // "*.platemaker.json" less its '*'
    QDir            beside(ws.absolutePath());
    beside.cdUp();
    QString zip = QFileDialog::getSaveFileName(
        this, tr("Export Package"), beside.filePath(name + QLatin1String(k_packageSuffix)),
        tr("Platemaker package (*%1)").arg(QLatin1String(k_packageSuffix)));
    if (zip.isEmpty())
        return;
    if (!zip.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        zip += QLatin1String(k_packageSuffix);

    // --- What: the library's plan, then the GUI's own files -------------------------------------------
    PackagePlan plan;
    try {
        plan = WorkspacePackager::plan(m_workspace, m_workspacePath.toStdString());
    } catch (const std::exception &e) {
        QMessageBox::critical(this, tr("Export Package"), tr("Cannot plan the package:\n%1").arg(e.what()));
        return;
    }

    GuiPart  gui;
    Appender files(plan);

    QSet<QString> named;   // families a bubble asks for by name
    for (const auto &project : m_workspace.projectItems) {
        const ArtifactMap records = m_overlayArtifacts.artifacts(QString::fromStdString(project.uid));
        for (const auto &overlay : project.getStripOverlays()) {
            const auto rec = records.constFind(QString::fromStdString(overlay.uid));
            if (rec == records.constEnd())
                continue;
            if (!rec->text.family.isEmpty())
                named.insert(rec->text.family);
            if (!rec->isArtwork())
                continue;
            // The picture goes beside its wrapper, under the name its record gives — never renamed.
            const QString asset   = QString::fromStdString(overlay.assetPath);
            const QString wrapper = files.targetOf(asset);
            const QString picture = QDir(QFileInfo(asset).absolutePath()).filePath(rec->artwork);
            if (wrapper.isEmpty() || !QFileInfo::exists(picture)) {
                gui.picturesMissing << QDir::toNativeSeparators(picture);
                continue;
            }
            const QString target = QFileInfo(wrapper).path() + QLatin1Char('/') + rec->artwork;
            if (!files.add(picture, target))
                gui.picturesMissing << QDir::toNativeSeparators(picture);   // content-named: cannot happen
        }
    }

    // The workspace's own fonts, whole (a licence travels with its font), and the families they bring.
    const QString fontsTarget = QString::fromLatin1(k_workspaceFontsFolder) + QLatin1Char('/');
    const QDir    ownFonts(QDir(ws.absolutePath()).filePath(QString::fromLatin1(k_workspaceFontsFolder)));
    for (const QFileInfo &f : ownFonts.entryInfoList(QDir::Files, QDir::Name))
        if (files.add(f.absoluteFilePath(), fontsTarget + f.fileName()))
            gui.fonts << fontsTarget + f.fileName();
    QSet<QString> brought;
    for (const int id : std::as_const(m_workspaceFonts))
        for (const QString &family : QFontDatabase::applicationFontFamilies(id))
            brought.insert(family.toLower());

    // Every other family a bubble names, from the files it is installed in.
    QStringList wanted(named.begin(), named.end());
    wanted.sort(Qt::CaseInsensitive);
    for (const QString &family : std::as_const(wanted)) {
        if (brought.contains(family.toLower()))
            continue;
        const QStringList installed = installedFontFiles(family);
        bool              packed    = !installed.isEmpty();
        for (const QString &file : installed) {
            const QString target = fontsTarget + QFileInfo(file).fileName();
            if (files.add(file, target)) {
                if (!gui.fonts.contains(target))
                    gui.fonts << target;
            } else {
                packed = false;   // another font's file of the same name is there already
            }
        }
        if (!packed)
            gui.fontsNotPacked << family;
    }

    QJsonObject details;
    details[QStringLiteral("editable")]        = gui.fontsNotPacked.isEmpty() && gui.picturesMissing.isEmpty();
    details[QStringLiteral("fonts")]           = QJsonArray::fromStringList(gui.fonts);
    details[QStringLiteral("fontsNotPacked")]  = QJsonArray::fromStringList(gui.fontsNotPacked);
    details[QStringLiteral("picturesMissing")] = QJsonArray::fromStringList(gui.picturesMissing);
    plan.applicationName     = QCoreApplication::applicationName().toStdString();
    plan.applicationVersion  = QCoreApplication::applicationVersion().toStdString();
    plan.applicationManifest = QJsonDocument(details).toJson(QJsonDocument::Compact).toStdString();

    // --- What will be missing, said before anything is written -----------------------------------------
    QStringList gaps;
    for (const auto &m : plan.missing)
        gaps << tr("Missing file: %1").arg(QDir::toNativeSeparators(QString::fromStdString(m.source)));
    for (const QString &p : std::as_const(gui.picturesMissing))
        gaps << tr("Missing picture behind lettering: %1").arg(p);
    for (const QString &f : std::as_const(gui.fontsNotPacked))
        gaps << tr("Font not packed (no file found): %1").arg(f);
    if (!gaps.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Export Package"),
                        tr("%n item(s) cannot go into the package. It can still be exported: missing pages "
                           "stay listed in it, and can be replaced after it is opened.", "",
                           static_cast<int>(gaps.size())),
                        QMessageBox::Cancel, this);
        box.setDetailedText(gaps.join(QLatin1Char('\n')));
        QPushButton *go = box.addButton(tr("Export anyway"), QMessageBox::AcceptRole);
        box.setDefaultButton(go);
        box.exec();
        if (box.clickedButton() != go)
            return;
    }

    // --- Written on a worker; the window waits, and can cancel --------------------------------------
    const std::string target = zip.toStdString();
    const Outcome     out    = behindProgress(this, tr("Exporting package…"),
                                     [plan = std::move(plan), target](const PackageProgress &p, const Token *c) {
                                         return WorkspacePackager::write(plan, target, p, c);
                                     });
    if (!out.error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Package"), tr("The package was not written:\n%1").arg(out.error));
        return;
    }
    if (!out.finished) {
        statusBar()->showMessage(tr("Export cancelled — nothing was written."), k_noticeMs);
        return;
    }
    ui->textBrowserActionLogs->append(tr("Package exported: %1").arg(QDir::toNativeSeparators(zip)));
    statusBar()->showMessage(tr("Package exported: %1").arg(QFileInfo(zip).fileName()), k_noticeMs);
}

void MainWindow::onOpenPackage()
{
    if (m_rendering) { setProjectStatus(tr("Stop the current render first.")); return; }
    if (!maybeSave()) return;

    const QString zip = QFileDialog::getOpenFileName(
        this, tr("Open Package"), defaultDialogDir(),
        tr("Platemaker package (*%1);;Zip archives (*.zip)").arg(QLatin1String(k_packageSuffix)));
    if (zip.isEmpty())
        return;

    // --- Into a new folder named after the package — never into one that exists, so nothing is overwritten
    //     and the folder holds this one workspace (W1) by construction.
    QString name = QFileInfo(zip).fileName();
    if (name.endsWith(QLatin1String(k_packageSuffix), Qt::CaseInsensitive))
        name.chop(static_cast<int>(qstrlen(k_packageSuffix)));
    else
        name = QFileInfo(zip).completeBaseName();

    QString folder;
    QString startAt = QFileInfo(zip).absolutePath();
    for (;;) {
        const QString where = QFileDialog::getExistingDirectory(
            this, tr("Unpack \"%1\" into a new folder in…").arg(name), startAt);
        if (where.isEmpty())
            return;
        folder = QDir(where).filePath(name);
        if (!QFileInfo::exists(folder))
            break;
        QMessageBox::information(this, tr("Open Package"),
                                 tr("There is already a folder named \"%1\" in\n%2\n\nChoose another place: a "
                                    "package is always unpacked into a new folder of its own.")
                                     .arg(name, QDir::toNativeSeparators(where)));
        startAt = where;
    }

    std::optional<UnpackedPackage> unpacked;
    const std::string              source = zip.toStdString();
    const std::string              target = folder.toStdString();
    const Outcome out = behindProgress(this, tr("Unpacking package…"),
                                       [&unpacked, source, target](const PackageProgress &p, const Token *c) {
                                           unpacked = WorkspacePackager::unpack(source, target, p, c);
                                           return unpacked.has_value();
                                       });
    if (!out.error.isEmpty()) {
        QMessageBox::critical(this, tr("Open Package"), tr("The package was not opened:\n%1").arg(out.error));
        return;
    }
    if (!out.finished) {
        statusBar()->showMessage(tr("Opening the package was cancelled — nothing was unpacked."), k_noticeMs);
        return;
    }
    ui->textBrowserActionLogs->append(tr("Package unpacked into: %1").arg(QDir::toNativeSeparators(folder)));

    // --- Opened like any workspace: the lock, its fonts/, the heal and the sweep all follow ---------------
    const QString workspace = QString::fromStdString(unpacked->workspaceFile);
    loadWorkspace(workspace);
    if (m_workspacePath.isEmpty() || QFileInfo(m_workspacePath) != QFileInfo(workspace))
        return;   // not opened (the load said why); the unpacked folder stays, and opens like any other

    // After what the open itself reports (queued before this), so this reads as the last word on it.
    const PackageManifest manifest = unpacked->manifest;
    QTimer::singleShot(0, this, [this, manifest] {
        // Nothing is blocked by a version difference; it only explains a byte difference in a render.
        const QString app = QCoreApplication::applicationVersion();
        const QString lib = QString::fromStdString(Platemaker::Infrastructure::buildInfo().version);
        const QString byApp = QString::fromStdString(manifest.applicationVersion);
        const QString byLib = QString::fromStdString(manifest.libraryVersion);
        if ((!byApp.isEmpty() && byApp != app) || (!byLib.isEmpty() && byLib != lib)) {
            const QString note = tr("This package was exported by %1 %2 (libplatemaker %3); this is Platemaker %4 "
                                    "(libplatemaker %5). Renders from it may differ slightly.")
                                     .arg(QString::fromStdString(manifest.applicationName), byApp, byLib, app, lib);
            ui->textBrowserActionLogs->append(note);
            statusBar()->showMessage(note, k_noticeMs);
        }

        const QJsonObject details =
            QJsonDocument::fromJson(QByteArray::fromStdString(manifest.applicationDetails)).object();
        if (details.contains(QStringLiteral("editable")) && !details.value(QStringLiteral("editable")).toBool())
            ui->textBrowserActionLogs->append(
                tr("This package was exported without some fonts or pictures behind lettering, so some "
                   "lettering may not be editable as it was. It renders as it did."));

        // The fonts it brought are active; offered once to be installed, for everything else too.
        QStringList notInstalled;
        for (const int id : std::as_const(m_workspaceFonts))
            for (const QString &family : QFontDatabase::applicationFontFamilies(id))
                if (!notInstalled.contains(family) && !isInstalledFamily(family))
                    notInstalled << family;
        if (!notInstalled.isEmpty()
            && QMessageBox::question(this, tr("Open Package"),
                                     tr("This package brought fonts that are not installed on this computer:\n%1\n\n"
                                        "They are used while this workspace is open. Open Fonts to install them "
                                        "for your account as well?")
                                         .arg(notInstalled.join(QStringLiteral(", "))))
                   == QMessageBox::Yes)
            onFonts();
    });
}
