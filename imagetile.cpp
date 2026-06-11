#include "imagetile.h"
#include "ui_imagetile.h"

ImageTile::ImageTile(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::imagetile)
{
    ui->setupUi(this);
}

ImageTile::~ImageTile()
{
    delete ui;
}

void ImageTile::setTileName(const QString &name) {
    // Ustawiamy tekst w komponencie QLabel wewnątrz kafelka
    // Podmień 'imageLabel' na rzeczywistą nazwę obiektu z Twojego imagetile.ui
    ui->imageLabel->setText(name);
}
