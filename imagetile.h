#ifndef IMAGETILE_H
#define IMAGETILE_H

#include <QPixmap>
#include <QWidget>

#include <platemaker/models/project_item.hpp>

namespace Ui { class imagetile; }

class ImageTile : public QWidget
{
    Q_OBJECT

public:
    explicit ImageTile(QWidget *parent = nullptr);
    ~ImageTile();

    void setFileInfo(const QString& filePath,
                     Platemaker::Models::FileStatus status,
                     const QString& cacheDir);

    void setThumbnail(const QPixmap& pixmap);

    // Legacy — kept so existing test code still compiles
    void setTileName(const QString& name);

private:
    void updateStatusStyle(Platemaker::Models::FileStatus status);
    void loadThumbnailAsync(const QString& cacheDir);

    Ui::imagetile* ui;
    QString m_filePath;
};

#endif // IMAGETILE_H
