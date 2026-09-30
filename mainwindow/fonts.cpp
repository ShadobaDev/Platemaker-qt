/**
 * @file fonts.cpp
 * @brief *Tools → Fonts…*: the workspace's own fonts, the ones its bubbles miss, and adding one.
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
#include <QRawFont>
#include <QStatusBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

//! Any size will do: a QRawFont is read here only for its names.
constexpr qreal k_probePixelSize = 12.0;

//! The layout's own size hint fits the headers and nothing else; a family name, a file name and a few
//! rows need room to be read without dragging the edges first.
constexpr qreal k_widthOverHint  = 2.5;
constexpr qreal k_heightOverHint = 1.5;

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
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    const auto populate = [&] {
        own->clear();
        for (const QString &file : workspaceFontFiles(folder)) {
            const QString name = QFileInfo(file).fileName();
            const QRawFont raw(file, k_probePixelSize);
            auto *row = new QTreeWidgetItem(own);
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
    dlg.resize(qRound(dlg.sizeHint().width() * k_widthOverHint), qRound(dlg.sizeHint().height() * k_heightOverHint));
    dlg.exec();
}
