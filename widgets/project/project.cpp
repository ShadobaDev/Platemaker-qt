#include "project.h"
#include "artifactpainter.h"
#include "artifactsvg.h"
#include "ui_project.h"
#include "imagetile.h"
#include "projectsnapshotcommand.h"
#include "canvasprofiledialog.h"
#include "outputformatoptionswidget.h"
#include "stagecard.h"

#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/project_editor/project_editor.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QApplication>   // for the optional DRAGNDROP_DEBUG_ENABLE logging (qApp filter)
#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>         // for the optional DRAGNDROP_DEBUG_ENABLE logging
#include <QSpinBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QVBoxLayout>
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

// Drag-and-drop diagnostics: flip to 1 to re-enable the [DND] logging — an app-wide event filter that
// logs every drag/drop and which widget it lands on (tile vs input-list viewport vs group box vs dock),
// plus whether Project's own handlers fire. Kept behind this flag so it's a one-line switch next time,
// with zero cost (no logging, no qApp filter) when 0. The D&D behaviour itself is independent of this.
#define DRAGNDROP_DEBUG_ENABLE 0

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

// True when a drag carries at least one local file/folder — i.e. an external "add" drag, as opposed to
// the input list's own InternalMove reorder (which carries no file URLs). Shared by the list-viewport
// event filter and the whole-widget drop handlers.
bool hasLocalFileUrls(const QMimeData* mime)
{
    return mime && mime->hasUrls() &&
           std::any_of(mime->urls().cbegin(), mime->urls().cend(),
                       [](const QUrl& u){ return u.isLocalFile(); });
}

// True for the input list's OWN InternalMove reorder drag, identified by the item-model MIME type it
// carries (never file URLs). We accept the *drag phase* of everything else (external drops) by ruling
// out this type — rather than by hasLocalFileUrls() — because a Windows OLE quirk makes an external file
// drag intermittently report hasUrls()==false mid-drag; gating acceptance on that left the drop target
// in a "reject" state whenever the last pre-release event flickered false, so the drop silently failed.
// The actual add still gates on hasLocalFileUrls() at Drop time, which is reliable (data is materialised
// on release).
bool isInternalReorder(const QMimeData* mime)
{
    return mime && mime->hasFormat(QStringLiteral("application/x-qabstractitemmodeldatalist"));
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

    // Accept images / folders dropped from the file manager. Two layers cover the whole panel:
    //  - the input list is in InternalMove mode for reordering, so an event filter on its viewport
    //    handles external file drags there (URLs) while letting internal reorder drags (no URLs) fall
    //    through untouched — see eventFilter();
    //  - the Project widget itself accepts drops so an image dropped ANYWHERE else on the panel (empty
    //    space, labels, buttons, other tabs) still adds it — see dragEnterEvent()/dropEvent().
    // A given drop reaches exactly one of the two (the list viewport if over the list, otherwise the
    // widget), so there is no double-add.
    ui->listInputImageTile->viewport()->installEventFilter(this);
    setAcceptDrops(true);

#if DRAGNDROP_DEBUG_ENABLE
    // Watch drag/drop on EVERY widget so we can see exactly which one a drag lands on and whether our
    // filter/handlers fire. See DRAGNDROP_DEBUG_ENABLE at the top of this file.
    qApp->installEventFilter(this);
#endif

    // Sort options (user-triggered helper; manual drag-reorder still wins).
    ui->comboBoxSortingOpt->addItem(tr("Name"),          SortByName);
    ui->comboBoxSortingOpt->addItem(tr("Date created"),  SortByCreated);
    ui->comboBoxSortingOpt->addItem(tr("Date modified"), SortByModified);
    connect(ui->pushSortyByApply, &QPushButton::clicked,
            this, &Project::onApplySort);

    // Auto-sort rules are not implemented yet. Grey the whole group out (setEnabled(false) disables
    // every child) and flag the fields "Coming soon" so the UI doesn't offer inputs that do nothing.
    // Remove this stopgap when the auto-sort feature lands.
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
    connect(ui->pushButtonViewStrip, &QPushButton::clicked, this, [this]{
        emit viewStripRequested(m_projectIndex);
    });

    // --- Workflow tab: a read-only pipeline map that doubles as a launchpad. It reads top-to-bottom like
    // the strip itself — a vertical stack of stage bars (so every step is visible without scrolling and it
    // fits the narrow panel). The .ui gives only the empty tab + its layout; the header hint and the
    // scrollable, centred column are built here, and the column is (re)filled by refreshWorkflowMap().
    {
        auto* hint = new QLabel(
            tr("The pipeline your pages run through. Click a step to configure it"),
            this);
        hint->setWordWrap(true);
        hint->setEnabled(false); // muted, palette-derived (no hardcoded colour)
        ui->workflowTabLayout->addWidget(hint);

        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // The bars are added centred (Qt::AlignHCenter) at their fixed width, so they read as a neat
        // column and don't stretch absurdly wide on a wide panel — no wrapper widget needed.
        auto* contents = new QWidget(scroll);
        m_workflowStack = new QVBoxLayout(contents);
        m_workflowStack->setContentsMargins(8, 8, 8, 8);
        m_workflowStack->setSpacing(2);
        scroll->setWidget(contents);

        ui->workflowTabLayout->addWidget(scroll, 1); // fills the tab
    }

    // Validate against disk on open so tiles reflect deletions / external edits made
    // while the app was closed (the "changed externally" case), and against the canvas
    // profiles so pages whose profile was edited show as out of sync rather than done.
    Platemaker::Infrastructure::ProjectEditor{m_workspace.projectItems[m_projectIndex]}.sanitize(m_workspace.canvasProfiles());
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

QString Project::fullSnapshot()
{
    // The library's project snapshot plus this GUI's authoring records for the same project. Undo has to
    // move both together: an overlay's record says *which* bitmap renders, its artifact says what that
    // bitmap contains, and restoring one without the other would leave the strip showing text the render
    // does not bake. The lib snapshot is itself JSON, carried here as a string — the library owns its
    // format and this wrapper has no business parsing it.
    auto& item = m_workspace.projectItems[m_projectIndex];
    const QJsonObject j{
        {QStringLiteral("project"),
         QString::fromStdString(Platemaker::Infrastructure::ProjectEditor(item).snapshot())},
        {QStringLiteral("artifacts"), artifactsToJsonObject(m_artifacts)},
    };
    return QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact));
}

void Project::applyProjectSnapshot(const QString& snapshot)
{
    // Restore the whole project (inputs, links, output-profile selection, output dir) from a
    // ProjectEditor snapshot. The project's name is workspace-owned and deliberately preserved by
    // ProjectEditor::restore. Outputs are left as they are — their staleness is recomputed by
    // sanitize() at the next Refresh/render, exactly as for a live reorder.
    auto& item = m_workspace.projectItems[m_projectIndex];

    const QJsonObject j = QJsonDocument::fromJson(snapshot.toUtf8()).object();
    Platemaker::Infrastructure::ProjectEditor(item).restore(
        j.value(QStringLiteral("project")).toString().toStdString());

    m_artifacts = artifactsFromJsonObject(j.value(QStringLiteral("artifacts")).toObject());
    // The records are back; the files on disk still hold what the step being undone wrote.
    rewriteOverlayAssets();
    emit artifactsChanged(m_artifacts);

    populate();
    emit projectModified();
}

void Project::commitEdit(const QString& text, const std::function<void()>& mutate)
{
    const QString before = fullSnapshot();
    mutate();                                   // the existing operation (does its own populate/emit)
    QString after = fullSnapshot();

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
    refreshWorkflowMap();
}

void Project::applyColourCorrection(const ColourCorrection& cc)
{
    commitEdit(tr("Adjust colour correction"), [this, &cc] {
        m_workspace.projectItems[m_projectIndex].colourCorrection = cc;
        emit projectModified();
        populate(); // refresh the workflow map (CC on/off, exclusions) and the rest of the views
    });
}

void Project::refreshWorkflowMap()
{
    if (!m_workflowStack) // built in the ctor; guard in case populate() runs earlier
        return;

    // Clear the previous cards/arrows. deleteLater (not delete): activating an optional step repopulates
    // the map from inside a card's own clicked() handler, so the emitting card may be one of these — and
    // deleting the sender synchronously mid-signal would crash.
    while (QLayoutItem* item = m_workflowStack->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const auto& project = m_workspace.projectItems[m_projectIndex];
    const int  nInputs   = static_cast<int>(project.getInputImages().size());
    const int  nOutputs  = static_cast<int>(project.getOutputImages().size());
    const bool ccOn      = project.colourCorrection.enabled;
    const int  nExcluded = static_cast<int>(project.colourCorrection.excludedInputUids.size());
    const int  nOverlays = static_cast<int>(project.getStripOverlays().size());

    // Stage actions. Fixed stages jump to the tab where they're configured. The optional stages carry
    // their own buttons: "+" activates in place (no navigation), Edit / double-click open the editor,
    // "−" deactivates.
    const auto goInput    = [this]{ ui->tabWidget->setCurrentWidget(ui->tabInput); };
    const auto goOutput   = [this]{ ui->tabWidget->setCurrentWidget(ui->tabOutput); };
    const auto openEditor = [this]{ emit viewStripRequested(m_projectIndex); };
    const auto setCC = [this](bool on) {
        auto& item = m_workspace.projectItems[m_projectIndex];
        if (item.colourCorrection.enabled == on) return;
        commitEdit(on ? tr("Enable colour correction") : tr("Disable colour correction"),
                   [this, &item, on]{ item.colourCorrection.enabled = on; emit projectModified(); populate(); });
    };
    const auto clearOverlays = [this] {
        auto& item = m_workspace.projectItems[m_projectIndex];
        if (item.getStripOverlays().empty()) return;
        commitEdit(tr("Clear text & bubbles"), [this, &item]{
            while (!item.getStripOverlays().empty()) {
                const std::string uid = item.getStripOverlays().front().uid; // copy: removeOverlay erases it
                item.removeOverlay(uid);
            }
            emit projectModified();
            populate();
        });
    };

    auto addArrow = [this]{
        auto* arrow = new QLabel(QStringLiteral("↓")); // downwards arrow (UTF-8 source, as elsewhere)
        arrow->setAlignment(Qt::AlignCenter);
        arrow->setEnabled(false); // muted, palette-derived
        m_workflowStack->addWidget(arrow, 0, Qt::AlignHCenter);
    };
    auto addFixed = [this](const QString& title, const QString& subtitle, const std::function<void()>& jump) {
        auto* c = new StageCard;
        c->setKind(StageCard::Kind::Fixed);
        c->setTitle(title);
        c->setSubtitle(subtitle);
        c->setActions(false, false, false);
        connect(c, &StageCard::clicked, this, jump);
        m_workflowStack->addWidget(c, 0, Qt::AlignHCenter);
    };

    addFixed(tr("Inputs"),      tr("%1 pages").arg(nInputs), goInput);
    addArrow();
    addFixed(tr("Margin crop"), tr("trim scan edges"),       goInput);
    addArrow();

    // Colour correction (optional). Greyed until enabled: "+" or a click on the placeholder activates it
    // in place; once on, Edit / double-click open the editor and "−" turns it off.
    {
        auto* c = new StageCard;
        c->setKind(StageCard::Kind::Optional);
        c->setTitle(tr("Colour correction"));
        c->setActive(ccOn);
        if (ccOn) {
            c->setSubtitle(nExcluded > 0 ? tr("on, %1 excluded").arg(nExcluded) : tr("on"));
            c->setActions(/*add*/false, /*edit*/true, /*remove*/true);
            connect(c, &StageCard::editRequested,   this, openEditor);
            connect(c, &StageCard::removeRequested, this, [setCC]{ setCC(false); });
        } else {
            c->setSubtitle(tr("optional"));
            c->setActions(/*add*/true, /*edit*/false, /*remove*/false);
            connect(c, &StageCard::addRequested, this, [setCC]{ setCC(true); });
            connect(c, &StageCard::clicked,      this, [setCC]{ setCC(true); });
        }
        m_workflowStack->addWidget(c, 0, Qt::AlignHCenter);
    }
    addArrow();

    addFixed(tr("Resize"), tr("fit canvas width"), goOutput);
    addArrow();
    addFixed(tr("Slice"),  tr("cut to slices"),    goOutput);
    addArrow();

    // Text & bubbles (optional). A bubble only exists once authored in the editor, so there is no in-place
    // "+" — Edit / click / double-click open the editor; "−" clears the overlays once there are any.
    {
        const bool tbOn = nOverlays > 0;
        auto* c = new StageCard;
        c->setKind(StageCard::Kind::Optional);
        c->setTitle(tr("Text & bubbles"));
        c->setActive(tbOn);
        c->setSubtitle(tbOn ? tr("%1 artifacts").arg(nOverlays) : tr("optional"));
        c->setActions(/*add*/false, /*edit*/true, /*remove*/tbOn);
        connect(c, &StageCard::editRequested,   this, openEditor);
        connect(c, &StageCard::clicked,         this, openEditor);
        connect(c, &StageCard::removeRequested, this, clearOverlays);
        m_workflowStack->addWidget(c, 0, Qt::AlignHCenter);
    }
    addArrow();

    addFixed(tr("Output"), tr("%1 slices").arg(nOutputs), goOutput);

    m_workflowStack->addStretch(1);
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
#if DRAGNDROP_DEBUG_ENABLE
    // With the app-wide filter installed, this runs for every widget. Log where a drag/drop actually
    // lands (skip DragMove — it fires continuously).
    if (const auto t = event->type(); t == QEvent::DragEnter || t == QEvent::Drop) {
        const auto* de = static_cast<const QDropEvent*>(event); // DragEnter/Drop are QDropEvent-derived
        qDebug().nospace() << "[DND] " << (t == QEvent::DragEnter ? "DragEnter" : "Drop")
            << " -> " << watched->metaObject()->className()
            << " '" << watched->objectName() << "'"
            << " hasUrls=" << hasLocalFileUrls(de->mimeData())
            << (watched == ui->listInputImageTile->viewport() ? "  [input-list viewport]" : "");
    }
#endif

    // Only the input list's viewport is filtered for the ACTION (installed in the constructor).
    // Everything else, and any event type we don't care about, defers to the base implementation.
    if (watched == ui->listInputImageTile->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            // The list's own reorder (item-model MIME) falls through to InternalMove; every other drag is
            // an external drop we accept (Copy) — NOT gated on hasUrls(), which flickers false mid-drag
            // on Windows and would otherwise let the drop be rejected. See isInternalReorder().
            auto* de = static_cast<QDragMoveEvent*>(event);
            if (isInternalReorder(de->mimeData()))
                return false;
            de->setDropAction(Qt::CopyAction);
            de->accept();
            return true;
        }
        case QEvent::Drop: {
            auto* de = static_cast<QDropEvent*>(event);
            if (isInternalReorder(de->mimeData()))
                return false;                       // reorder drop — let the list handle it
            // Read the URLs ONCE: on Windows, mimeData()->urls() re-queries the OLE data object per
            // call and can return different results, so a separate hasUrls() check could disagree with
            // this read. Decide and add from the same snapshot.
            const QList<QUrl> urls = de->mimeData()->urls();
            if (std::any_of(urls.cbegin(), urls.cend(),
                            [](const QUrl& u){ return u.isLocalFile(); })) {
                de->setDropAction(Qt::CopyAction);
                de->accept();
                addDroppedUrls(urls);
                return true;
            }
            return false;                           // external drag with nothing droppable
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// --- Whole-widget drop target (everything on the panel except the input list, which eventFilter
//     already covers). Accept only external file/folder drags; anything else propagates normally. ---

void Project::dragEnterEvent(QDragEnterEvent* event)
{
#if DRAGNDROP_DEBUG_ENABLE
    qDebug() << "[DND] Project::dragEnterEvent  hasUrls=" << hasLocalFileUrls(event->mimeData())
             << " internalReorder=" << isInternalReorder(event->mimeData());
#endif
    // Accept any external drag (Copy) — NOT gated on hasUrls(), which flickers false mid-drag on Windows
    // and would let the drop be rejected. Only the list's own reorder is left to the default handler; the
    // add itself gates on hasLocalFileUrls() at Drop. Force Copy so we never signal the source to delete.
    if (!isInternalReorder(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        QWidget::dragEnterEvent(event);
    }
}

void Project::dragMoveEvent(QDragMoveEvent* event)
{
    // Same as dragEnterEvent: keep accepting the whole drag so a mid-drag hasUrls() flicker can't drop us
    // out of the "accepting" state before release. The add gates on hasLocalFileUrls() at Drop.
    if (!isInternalReorder(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        QWidget::dragMoveEvent(event);
    }
}

void Project::dropEvent(QDropEvent* event)
{
    // Read the URLs ONCE (Windows OLE re-queries mimeData()->urls() per call and can return different
    // results — see the list-viewport Drop case in eventFilter). Decide and add from the same snapshot.
    const QList<QUrl> urls = event->mimeData()->urls();
    const bool hasFiles = std::any_of(urls.cbegin(), urls.cend(),
                                      [](const QUrl& u){ return u.isLocalFile(); });
#if DRAGNDROP_DEBUG_ENABLE
    qDebug() << "[DND] Project::dropEvent  hasFiles=" << hasFiles << " urlCount=" << urls.size();
#endif
    if (hasFiles) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        addDroppedUrls(urls);
    } else {
        QWidget::dropEvent(event);
    }
}

void Project::addDroppedUrls(const QList<QUrl>& urls)
{
    // Turn the dropped URLs into a flat list of image file paths: a dropped folder is scanned the same
    // way Add-from-directory scans (non-recursive, by name, image extensions only); a dropped file is
    // taken only if it is a supported image, so stray non-images are silently ignored.
#if DRAGNDROP_DEBUG_ENABLE
    qDebug() << "[DND] addDroppedUrls: got" << urls.size() << "url(s)";
#endif
    QStringList paths;
    QString lastDir;
    for (const QUrl& url : urls) {
#if DRAGNDROP_DEBUG_ENABLE
        const QFileInfo dbgFi(url.toLocalFile());
        qDebug().nospace() << "[DND]   url=" << url.toString()
            << " local=" << url.isLocalFile() << " path='" << url.toLocalFile() << "'"
            << " exists=" << dbgFi.exists() << " file=" << dbgFi.isFile()
            << " dir=" << dbgFi.isDir() << " img=" << isSupportedImage(dbgFi);
#endif
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

#if DRAGNDROP_DEBUG_ENABLE
    qDebug() << "[DND] addDroppedUrls: collected" << paths.size() << "image path(s):" << paths;
#endif
    if (paths.isEmpty()) return;

    // One undo step for the whole drop. If a folder was dropped, remember it as the project's input
    // directory (same as Add-from-directory) so the next scan re-opens there; the assignment is inside
    // the bracket so it is captured in the snapshot.
    auto& item = m_workspace.projectItems[m_projectIndex];
#if DRAGNDROP_DEBUG_ENABLE
    const auto beforeCount = item.getInputImages().size();
#endif
    commitEdit(tr("Add files"), [&]{
        if (!lastDir.isEmpty())
            item.inputDirectory = lastDir.toStdString();
        addInputPaths(paths);
    });
#if DRAGNDROP_DEBUG_ENABLE
    qDebug() << "[DND] addDroppedUrls: input count" << beforeCount << "->"
             << m_workspace.projectItems[m_projectIndex].getInputImages().size()
             << "(no change = dropped file(s) already in the list -> dedup)";
#endif
}

// ---------------------------------------------------------------------------
// Text & bubble overlays
//
// Two layers meet here: the SVG asset on disk — which is both the artwork and the authoring record —
// and the library's inventory entry. The library owns uid minting, hashing and content dedup, so
// creation always goes through addOverlay(); everything else is a straight write of what the editor
// sends.
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Writes \p a into the workspace's overlays/ directory as an SVG.
 *
 * The file the library composites **is** the authoring record: the artwork every renderer can draw,
 * plus the editor's parameters in a namespace renderers ignore. There is no separate sidecar and no
 * intermediate raster.
 *
 * @param reusePath The file this overlay already owns, overwritten in place. Empty for a new overlay,
 *                  which is then named by content hash — that name is also what makes two identical
 *                  bubbles share one file, the same dedup the library's inventory performs.
 *
 * A bubble keeps **one** file for its lifetime. Naming every revision by its content instead would
 * leave one file per settled edit — a dozen in a single lettering session — and the reason to do that
 * has gone: the undo snapshot carries the full authoring record, so a previous rendering is regenerated
 * from it (see Project::rewriteOverlayAssets()) rather than recovered from a file kept alive for it.
 *
 * @return The asset's absolute path, or empty when it could not be written.
 */
QString writeArtifactSvg(const QString& overlaysDir, const TextArtifact& a,
                         const QString& reusePath = {})
{
    const QByteArray svg = artifactToSvg(a);
    if (svg.isEmpty() || overlaysDir.isEmpty())
        return {};

    QString path = reusePath;
    if (path.isEmpty()) {
        // Hashed from the bytes about to be written, so the name matches what the library will hash
        // when it inventories the file — no scratch file needed, unlike the raster path's encode-then-hash.
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(svg, QCryptographicHash::Sha256).toHex()).left(16);
        path = overlaysDir + QStringLiteral("/ovl-") + sha + QStringLiteral(".svg");
        if (QFile::exists(path))
            return path;               // same content, already stored
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(svg);
    return f.error() == QFile::NoError ? path : QString{};
}

} // namespace

void Project::setArtifacts(ArtifactMap artifacts)
{
    m_artifacts = std::move(artifacts);
}

void Project::createOverlay(const TextArtifact& artifact, int x, int y, const QString& anchorInputUid)
{
    const QString dir = ArtifactStore::ensureOverlaysDir(m_workspacePath);
    if (dir.isEmpty()) {
        QMessageBox::warning(this, tr("Text & bubbles"),
                             tr("Save the workspace first — bubbles are stored next to the workspace "
                                "file, in an 'overlays' folder."));
        return;
    }

    const QString asset = writeArtifactSvg(dir, artifact);
    if (asset.isEmpty()) {
        QMessageBox::warning(this, tr("Text & bubbles"),
                             tr("Could not write the bubble to:\n%1").arg(dir));
        return;
    }

    commitEdit(tr("Add bubble"), [&] {
        auto& item = m_workspace.projectItems[m_projectIndex];
        // The library mints the uid, hashes the file and reuses an existing path for identical content.
        const std::string uid =
            item.addOverlay(asset.toStdString(), x, y,
                            Platemaker::Models::BlendMode::Over, anchorInputUid.toStdString());
        m_artifacts.insert(QString::fromStdString(uid), artifact);
        emit artifactsChanged(m_artifacts);
        emit projectModified();
        populate();   // the workflow map counts overlays
    });
}

void Project::rewriteOverlayAssets()
{
    const QString dir = ArtifactStore::overlaysDir(m_workspacePath);
    if (dir.isEmpty())
        return;

    for (const auto& o : m_workspace.projectItems[m_projectIndex].getStripOverlays()) {
        const auto it = m_artifacts.constFind(QString::fromStdString(o.uid));
        if (it == m_artifacts.constEnd() || o.assetPath.empty())
            continue;   // a flat asset has no record to re-emit from, and must be left exactly as it is
        writeArtifactSvg(dir, it.value(), QString::fromStdString(o.assetPath));
    }
}

void Project::importOverlayArtwork(const QString& sourceFile, int x, int y,
                                   const QString& anchorInputUid)
{
    const QString dir = ArtifactStore::ensureOverlaysDir(m_workspacePath);
    if (dir.isEmpty()) {
        QMessageBox::warning(this, tr("Import artwork"),
                             tr("Save the workspace first — artwork is stored next to the workspace "
                                "file, in an 'overlays' folder."));
        return;
    }

    QFile src(sourceFile);
    if (!src.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import artwork"),
                             tr("Could not read:\n%1").arg(sourceFile));
        return;
    }
    const QByteArray bytes = src.readAll();
    src.close();

    // Ask the renderer that will have to draw it, rather than trusting the extension. An SVG using
    // features it cannot handle, or one whose visible result is empty, is better refused here than
    // placed as an invisible rectangle the author then has to hunt for.
    const QFileInfo fi(sourceFile);
    if (fi.suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0) {
        const auto probe =
            Platemaker::Core::StripOverlayCompositor{}.rasterizeSvgRgba(bytes.toStdString(), 1.0);
        if (!probe.isValid()) {
            QMessageBox::warning(this, tr("Import artwork"),
                                 tr("This SVG could not be rendered, so it would not appear in the "
                                    "output either:\n%1").arg(sourceFile));
            return;
        }
    }

    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).left(16);
    const QString dest = dir + QStringLiteral("/art-") + sha + QLatin1Char('.') + fi.suffix().toLower();

    if (!QFile::exists(dest)) {          // identical artwork imported twice keeps one copy
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) < 0) {
            QMessageBox::warning(this, tr("Import artwork"),
                                 tr("Could not write the artwork to:\n%1").arg(dir));
            return;
        }
    }

    commitEdit(tr("Import artwork"), [&] {
        auto& item = m_workspace.projectItems[m_projectIndex];
        item.addOverlay(dest.toStdString(), x, y,
                        Platemaker::Models::BlendMode::Over, anchorInputUid.toStdString());
        emit projectModified();
        populate();
    });
}

namespace {

/**
 * @brief Rewrites an SVG's rendered size, preserving everything else about the document.
 *
 * Only the root `<svg …>` tag is touched — the substring up to its first `>` — so the artist's markup
 * comes back byte-identical apart from the two attributes that had to change.
 *
 * A document with **no viewBox** gets one synthesised from its current size first. Without that,
 * changing width/height moves the coordinate system rather than scaling the drawing, and the artwork
 * would come back the same size in a bigger canvas.
 *
 * @return The rewritten document, or empty when the root tag cannot be found.
 */
QByteArray svgResized(const QByteArray& svg, QSize size, QSize naturalSize)
{
    const int open = svg.indexOf("<svg");
    if (open < 0)
        return {};
    const int close = svg.indexOf('>', open);
    if (close < 0)
        return {};

    QString root = QString::fromUtf8(svg.mid(open, close - open));

    static const QRegularExpression viewBoxRe(QStringLiteral("\\bviewBox\\s*="));
    if (!viewBoxRe.match(root).hasMatch() && !naturalSize.isEmpty()) {
        root += QStringLiteral(" viewBox=\"0 0 %1 %2\"")
                    .arg(naturalSize.width()).arg(naturalSize.height());
    }

    // Width and height may carry units (400px, 10cm, 100%) — replaced outright rather than parsed,
    // since the viewBox above is what preserves the drawing's proportions.
    const auto setAttr = [&root](const QString& name, int value) {
        const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*([\"'])[^\"']*\\1").arg(name));
        const QString replacement = QStringLiteral("%1=\"%2\"").arg(name).arg(value);
        if (re.match(root).hasMatch())
            root.replace(re, replacement);
        else
            root += QLatin1Char(' ') + replacement;
    };
    setAttr(QStringLiteral("width"),  size.width());
    setAttr(QStringLiteral("height"), size.height());

    QByteArray out = svg;
    out.replace(open, close - open, root.toUtf8());
    return out;
}

} // namespace

void Project::resizeOverlayArtwork(const QString& overlayUid, QSize size)
{
    if (size.width() < 1 || size.height() < 1)
        return;
    if (m_artifacts.contains(overlayUid))
        return;   // a parametric bubble; its size is in its record, not in the file

    auto& item = m_workspace.projectItems[m_projectIndex];
    auto& overlays = item.getStripOverlays();
    const auto it = std::find_if(overlays.begin(), overlays.end(),
                                 [&](const Platemaker::Models::StripOverlay& o) {
                                     return QString::fromStdString(o.uid) == overlayUid;
                                 });
    if (it == overlays.end() || it->assetPath.empty())
        return;

    const QString path = QString::fromStdString(it->assetPath);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray original = f.readAll();
    f.close();

    QByteArray rewritten;
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgRenderer renderer(original);
        rewritten = svgResized(original, size, renderer.isValid() ? renderer.defaultSize() : QSize());
    } else {
        // A raster has no size to rewrite — only pixels to resample, which is lossy. That is the
        // inherent limit of raster artwork, and why an imported SVG is the better thing to hand over.
        QImage img;
        if (img.loadFromData(original)) {
            QBuffer buf(&rewritten);
            buf.open(QIODevice::WriteOnly);
            img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
               .save(&buf, QFileInfo(path).suffix().toUpper().toLatin1().constData());
        }
    }
    if (rewritten.isEmpty() || rewritten == original)
        return;

    // Overwrite in place: an overlay owns one file for its lifetime (see writeArtifactSvg).
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(rewritten) < 0)
        return;
    out.close();

    commitEdit(tr("Resize artwork"), [&] {
        auto& live = m_workspace.projectItems[m_projectIndex].getStripOverlays();
        for (auto& o : live) {
            if (QString::fromStdString(o.uid) != overlayUid)
                continue;
            try {
                // The hash is what the staleness signature watches; without it the render would keep
                // compositing the previous size.
                o.sha256 = Platemaker::Infrastructure::FileMetaData::computeFileSha256(o.assetPath);
            } catch (const std::exception&) {
                o.sha256.clear();
            }
            break;
        }
        emit projectModified();
        populate();
    });
}

void Project::applyOverlays(std::vector<Platemaker::Models::StripOverlay> overlays,
                            ArtifactMap                                  artifacts,
                            const QString&                               undoText)
{
    const QString dir = ArtifactStore::ensureOverlaysDir(m_workspacePath);

    // Re-write only what actually changed. A move or a reorder touches no artwork, so the common
    // gesture writes no files at all; a text or styling edit rewrites exactly one bubble.
    for (auto& o : overlays) {
        const QString uid = QString::fromStdString(o.uid);
        const auto    it  = artifacts.constFind(uid);
        if (it == artifacts.constEnd())
            continue;                                   // flat asset: no parameters, nothing to re-emit
        if (m_artifacts.contains(uid) && m_artifacts.value(uid) == it.value())
            continue;                                   // unchanged content

        // Overwrite the file this overlay owns — unless another overlay shares it, which happens when
        // addOverlay() deduped two identical bubbles onto one path at creation. Writing through that
        // would silently re-letter the other one, so fork to a fresh file instead.
        const QString owned  = QString::fromStdString(o.assetPath);
        const bool    shared = std::count_if(overlays.begin(), overlays.end(),
                                             [&o](const Platemaker::Models::StripOverlay& other) {
                                                 return other.assetPath == o.assetPath;
                                             }) > 1;
        const QString asset = dir.isEmpty()
            ? QString{}
            : writeArtifactSvg(dir, it.value(), shared ? QString{} : owned);
        if (asset.isEmpty())
            continue;                                   // keep the previous asset rather than lose it

        o.assetPath = asset.toStdString();
        try {
            // The hash is what the staleness signature watches: without updating it, a re-written
            // bubble would look changed on screen and render as the old one.
            o.sha256 = Platemaker::Infrastructure::FileMetaData::computeFileSha256(o.assetPath);
        } catch (const std::exception&) {
            o.sha256.clear();
        }
    }

    commitEdit(undoText, [&] {
        auto& item = m_workspace.projectItems[m_projectIndex];
        item.getStripOverlays() = std::move(overlays);
        m_artifacts             = std::move(artifacts);
        emit artifactsChanged(m_artifacts);
        emit projectModified();
        populate();
    });
}
