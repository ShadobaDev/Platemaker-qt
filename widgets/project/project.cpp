#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "inputlistcommand.h"
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
#include <QKeySequence>
#include <QUndoStack>

#include <algorithm>
#include <utility>

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

    //--- Input tab ---
    // Input files selection
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

    // Auto-sort rules are not implemented yet. Grey the whole group out (setEnabled(false) disables
    // every child) and flag the fields "Coming soon" so the UI doesn't offer inputs that do nothing.
    // Remove this stopgap when the auto-sort feature lands (tracked in the GUI TODO).
    ui->groupBoxAutosort->setEnabled(false);
    ui->groupBoxAutosort->setTitle(tr("Auto-sort rules (coming soon):"));
    const QString comingSoon = tr("Coming soon");
    ui->lineEditInputNameRegex->setPlaceholderText(comingSoon);
    ui->lineEditPrependedRegex->setPlaceholderText(comingSoon);
    ui->lineEditAppendedRegex->setPlaceholderText(comingSoon);

    // Undo / redo for the input-list edits wired above (add / remove / clear / reorder / sort).
    setupUndo();

    // Go to output tab
    connect(ui->pushButtonGoToOutput, &QPushButton::clicked,
            this, &Project::onGoToOutput);

    // Output profile selection
    connect(ui->pushButtonAssignCanvasProfiles, &QPushButton::clicked,
            this, &Project::onAssignCanvasProfiles);
    connect(ui->listWidgetCanvasProfiles, &QListWidget::itemDoubleClicked,
            this, &Project::onCanvasProfileDoubleClicked);
    connect(ui->comboBoxOutputProfile, &QComboBox::currentIndexChanged,
            this, &Project::onOutputProfileChanged);

    // --- Output tab ---
    // Output directory selection
    connect(ui->pushButtonODSelect, &QPushButton::clicked,
            this, &Project::onSelectOutputDir);
    connect(ui->pushButtonODClear, &QPushButton::clicked,
            this, &Project::onClearOutputDir);
    connect(ui->pushButtonODOpen, &QPushButton::clicked,
            this, &Project::onOpenOutputDir);

    // Image format + options: shared widget editing the project's SELECTED output
    // profile (also editable via Manage Output Profiles). Embedded into the existing
    // "Image options" group container.
    m_formatOptions = new OutputFormatOptionsWidget(this);
    ui->verticalLayout_6->addWidget(m_formatOptions);
    connect(m_formatOptions, &OutputFormatOptionsWidget::edited,
            this, &Project::onFormatOptionsEdited);

    // Render / Stop button
    connect(ui->pushButtonJumpToInput, &QPushButton::clicked,
            this, &Project::onJumpToInput);
    connect(ui->pushButtonRefresh, &QPushButton::clicked,
            this, &Project::onRefreshFiles);
    connect(ui->pushButtonRender, &QPushButton::clicked, this, [this]{
        emit renderToggleRequested(m_projectIndex);
    });

    // Validate against disk on open so tiles reflect deletions / external edits made
    // while the app was closed (the "changed externally" case), and against the canvas
    // profiles so pages whose profile was edited show as out of sync rather than done.
    m_workspace.projectItems[m_projectIndex].sanitize(m_workspace.canvasProfiles());
    populate();
}

Project::~Project()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Undo / redo of input-list edits
// ---------------------------------------------------------------------------

void Project::setupUndo()
{
    m_undoStack = new QUndoStack(this);

    // createUndo/RedoAction give QActions whose enabled state and text ("Undo Reorder inputs") track
    // the stack automatically. They carry no shortcut of their own, so we set the platform-standard
    // ones (Ctrl+Z / Ctrl+Y, plus Ctrl+Shift+Z for redo on Windows). Scope them to this widget so the
    // shortcut acts on the project that has focus — each open project dock has its own history.
    QAction* undoAction = m_undoStack->createUndoAction(this, tr("Undo"));
    QAction* redoAction = m_undoStack->createRedoAction(this, tr("Redo"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);
    undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undoAction);
    addAction(redoAction);
}

Project::InputSnapshot Project::captureInputSnapshot() const
{
    const auto& item = m_workspace.projectItems[m_projectIndex];

    InputSnapshot snap;
    snap.inputs          = item.getInputImages();   // copy (InputFile is a plain value type)
    snap.inputDirectory  = item.inputDirectory;

    // Cheap identity for no-op detection: the fields the input-list ops actually change (uid, order,
    // path), in vector order, plus the scanned directory. Two snapshots with equal signatures are
    // treated as "nothing changed", so a sort that was already sorted (etc.) records no undo step.
    QString sig;
    for (const auto& f : snap.inputs) {
        sig += QString::fromStdString(f.uid);
        sig += '|'; sig += QString::number(f.order);
        sig += '|'; sig += QString::fromStdString(f.filePath);
        sig += '\n';
    }
    sig += QString::fromStdString(snap.inputDirectory);
    snap.signature = std::move(sig);
    return snap;
}

void Project::restoreInputSnapshot(const InputSnapshot& snap)
{
    auto& item = m_workspace.projectItems[m_projectIndex];

    // Replace the input vector wholesale and rebuild the path→output / sha→path lookup tables so they
    // match. Outputs are left as they are: their staleness is derived and recomputed by sanitize() at
    // the next Refresh/render (the input-composition baseline notices a reorder/add/remove), exactly as
    // for a live reorder — undo never touches rendered output files.
    item.getInputImages() = snap.inputs;
    item.inputDirectory   = snap.inputDirectory;
    item.rebuildLookupTables();

    populate();
    emit projectModified();
}

void Project::commitInputChange(const QString& text, const std::function<void()>& mutate)
{
    InputSnapshot before = captureInputSnapshot();
    mutate();                                   // the existing operation (does its own populate/emit)
    InputSnapshot after = captureInputSnapshot();

    if (after.signature == before.signature)    // no effective change (e.g. re-sorting sorted inputs)
        return;                                 // — don't pollute the undo history

    m_undoStack->push(new InputListCommand(this, std::move(before), std::move(after), text));
}

void Project::populate()
{
    // Populate the UI with the current state of the project, including input files, canvas profiles, output profile selection, format controls, output directory display, and output tiles.
    ui->listInputImageTile->clear();

    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();

    // Sort the input files by their order field to ensure they are displayed in the correct order in the UI.
    std::vector<const InputFile*> sorted;
    sorted.reserve(inputs.size());
    for (const auto& f : inputs)
        sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    // Add each input file to the UI as an ImageTile widget, which displays the file's information and status.
    for (const InputFile* f : sorted)
        addImageTile(*f);

    // Refresh the canvas profiles list, output profile combo box, format controls, output directory display, and output tiles to reflect the current state of the project.
    refreshCanvasProfilesList();
    refreshOutputProfileCombo();
    refreshFormatControls();
    refreshOutputDirectoryDisplay();
    refreshOutputTiles();
}

void Project::refreshProfileViews()
{
    // Rebuild only the views derived from the workspace's profile palettes, after a workspace-level
    // profile edit (Manage/New/Edit, done in MainWindow). Deliberately a subset of populate(): it
    // touches neither the input/output tiles (no thumbnail churn) nor sanitize() (so it can't disturb
    // the live render statuses or re-scan inputs), which is exactly what a palette change needs.
    refreshCanvasProfilesList();
    refreshOutputProfileCombo();
    refreshFormatControls();
}
