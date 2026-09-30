/**
 * @file fonts.cpp
 * @brief *Tools → Fonts…*: the workspace's own fonts, the ones its bubbles miss, adding one, and installing
 *        one for this user.
 *
 * The workspace's `fonts/` is activated when it opens (loadWorkspace()); this is where it is looked at and
 * added to. Where a font comes from is this dialog's business alone — the font picker stays one plain list.
 */

#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include "workspacefolder.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QRawFont>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

//! Any size will do: a QRawFont is read here only for its names.
constexpr qreal k_probePixelSize = 12.0;

//! The layout's own size hint fits the headers and nothing else; a family name, a file name and a few
//! rows need room to be read without dragging the edges first.
constexpr qreal k_widthOverHint  = 2.5;
constexpr qreal k_heightOverHint = 1.5;

#ifdef Q_OS_WIN
//! How long a window that does not answer the font-change broadcast may hold the install up.
constexpr UINT k_broadcastTimeoutMs = 1000;
#endif

/**
 * @brief Installs \p file for the current user only — no administrator rights — the way Explorer's
 *        *Install* does (as opposed to *Install for all users*).
 *
 * Windows 10 1803+: a copy in `%LOCALAPPDATA%\Microsoft\Windows\Fonts`, a value under `HKCU\…\Fonts`
 * naming it, then `AddFontResource` and a `WM_FONTCHANGE` broadcast for the programs already running.
 * Elsewhere: a copy in the user's font folder, and a fontconfig refresh.
 *
 * A different file already installed under the same file name is never overwritten.
 *
 * @param displayName What the registry value is called: family and style, as Explorer names it.
 * @return Why it failed, or an empty string.
 */
QString installForUser(const QString &file, const QString &displayName)
{
#ifdef Q_OS_WIN
    const QString dir = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
                      + QStringLiteral("/Microsoft/Windows/Fonts");
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::FontsLocation);
#endif
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return MainWindow::tr("Could not create the folder:\n%1").arg(QDir::toNativeSeparators(dir));

    const QString dest = QDir(dir).filePath(QFileInfo(file).fileName());
    if (!copyUnlessIdentical(file, dest))
        return MainWindow::tr("A different font file with this name is already installed:\n%1")
            .arg(QDir::toNativeSeparators(dest));

#ifdef Q_OS_WIN
    const bool postScript = QFileInfo(file).suffix().compare(QStringLiteral("otf"), Qt::CaseInsensitive) == 0;
    QSettings  reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"),
                   QSettings::NativeFormat);
    reg.setValue(displayName + (postScript ? QStringLiteral(" (OpenType)") : QStringLiteral(" (TrueType)")),
                 QDir::toNativeSeparators(dest));
    reg.sync();
    if (reg.status() != QSettings::NoError)
        return MainWindow::tr("Could not register the font for this user.");
    const std::wstring path = QDir::toNativeSeparators(dest).toStdWString();
    AddFontResourceW(path.c_str());
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, k_broadcastTimeoutMs, &ignored);
#else
    QProcess::startDetached(QStringLiteral("fc-cache"), {QStringLiteral("-f"), dir});
#endif
    return {};
}

} // namespace

void MainWindow::onFonts()
{
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }
    const QString folder   = QFileInfo(m_workspacePath).absolutePath();
    const QString fontsDir = QDir(folder).filePath(QString::fromLatin1(k_workspaceFontsFolder));

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Fonts"));
    auto *layout = new QVBoxLayout(&dlg);

    auto *ownIntro = new QLabel(&dlg);
    ownIntro->setWordWrap(true);
    layout->addWidget(ownIntro);
    auto *own = new QTreeWidget(&dlg);
    own->setRootIsDecorated(false);
    own->setSelectionMode(QAbstractItemView::ExtendedSelection);
    own->setHeaderLabels({tr("Family"), tr("Style"), tr("File"), tr("Status"), tr("Also installed")});
    layout->addWidget(own);

    auto *missingIntro = new QLabel(&dlg);
    missingIntro->setWordWrap(true);
    layout->addWidget(missingIntro);
    auto *missing = new QTreeWidget(&dlg);
    missing->setRootIsDecorated(false);
    missing->setHeaderLabels({tr("Family"), tr("Objects")});
    layout->addWidget(missing);

    auto *buttons  = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto *addBtn   = buttons->addButton(tr("Add font…"), QDialogButtonBox::ActionRole);
    auto *showBtn  = buttons->addButton(tr("Show folder"), QDialogButtonBox::ActionRole);
    auto *installBtn = buttons->addButton(tr("Install"), QDialogButtonBox::ActionRole);
    installBtn->setToolTip(tr("Install the selected fonts for your user account, so other programs — and "
                              "Platemaker outside this workspace — can use them. No administrator rights."));
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    const auto populate = [&] {
        own->clear();
        for (const QString &file : workspaceFontFiles(folder)) {
            const QString name = QFileInfo(file).fileName();
            const QRawFont raw(file, k_probePixelSize);
            auto *row = new QTreeWidgetItem(own);
            row->setData(0, Qt::UserRole, file);
            row->setText(2, name);
            row->setToolTip(2, QDir::toNativeSeparators(file));
            if (const auto id = m_workspaceFonts.constFind(name); id != m_workspaceFonts.constEnd()) {
                // The family as the font database knows it — the name a bubble has to use.
                row->setText(0, QFontDatabase::applicationFontFamilies(*id).join(QStringLiteral(", ")));
                row->setText(3, tr("Active"));
            } else if (raw.isValid()) {
                row->setText(0, raw.familyName());
                row->setText(3, tr("Active from the next open"));
                row->setToolTip(3, tr("Added to the folder while the workspace was open."));
            } else {
                row->setText(3, tr("Not a font Platemaker can use"));
            }
            row->setText(1, raw.isValid() ? raw.styleName() : QString{});
            // Measured (PLAN-X M2.1): the workspace's copy wins over an installed one, so this says only
            // that another copy exists — not which one is drawn.
            if (!row->text(0).isEmpty() && m_installedFamilies.contains(row->text(0), Qt::CaseInsensitive)) {
                row->setText(4, tr("Yes"));
                row->setToolTip(4, tr("This workspace's copy is the one used while it is open."));
            }
            // Only a usable font that is not installed yet has anything to install.
            row->setData(4, Qt::UserRole, raw.isValid() && row->text(4).isEmpty());
        }
        ownIntro->setText(own->topLevelItemCount() == 0
                              ? tr("This workspace has no fonts of its own. Fonts added here are kept in its "
                                   "\"fonts\" folder and used while it is open — nothing is installed.")
                              : tr("This workspace's own fonts, kept in its \"fonts\" folder and used while it "
                                   "is open — nothing is installed."));

        missing->clear();
        QHash<QString, int> objects;
        QStringList         order;
        for (const auto &project : m_workspace.projectItems) {
            const ArtifactMap records = m_overlayArtifacts.artifacts(QString::fromStdString(project.uid));
            for (const QString &uid : objectsInStandIns(project)) {
                const QString family = records.value(uid).text.family;
                if (!objects.contains(family))
                    order << family;
                ++objects[family];
            }
        }
        for (const QString &family : std::as_const(order))
            new QTreeWidgetItem(missing, {family, QString::number(objects.value(family))});
        missingIntro->setText(order.isEmpty()
                                  ? tr("Every font the bubbles use is available.")
                                  : tr("Used by bubbles but not on this computer. They render as they were last "
                                       "saved; add the font here to edit them in it."));
        missing->setVisible(!order.isEmpty());
        for (QTreeWidget *tree : {own, missing})
            for (int c = 0; c < tree->columnCount(); ++c)
                tree->resizeColumnToContents(c);
    };

    const auto installable = [own] {
        QList<QTreeWidgetItem *> out;
        for (QTreeWidgetItem *row : own->selectedItems())
            if (row->data(4, Qt::UserRole).toBool())
                out << row;
        return out;
    };
    const auto updateInstall = [&] { installBtn->setEnabled(!installable().isEmpty()); };
    connect(own, &QTreeWidget::itemSelectionChanged, &dlg, updateInstall);

    connect(installBtn, &QPushButton::clicked, &dlg, [&] {
        QStringList installed, failed;
        for (QTreeWidgetItem *row : installable()) {
            const QString family = row->text(0);
            const QString style  = row->text(1);
            const QString error  = installForUser(
                row->data(0, Qt::UserRole).toString(),
                style.isEmpty() || style == QLatin1String("Regular") ? family : family + QLatin1Char(' ') + style);
            if (!error.isEmpty()) {
                failed << QStringLiteral("%1 — %2").arg(row->text(2), error);
                continue;
            }
            installed << family;
            if (!m_installedFamilies.contains(family, Qt::CaseInsensitive))
                m_installedFamilies << family;
            ui->textBrowserActionLogs->append(tr("Font installed for this user: %1").arg(row->text(2)));
        }
        installed.removeDuplicates();
        if (!installed.isEmpty())
            // Measured (PLAN-X M2.3): a running program does not see a font installed after it started —
            // Platemaker included, which is why it says so; this workspace keeps using its own copy.
            QMessageBox::information(&dlg, tr("Install"),
                                     tr("Installed for you: %1.\n\nPrograms that are already running — "
                                        "Platemaker included, once this workspace is closed — see it after "
                                        "they are restarted.").arg(installed.join(QStringLiteral(", "))));
        if (!failed.isEmpty())
            QMessageBox::warning(&dlg, tr("Install"),
                                 tr("Not installed:\n%1").arg(failed.join(QLatin1Char('\n'))));
        populate();
        updateInstall();
    });

    connect(showBtn, &QPushButton::clicked, &dlg, [&] {
        QDir().mkpath(fontsDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fontsDir));
    });

    connect(addBtn, &QPushButton::clicked, &dlg, [&] {
        const QStringList picked = QFileDialog::getOpenFileNames(
            &dlg, tr("Add Font"), QString(), tr("Fonts (*.ttf *.otf *.ttc)"));
        // It writes into the folder, so it asks the lock first, like every other write.
        if (picked.isEmpty() || !canWriteWorkspace())
            return;
        if (!QDir().mkpath(fontsDir)) {
            QMessageBox::warning(&dlg, tr("Add Font"),
                                 tr("Could not create the folder:\n%1").arg(QDir::toNativeSeparators(fontsDir)));
            return;
        }

        QStringList added, failed;
        for (const QString &source : picked) {
            const QString name    = QFileInfo(source).fileName();
            const QString dest    = QDir(fontsDir).filePath(name);
            const bool    existed = QFileInfo::exists(dest);
            if (!copyUnlessIdentical(source, dest)) {
                failed << tr("%1 — a different file with this name is already in the folder").arg(name);
                continue;
            }
            if (m_workspaceFonts.contains(name))
                continue;   // the very same file, active already
            const int id = QFontDatabase::addApplicationFont(dest);
            if (id < 0) {
                if (!existed)
                    QFile::remove(dest);   // ours to take back: nothing else put it there
                failed << tr("%1 — not a font Platemaker can use").arg(name);
                continue;
            }
            m_workspaceFonts.insert(name, id);
            added << QFontDatabase::applicationFontFamilies(id);
            ui->textBrowserActionLogs->append(tr("Font added to the workspace: %1").arg(name));
        }

        if (!added.isEmpty()) {
            added.removeDuplicates();
            refreshAllAdvisories();
            statusBar()->showMessage(tr("Added %1.").arg(added.join(QStringLiteral(", "))), k_noticeMs);
            // What was baked in a stand-in is re-set now, as at open — the lock was asked above. Not a step
            // in the history: it repairs files, it is not an edit, and undoing it would only break them again.
            // ponytail: a step recorded before this still carries the old sha256 (and, for a lettered
            // picture, the old wrapper), so undoing past it shows the stand-in again until the next open's
            // heal; rewrite the history's states too if that turns out to matter.
            healFontFallbacks();
        }
        if (!failed.isEmpty())
            QMessageBox::warning(&dlg, tr("Add Font"),
                                 tr("Not added:\n%1").arg(failed.join(QLatin1Char('\n'))));
        populate();
    });

    populate();
    updateInstall();
    dlg.resize(qRound(dlg.sizeHint().width() * k_widthOverHint), qRound(dlg.sizeHint().height() * k_heightOverHint));
    dlg.exec();
}
