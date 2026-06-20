#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "canvasprofiledialog.h"

#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QToolButton>

#include <algorithm>

using namespace Platemaker::Models;

namespace {
// Sort keys stored in comboBoxSortingOpt item data.
enum SortKey { SortByName = 0, SortByCreated = 1, SortByModified = 2 };

} // namespace

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
    connect(ui->pushButtonAddInputs, &QPushButton::clicked,
            this, &Project::onAddFiles);
    connect(ui->pushButtonInputClear, &QPushButton::clicked,
            this, &Project::onClearInputs);
    connect(ui->listInputImageTile->model(), &QAbstractItemModel::rowsMoved,
            this, &Project::onRowsMoved);

    // Sort options (user-triggered helper; manual drag-reorder still wins).
    ui->comboBoxSortingOpt->addItem(tr("Name"),          SortByName);
    ui->comboBoxSortingOpt->addItem(tr("Date created"),  SortByCreated);
    ui->comboBoxSortingOpt->addItem(tr("Date modified"), SortByModified);
    connect(ui->pushSortyByApply, &QPushButton::clicked,
            this, &Project::onApplySort);

    connect(ui->pushButtonGoToOutput, &QPushButton::clicked,
            this, &Project::onGoToOutput);

    connect(ui->pushButtonAssignCanvasProfiles, &QPushButton::clicked,
            this, &Project::onAssignCanvasProfiles);
    connect(ui->listWidgetCanvasProfiles, &QListWidget::itemDoubleClicked,
            this, &Project::onCanvasProfileDoubleClicked);

    connect(ui->comboBoxOutputProfile, &QComboBox::currentIndexChanged,
            this, &Project::onOutputProfileChanged);

    // --- Output tab ---
    connect(ui->pushButtonODSelect, &QPushButton::clicked,
            this, &Project::onSelectOutputDir);
    connect(ui->pushButtonODClear, &QPushButton::clicked,
            this, &Project::onClearOutputDir);

    // Image format: invalid placeholder + the three formats. Selecting one reveals
    // its options group; the group contents are not yet bound to the model.
    ui->comboBoxImageFormatSelection->addItem(tr("Select image format"), QVariant{});
    ui->comboBoxImageFormatSelection->addItem(tr("PNG"),  static_cast<int>(OutputFormat::PNG));
    ui->comboBoxImageFormatSelection->addItem(tr("JPEG"), static_cast<int>(OutputFormat::JPEG));
    ui->comboBoxImageFormatSelection->addItem(tr("WebP"), static_cast<int>(OutputFormat::WebP));
    connect(ui->comboBoxImageFormatSelection, &QComboBox::currentIndexChanged,
            this, &Project::onImageFormatChanged);
    onImageFormatChanged(); // start with all format groups hidden

    connect(ui->pushButtonJumpToInput, &QPushButton::clicked,
            this, &Project::onJumpToInput);
    connect(ui->pushButtonRender, &QPushButton::clicked,
            this, &Project::onRender);

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
    refreshOutputDirectoryDisplay();
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
    ui->comboBoxOutputProfile->addItem(tr("Choose output profile"), QString{});

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

void Project::refreshOutputDirectoryDisplay()
{
    ui->textOutputDirectory->setPlainText(QString::fromStdString(
        m_workspace.projectItems[m_projectIndex].getOutputDirectory()));
}

void Project::onSelectOutputDir()
{
    auto& item = m_workspace.projectItems[m_projectIndex];

    QString start = QString::fromStdString(item.getOutputDirectory());
    if (start.isEmpty())
        start = QSettings().value(QStringLiteral("lastOutputDir")).toString();

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"), start);
    if (dir.isEmpty()) return;

    item.getOutputDirectory() = dir.toStdString();
    QSettings().setValue(QStringLiteral("lastOutputDir"), dir);

    refreshOutputDirectoryDisplay();
    emit projectModified();
}

void Project::onClearOutputDir()
{
    m_workspace.projectItems[m_projectIndex].getOutputDirectory().clear();
    refreshOutputDirectoryDisplay();
    emit projectModified();
}

void Project::onImageFormatChanged()
{
    const QVariant data = ui->comboBoxImageFormatSelection->currentData();
    const bool valid = data.isValid();
    ui->groupBoxPNG->setVisible(valid && data.toInt() == static_cast<int>(OutputFormat::PNG));
    ui->groupBoxJPG->setVisible(valid && data.toInt() == static_cast<int>(OutputFormat::JPEG));
    ui->groupBoxWebP->setVisible(valid && data.toInt() == static_cast<int>(OutputFormat::WebP));
}

void Project::onJumpToInput()
{
    ui->tabWidget->setCurrentWidget(ui->tabInput);
}

void Project::onRender()
{
    if (ui->comboBoxOutputProfile->currentData().toString().isEmpty()) {
        QMessageBox::warning(this, tr("Render"),
            tr("Output profile is not selected."));
        return;
    }
    if (m_workspace.projectItems[m_projectIndex].getOutputDirectory().empty()) {
        QMessageBox::warning(this, tr("Render"),
            tr("Output directory is not selected."));
        return;
    }
    QMessageBox::information(this, tr("Render"),
        tr("The rendering pipeline will be implemented in Stage 4."));
}

void Project::addInputPaths(const QStringList& newPaths)
{
    if (newPaths.isEmpty()) return;

    auto& item = m_workspace.projectItems[m_projectIndex];

    // Existing inputs in current display order.
    auto& inputs = item.getInputImages();
    std::vector<const InputFile*> ordered;
    ordered.reserve(inputs.size());
    for (const auto& f : inputs)
        ordered.push_back(&f);
    std::sort(ordered.begin(), ordered.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    // Dedup set (Windows paths are case-insensitive — normalise to lower-case).
    QSet<QString> seen;
    std::vector<std::string> unionPaths;
    unionPaths.reserve(inputs.size() + static_cast<std::size_t>(newPaths.size()));

    const auto pushUnique = [&](const QString& absPath) {
        const QString key = absPath.toLower();
        if (seen.contains(key)) return;
        seen.insert(key);
        unionPaths.push_back(absPath.toStdString());
    };

    for (const InputFile* f : ordered)
        pushUnique(QString::fromStdString(f->filePath));
    for (const QString& p : newPaths)
        pushUnique(QFileInfo(p).absoluteFilePath());

    // Additive merge: existing kept (status/hash preserved), new appended Pending.
    item.mergeFileScan(unionPaths);
    populate();
    emit projectModified();
}

void Project::onAddFromDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Input Directory"));
    if (dir.isEmpty()) return;

    m_workspace.projectItems[m_projectIndex].inputDirectory = dir.toStdString();

    const QStringList filters = {"*.jpg","*.jpeg","*.png","*.webp","*.tif","*.tiff"};
    const auto entries = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Name);

    QStringList paths;
    paths.reserve(entries.size());
    for (const auto& e : entries)
        paths << e.absoluteFilePath();

    addInputPaths(paths);
}

void Project::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Input Files"), {},
        tr("Images (*.jpg *.jpeg *.png *.webp *.tif *.tiff);;All files (*)"));
    addInputPaths(files);
}

void Project::onClearInputs()
{
    auto& item = m_workspace.projectItems[m_projectIndex];
    if (item.getInputImages().empty()) return;

    if (QMessageBox::question(this, tr("Clear Inputs"),
            tr("Remove all input files from this project?\n\n"
               "Files on disk are not deleted."))
        != QMessageBox::Yes)
        return;

    item.mergeFileScan({}); // clears the list and marks outputs desynchronised
    populate();
    emit projectModified();
}

void Project::onApplySort()
{
    const int key = ui->comboBoxSortingOpt->currentData().toInt();

    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();
    if (inputs.size() < 2) return;

    std::vector<InputFile*> order;
    order.reserve(inputs.size());
    for (auto& f : inputs)
        order.push_back(&f);

    if (key == SortByName) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(order.begin(), order.end(),
            [&](const InputFile* a, const InputFile* b) {
                return collator.compare(
                    QFileInfo(QString::fromStdString(a->filePath)).fileName(),
                    QFileInfo(QString::fromStdString(b->filePath)).fileName()) < 0;
            });
    } else {
        // Date created (birthTime, fallback to lastModified) or date modified.
        const auto keyTime = [key](const InputFile* f) {
            const QFileInfo fi(QString::fromStdString(f->filePath));
            if (key == SortByModified)
                return fi.lastModified();
            const QDateTime born = fi.birthTime();
            return born.isValid() ? born : fi.lastModified();
        };
        std::stable_sort(order.begin(), order.end(),
            [&](const InputFile* a, const InputFile* b) {
                return keyTime(a) < keyTime(b);
            });
    }

    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        order[static_cast<std::size_t>(i)]->order = i;

    populate();
    emit projectModified();
}

void Project::onGoToOutput()
{
    ui->tabWidget->setCurrentWidget(ui->tabOutput);
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
