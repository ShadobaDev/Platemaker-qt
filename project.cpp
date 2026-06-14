#include "project.h"
#include "ui_project.h"
#include "imagetile.h"

#include <QDir>
#include <QFileDialog>
#include <QListWidgetItem>

#include <algorithm>

using namespace Platemaker::Models;

Project::Project(int projectIndex,
                 Workspace& workspace,
                 const QString& cacheDir,
                 QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::Project)
    , m_projectIndex(projectIndex)
    , m_workspace(workspace)
    , m_cacheDir(cacheDir)
{
    ui->setupUi(this);

    connect(ui->pushButton, &QPushButton::clicked, this, &Project::onAddFromDirectory);
    connect(ui->listImageTile->model(), &QAbstractItemModel::rowsMoved,
            this, &Project::onRowsMoved);

    populate();
}

Project::~Project()
{
    delete ui;
}

void Project::populate()
{
    ui->listImageTile->clear();

    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();

    std::vector<const InputFile*> sorted;
    sorted.reserve(inputs.size());
    for (const auto& f : inputs)
        sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    for (const InputFile* f : sorted)
        addImageTile(*f);
}

void Project::addImageTile(const InputFile& file)
{
    auto* listItem = new QListWidgetItem(ui->listImageTile);
    listItem->setData(Qt::UserRole, QString::fromStdString(file.filePath));

    auto* tile = new ImageTile(this);
    tile->setFileInfo(QString::fromStdString(file.filePath), file.status, m_cacheDir);

    listItem->setSizeHint(tile->sizeHint());
    ui->listImageTile->setItemWidget(listItem, tile);
}

void Project::onAddFromDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Input Directory"));
    if (dir.isEmpty()) return;

    auto& item = m_workspace.projectItems[m_projectIndex];
    item.inputDirectory = dir.toStdString();

    const QStringList filters = {"*.jpg","*.jpeg","*.png","*.webp","*.tif","*.tiff"};
    const auto entries = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Name);

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (const auto& e : entries)
        paths.push_back(e.absoluteFilePath().toStdString());

    item.mergeFileScan(paths);
    populate();
    emit projectModified();
}

void Project::onRowsMoved()
{
    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();
    for (int i = 0; i < ui->listImageTile->count(); ++i) {
        const QString path = ui->listImageTile->item(i)->data(Qt::UserRole).toString();
        for (auto& f : inputs) {
            if (QString::fromStdString(f.filePath) == path) {
                f.order = i;
                break;
            }
        }
    }
    emit projectModified();
}
