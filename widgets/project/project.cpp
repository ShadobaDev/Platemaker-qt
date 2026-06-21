#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "canvasprofiledialog.h"
#include "outputformatoptionswidget.h"

#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QAction>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
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

    // Multi-select (Ctrl/Shift) for multi-drag + multi-delete; right-click menu.
    ui->listInputImageTile->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->listInputImageTile->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listInputImageTile, &QWidget::customContextMenuRequested,
            this, &Project::onInputContextMenu);

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

    // Image format + options: shared widget editing the project's SELECTED output
    // profile (also editable via Manage Output Profiles). Embedded into the existing
    // "Image options" group container.
    m_formatOptions = new OutputFormatOptionsWidget(this);
    ui->verticalLayout_6->addWidget(m_formatOptions);
    connect(m_formatOptions, &OutputFormatOptionsWidget::edited,
            this, &Project::onFormatOptionsEdited);

    connect(ui->pushButtonJumpToInput, &QPushButton::clicked,
            this, &Project::onJumpToInput);
    connect(ui->pushButtonRender, &QPushButton::clicked, this, [this]{
        emit renderToggleRequested(m_projectIndex);
    });

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
    refreshFormatControls();
    refreshOutputDirectoryDisplay();
    refreshOutputTiles();
}
