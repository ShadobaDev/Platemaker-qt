#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "projectsnapshotcommand.h"
#include "canvasprofiledialog.h"
#include "outputformatoptionswidget.h"

#include <platemaker/infrastructure/project_editor/project_editor.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QSpinBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
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
#include <QMimeData>
#include <QSet>
#include <QSettings>
#include <QToolButton>
#include <QKeySequence>
#include <QUndoStack>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Platemaker::Models;

namespace {
// Sort keys stored in comboBoxSortingOpt item data.
enum SortKey { SortByName = 0, SortByCreated = 1, SortByModified = 2 };

// Image extensions Platemaker accepts as inputs — the same set the Add-from-directory scan uses,
// as name filters (for QDir) and as a membership test (for individually dropped files).
const QStringList kImageNameFilters = {"*.jpg","*.jpeg","*.png","*.webp","*.tif","*.tiff"};

bool isSupportedImage(const QFileInfo& fi)
{
    static const QSet<QString> exts = {"jpg","jpeg","png","webp","tif","tiff"};
    return exts.contains(fi.suffix().toLower());
}

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

    // Accept images / folders dropped from the file manager onto the input tile list. The list is in
    // InternalMove mode for reordering; an event filter on its viewport handles external file drags
    // (which carry URLs) and lets internal reorder drags (no URLs) fall through untouched. See
    // eventFilter().
    ui->listInputImageTile->viewport()->installEventFilter(this);

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
// Undo / redo
// ---------------------------------------------------------------------------

void Project::setupUndo()
{
    // Each project owns its stack (depth 10, per the design). MainWindow adds it to a QUndoGroup and
    // makes it active while this dock is visible; the Ctrl+Z / Ctrl+Y actions live on the group, so
    // the shortcut always targets whichever project (or the workspace) is in front.
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(10);
}

void Project::applyProjectSnapshot(const QString& snapshot)
{
    // Restore the whole project (inputs, links, output-profile selection, output dir) from a
    // ProjectEditor snapshot. The project's name is workspace-owned and deliberately preserved by
    // ProjectEditor::restore. Outputs are left as they are — their staleness is recomputed by
    // sanitize() at the next Refresh/render, exactly as for a live reorder.
    auto& item = m_workspace.projectItems[m_projectIndex];
    Platemaker::Infrastructure::ProjectEditor(item).restore(snapshot.toStdString());

    populate();
    emit projectModified();
}

void Project::commitEdit(const QString& text, const std::function<void()>& mutate)
{
    auto& item = m_workspace.projectItems[m_projectIndex];

    const QString before = QString::fromStdString(
        Platemaker::Infrastructure::ProjectEditor(item).snapshot());
    mutate();                                   // the existing operation (does its own populate/emit)
    QString after = QString::fromStdString(
        Platemaker::Infrastructure::ProjectEditor(item).snapshot());

    if (after == before)                        // no effective change (e.g. re-sorting sorted inputs)
        return;                                 // — don't pollute the undo history

    m_undoStack->push(new ProjectSnapshotCommand(this, before, std::move(after), text));
}

void Project::commitWorkspaceEdit(const QString& text, const std::function<void()>& mutate)
{
    // A workspace-level edit triggered from this dock (canvas-profile content edit, output-format
    // edit). Bracket it with the workspace-metadata snapshot and hand both ends to MainWindow, which
    // owns the workspace undo stack and refreshes every open dock on restore.
    const QString before = QString::fromStdString(
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).snapshotMeta());
    mutate();
    const QString after = QString::fromStdString(
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).snapshotMeta());

    if (after == before)
        return;

    emit workspaceEditCommitted(text, before, after);
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

// ---------------------------------------------------------------------------
// Drag & drop of files / folders onto the input list
// ---------------------------------------------------------------------------

bool Project::eventFilter(QObject* watched, QEvent* event)
{
    // Only the input list's viewport is filtered (installed in the constructor). Everything else, and
    // any event type we don't care about, defers to the base implementation.
    if (watched == ui->listInputImageTile->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            // A drag carrying at least one local file/folder is an external add — accept it (Copy) and
            // consume the event. A drag without URLs is the list's own reorder; return false so its
            // InternalMove handling runs unchanged.
            auto* de = static_cast<QDragMoveEvent*>(event);
            const auto* mime = de->mimeData();
            if (mime->hasUrls() &&
                std::any_of(mime->urls().cbegin(), mime->urls().cend(),
                            [](const QUrl& u){ return u.isLocalFile(); })) {
                de->setDropAction(Qt::CopyAction);
                de->accept();
                return true;
            }
            return false;
        }
        case QEvent::Drop: {
            auto* de = static_cast<QDropEvent*>(event);
            if (!de->mimeData()->hasUrls())
                return false;                       // reorder drop — let the list handle it
            de->setDropAction(Qt::CopyAction);
            de->accept();
            addDroppedUrls(de->mimeData()->urls());
            return true;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Project::addDroppedUrls(const QList<QUrl>& urls)
{
    // Turn the dropped URLs into a flat list of image file paths: a dropped folder is scanned the same
    // way Add-from-directory scans (non-recursive, by name, image extensions only); a dropped file is
    // taken only if it is a supported image, so stray non-images are silently ignored.
    QStringList paths;
    QString lastDir;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        const QFileInfo fi(url.toLocalFile());
        if (fi.isDir()) {
            const auto entries = QDir(fi.absoluteFilePath())
                                     .entryInfoList(kImageNameFilters, QDir::Files, QDir::Name);
            for (const auto& e : entries)
                paths << e.absoluteFilePath();
            lastDir = fi.absoluteFilePath();
        } else if (fi.isFile() && isSupportedImage(fi)) {
            paths << fi.absoluteFilePath();
        }
    }

    if (paths.isEmpty()) return;

    // One undo step for the whole drop. If a folder was dropped, remember it as the project's input
    // directory (same as Add-from-directory) so the next scan re-opens there; the assignment is inside
    // the bracket so it is captured in the snapshot.
    auto& item = m_workspace.projectItems[m_projectIndex];
    commitEdit(tr("Add files"), [&]{
        if (!lastDir.isEmpty())
            item.inputDirectory = lastDir.toStdString();
        addInputPaths(paths);
    });
}
