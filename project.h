#ifndef PROJECT_H
#define PROJECT_H

#include <QWidget>

namespace Ui {
class Project;
}

class Project : public QWidget
{
    Q_OBJECT

public:
    explicit Project(QWidget *parent = nullptr);
    ~Project();


    void addImageTile(const QString &tileName);

private:
    Ui::Project *ui;
};

#endif // PROJECT_H
