/**
 * @file package.cpp
 * @brief *File → Export package…*: the library's package of the workspace, plus what only the GUI knows.
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
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace {

using Platemaker::Infrastructure::PackagePlan;
using Platemaker::Infrastructure::WorkspacePackager;

//! What a package file is called.
constexpr char k_packageSuffix[] = ".platemaker.zip";

//! The progress bar counts in these steps, whatever the byte count — an int cannot hold every package's.
constexpr int k_progressSteps = 1000;

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
    QProgressDialog progress(tr("Exporting package…"), tr("Cancel"), 0, k_progressSteps, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    auto cancel = std::make_shared<Platemaker::Infrastructure::CancellationToken>();
    connect(&progress, &QProgressDialog::canceled, this, [cancel] { cancel->cancel(); });

    struct Outcome {
        bool    written = false;
        QString error;
    };
    // The dialog outlives the worker (the loop below waits for it), and a call still queued when the dialog
    // goes is dropped with it, so a plain pointer is enough.
    QProgressDialog          *bar    = &progress;
    const std::string         target = zip.toStdString();
    QFutureWatcher<Outcome>   watcher;
    QEventLoop                wait;
    connect(&watcher, &QFutureWatcher<Outcome>::finished, &wait, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([plan = std::move(plan), target, cancel, bar]() {
        Outcome out;
        try {
            out.written = WorkspacePackager::write(
                plan, target,
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

    const Outcome out = watcher.result();
    if (!out.error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Package"), tr("The package was not written:\n%1").arg(out.error));
        return;
    }
    if (!out.written) {
        statusBar()->showMessage(tr("Export cancelled — nothing was written."), k_noticeMs);
        return;
    }
    ui->textBrowserActionLogs->append(tr("Package exported: %1").arg(QDir::toNativeSeparators(zip)));
    statusBar()->showMessage(tr("Package exported: %1").arg(QFileInfo(zip).fileName()), k_noticeMs);
}
