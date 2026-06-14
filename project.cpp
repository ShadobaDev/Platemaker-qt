#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "canvasprofiledialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QToolButton>

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

    connect(ui->pushButtonAddInputsFromDir, &QPushButton::clicked,
            this, &Project::onAddFromDirectory);
    connect(ui->listInputImageTile->model(), &QAbstractItemModel::rowsMoved,
            this, &Project::onRowsMoved);

    connect(ui->pushButtonAssignCanvasProfiles, &QPushButton::clicked,
            this, &Project::onAssignCanvasProfiles);
    connect(ui->listWidgetCanvasProfiles, &QListWidget::itemDoubleClicked,
            this, &Project::onCanvasProfileDoubleClicked);

    connect(ui->comboBoxOutputProfile, &QComboBox::currentIndexChanged,
            this, &Project::onOutputProfileChanged);

    populate();
}

Project::~Project()
{
    delete ui;
}

void Project::populate()
{
    ui->listInputImageTile->clear();

    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();

    std::vector<const InputFile*> sorted;
    sorted.reserve(inputs.size());
    for (const auto& f : inputs)
        sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    for (const InputFile* f : sorted)
        addImageTile(*f);

    refreshCanvasProfilesList();
    refreshOutputProfileCombo();
}

void Project::addImageTile(const InputFile& file)
{
    auto* listItem = new QListWidgetItem(ui->listInputImageTile);
    listItem->setData(Qt::UserRole, QString::fromStdString(file.filePath));

    auto* tile = new ImageTile(this);
    tile->setFileInfo(QString::fromStdString(file.filePath), file.status, m_cacheDir);

    listItem->setSizeHint(tile->sizeHint());
    ui->listInputImageTile->setItemWidget(listItem, tile);
}

void Project::refreshCanvasProfilesList()
{
    ui->listWidgetCanvasProfiles->clear();

    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.canvasProfiles;

    if (project.canvasProfileIds.empty()) {
        auto* placeholder = new QListWidgetItem(tr("(all workspace profiles)"));
        placeholder->setFlags(Qt::ItemIsEnabled);
        ui->listWidgetCanvasProfiles->addItem(placeholder);
        return;
    }

    for (const auto& id : project.canvasProfileIds) {
        for (const auto& cp : wsProfiles) {
            if (cp.id != id) continue;

            auto* item = new QListWidgetItem();
            item->setData(Qt::UserRole, QString::fromStdString(cp.id));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->listWidgetCanvasProfiles->addItem(item);

            auto* row = new QWidget;
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(4, 1, 2, 1);
            layout->setSpacing(4);

            auto* nameLabel = new QLabel(QString::fromStdString(cp.name));
            nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            auto* removeBtn = new QToolButton;
            removeBtn->setText(QStringLiteral("×"));
            removeBtn->setAutoRaise(true);
            removeBtn->setFixedSize(16, 16);
            removeBtn->setToolTip(tr("Remove assignment"));

            layout->addWidget(nameLabel);
            layout->addWidget(removeBtn);

            item->setSizeHint(row->sizeHint());
            ui->listWidgetCanvasProfiles->setItemWidget(item, row);

            const std::string profileId = cp.id;
            connect(removeBtn, &QToolButton::clicked, this, [this, profileId]() {
                auto& ids = m_workspace.projectItems[m_projectIndex].canvasProfileIds;
                ids.erase(std::remove(ids.begin(), ids.end(), profileId), ids.end());
                refreshCanvasProfilesList();
                emit projectModified();
            });
            break;
        }
    }
}

void Project::refreshOutputProfileCombo()
{
    ui->comboBoxOutputProfile->blockSignals(true);
    ui->comboBoxOutputProfile->clear();
    ui->comboBoxOutputProfile->addItem(tr("(workspace default)"), QString{});

    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.outputProfiles;

    int selectedIdx = 0;
    for (int i = 0; i < static_cast<int>(wsProfiles.size()); ++i) {
        const auto& op = wsProfiles[i];
        ui->comboBoxOutputProfile->addItem(
            QString::fromStdString(op.name),
            QString::fromStdString(op.id));
        if (op.id == project.outputProfileId)
            selectedIdx = i + 1;
    }

    ui->comboBoxOutputProfile->setCurrentIndex(selectedIdx);
    ui->comboBoxOutputProfile->blockSignals(false);
}

void Project::onAssignCanvasProfiles()
{
    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.canvasProfiles;

    if (wsProfiles.empty()) {
        QMessageBox::information(this, tr("Canvas Profiles"),
            tr("No canvas profiles in workspace. Create one via the Workspace menu."));
        return;
    }

    QStringList available;
    for (const auto& cp : wsProfiles) {
        const bool linked = std::find(
            project.canvasProfileIds.begin(),
            project.canvasProfileIds.end(),
            cp.id) != project.canvasProfileIds.end();
        if (!linked)
            available << QString::fromStdString(cp.name);
    }

    if (available.isEmpty()) {
        QMessageBox::information(this, tr("Canvas Profiles"),
            tr("All workspace canvas profiles are already assigned to this project."));
        return;
    }

    bool ok;
    const QString chosen = QInputDialog::getItem(
        this, tr("Assign Canvas Profile"),
        tr("Select a profile to assign:"),
        available, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    std::string profileId;
    for (const auto& cp : wsProfiles) {
        if (QString::fromStdString(cp.name) == chosen) {
            profileId = cp.id;
            break;
        }
    }

    if (!m_workspace.projectItems[m_projectIndex].addCanvasProfile(wsProfiles, profileId)) {
        QMessageBox::warning(this, tr("Conflict"),
            tr("Cannot assign \"%1\": a linked profile already has the same canvas dimensions.")
                .arg(chosen));
        return;
    }

    refreshCanvasProfilesList();
    emit projectModified();
}

void Project::onCanvasProfileDoubleClicked(QListWidgetItem* item)
{
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    auto& wsProfiles = m_workspace.canvasProfiles;
    auto it = std::find_if(wsProfiles.begin(), wsProfiles.end(),
        [&](const CanvasProfile& p){ return p.id == id.toStdString(); });
    if (it == wsProfiles.end()) return;

    CanvasProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    const std::string savedId = it->id;
    *it = dlg.profile();
    it->id = savedId;

    refreshCanvasProfilesList();
    emit projectModified();
}


void Project::onOutputProfileChanged(int index)
{
    const QString id = ui->comboBoxOutputProfile->itemData(index).toString();
    m_workspace.projectItems[m_projectIndex].outputProfileId = id.toStdString();
    emit projectModified();
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
    for (int i = 0; i < ui->listInputImageTile->count(); ++i) {
        const QString path = ui->listInputImageTile->item(i)->data(Qt::UserRole).toString();
        for (auto& f : inputs) {
            if (QString::fromStdString(f.filePath) == path) {
                f.order = i;
                break;
            }
        }
    }
    emit projectModified();
}
