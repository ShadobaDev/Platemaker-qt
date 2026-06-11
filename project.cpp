#include "project.h"
#include "imagetile.h"
#include "ui_project.h"
#include "imagetile.h"

Project::Project(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Project)
{
    ui->setupUi(this);

    // --- KOD DO TESTÓW: DODAJEMY 5 KAFELKÓW ---
    for (int i = 0; i < 5; ++i) {
        addImageTile(QString("Panel_%1.png").arg(i + 1));
    }
}

Project::~Project()
{
    delete ui;
}

void Project::addImageTile(const QString &tileName) {
    QListWidgetItem *item = new QListWidgetItem(ui->listImageTile);
    ImageTile *newTile = new ImageTile(this);
    newTile->setTileName(tileName);
    item->setSizeHint(newTile->sizeHint());
    ui->listImageTile->setItemWidget(item, newTile);
}
