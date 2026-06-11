#ifndef IMAGETILE_H
#define IMAGETILE_H

#include <QWidget>

namespace Ui {
class imagetile;
}

class ImageTile : public QWidget
{
    Q_OBJECT

public:
    explicit ImageTile(QWidget *parent = nullptr);
    ~ImageTile();

    void setTileName(const QString &name);
private:
    Ui::imagetile *ui;
};

#endif // IMAGETILE_H
