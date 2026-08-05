#include "imagetile.h"
#include "ui_imagetile.h"

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QPixmap>
#include <QtConcurrent/QtConcurrentRun>

using Platemaker::Models::FileStatus;

ImageTile::ImageTile(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::imagetile)
{
    ui->setupUi(this);

    // Fixed thumbnail area so every tile has the same height and thumbnails
    // are always fully visible (KeepAspectRatio in setThumbnail fits within this box).
    ui->imageLabel->setFixedSize(160, 120);
    ui->imageLabel->setAlignment(Qt::AlignCenter);
    ui->imageLabel->setScaledContents(false);

    // Let mouse presses on the passive display areas fall through to the QListWidget viewport so it
    // can start a drag (the tile is set via setItemWidget, which otherwise swallows the press) —
    // WITHOUT disabling the ▲/▼ move buttons.
    //
    // Only the leaf display widgets get WA_TransparentForMouseEvents. A transparent widget hides its
    // whole subtree from hit-testing (QWidget::childAt skips it and never descends), so the buttons'
    // ancestors — `frame` and `widget` — must NOT be transparent, or the buttons would be dead
    // (they are grandchildren of `frame`). Presses that land on `frame` or the button container are
    // ignored by those plain widgets and propagate up to the list viewport, so the drag still starts
    // from anywhere on the tile; presses on the buttons reach them normally.
    for (QWidget* w : {static_cast<QWidget*>(ui->imageLabel),
                       static_cast<QWidget*>(ui->textBrowser)}) {
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    connect(ui->pushButtonMoveUp, &QPushButton::clicked, this, [this]{
        emit moveUpRequested(m_filePath);
    });
    connect(ui->pushButtonMoveDown, &QPushButton::clicked, this, [this]{
        emit moveDownRequested(m_filePath);
    });
}

ImageTile::~ImageTile()
{
    delete ui;
}

void ImageTile::setTileName(const QString& name)
{
    ui->imageLabel->setText(name);
}

void ImageTile::setFileInfo(const QString& filePath,
                             FileStatus status,
                             const QString& cacheDir,
                             bool renderedWithoutProfile)
{
    m_filePath = filePath;

    ui->imageLabel->setText(QFileInfo(filePath).fileName());

    setStatus(status, renderedWithoutProfile);

    if (!cacheDir.isEmpty())
        loadThumbnailAsync(cacheDir);
}

void ImageTile::setStatus(FileStatus status, bool renderedWithoutProfile)
{
    const QString filename = QFileInfo(m_filePath).fileName();

    // A page rendered without a canvas profile is still Processed, but flagged so the "no margins"
    // fact is visible instead of hidden behind a plain green tile. Only meaningful for Processed.
    const bool noProfile = (status == FileStatus::Processed) && renderedWithoutProfile;

    QString statusText;
    switch (status) {
        case FileStatus::Pending:        statusText = "Pending";       break;
        case FileStatus::Processed:      statusText = noProfile ? "Processed (no canvas profile)"
                                                                : "Processed";                 break;
        case FileStatus::Modified:       statusText = "Modified";      break;
        case FileStatus::Missing:        statusText = "Missing";       break;
        case FileStatus::Desynchronized: statusText = "Out of sync";   break;
        case FileStatus::Done:           statusText = "Done";          break;
        case FileStatus::Skipped:        statusText = "Skipped";       break;
        case FileStatus::Error:          statusText = "Unverified";    break;
    }
    ui->textBrowser->setText(filename + "\n" + statusText);

    updateStatusStyle(status, noProfile);
}

void ImageTile::setMoveControlsVisible(bool visible)
{
    // `ui->widget` is the container holding both move buttons (see the .ui). Hiding it removes the
    // whole button column; a hidden widget is skipped by the box layout, so the thumbnail/text reclaim
    // the space.
    ui->widget->setVisible(visible);
}

void ImageTile::setThumbnail(const QPixmap& pixmap)
{
    // Skip if the pixmap is null (e.g., failed to load or generate thumbnail).
    if (pixmap.isNull()) return;
    // Fit within the fixed label area — whole image always visible, aspect ratio kept.
    ui->imageLabel->setPixmap(
        pixmap.scaled(ui->imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->imageLabel->setText({});
}

void ImageTile::updateStatusStyle(FileStatus status, bool renderedWithoutProfile)
{
    // Rendered without a canvas profile: cyan, distinct from the plain-green "Processed" (a profile
    // was applied) and from the amber "Out of sync" it must not be confused with.
    if (status == FileStatus::Processed && renderedWithoutProfile) {
        ui->frame->setStyleSheet(
            "QFrame { border-left: 4px solid #06b6d4; border-radius: 0px; }"); // cyan
        return;
    }

    QString color;
    switch (status) {
        case FileStatus::Processed:     color = "#22c55e"; break; // green
        case FileStatus::Done:          color = "#22c55e"; break; // green
        case FileStatus::Modified:      color = "#f97316"; break; // orange
        case FileStatus::Missing:       color = "#ef4444"; break; // red
        case FileStatus::Desynchronized:color = "#eab308"; break; // amber — out of sync with config
        case FileStatus::Skipped:       color = "#a855f7"; break; // violet — render left this page out
        case FileStatus::Error:         color = "#e11d48"; break; // rose — rendered but hash unverifiable
        default:                        color = "#6b7280"; break; // gray (Pending)
    }
    ui->frame->setStyleSheet(
        QString("QFrame { border-left: 4px solid %1; border-radius: 0px; }").arg(color));
}

void ImageTile::loadThumbnailAsync(const QString& cacheDir)
{
    // Use a QFutureWatcher to run the thumbnail generation in a separate thread.
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString thumbPath = watcher->result();
        if (!thumbPath.isEmpty())
            setThumbnail(QPixmap(thumbPath));
        watcher->deleteLater();
    });

    // Capture the file path and cache directory as std::string to avoid issues with QString lifetime in the lambda.
    const std::string filePathStd  = m_filePath.toStdString();
    const std::string cacheDirStd  = cacheDir.toStdString();
    watcher->setFuture(QtConcurrent::run([filePathStd, cacheDirStd]() -> QString {
        try {
            Platemaker::Infrastructure::ThumbnailCache cache(cacheDirStd);
            return QString::fromStdString(cache.getOrGenerate(filePathStd));
        } catch (...) {
            return {};
        }
    }));
}
