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

    // Let mouse presses on the display areas fall through to the QListWidget
    // viewport so it can start a drag (the tile is set via setItemWidget, which
    // otherwise swallows the press). The buttons stay interactive.
    for (QWidget* w : {static_cast<QWidget*>(ui->frame),
                       static_cast<QWidget*>(ui->imageLabel),
                       static_cast<QWidget*>(ui->textBrowser),
                       static_cast<QWidget*>(ui->widget)}) {
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
                             const QString& cacheDir)
{
    m_filePath = filePath;

    const QString filename = QFileInfo(filePath).fileName();
    ui->imageLabel->setText(filename);

    QString statusText;
    switch (status) {
        case FileStatus::Pending:        statusText = "Pending";       break;
        case FileStatus::Processed:      statusText = "Processed";     break;
        case FileStatus::Modified:       statusText = "Modified";      break;
        case FileStatus::Missing:        statusText = "Missing";       break;
        case FileStatus::Desynchronized: statusText = "Out of sync";   break;
        case FileStatus::Done:           statusText = "Done";          break;
    }
    ui->textBrowser->setText(filename + "\n" + statusText);

    updateStatusStyle(status);

    if (!cacheDir.isEmpty())
        loadThumbnailAsync(cacheDir);
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

void ImageTile::updateStatusStyle(FileStatus status)
{
    QString color;
    switch (status) {
        case FileStatus::Processed:     color = "#22c55e"; break; // green
        case FileStatus::Done:          color = "#22c55e"; break; // green
        case FileStatus::Modified:      color = "#f97316"; break; // orange
        case FileStatus::Missing:       color = "#ef4444"; break; // red
        case FileStatus::Desynchronized:color = "#eab308"; break; // amber — out of sync with config
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
