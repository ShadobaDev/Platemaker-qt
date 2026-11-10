#include "project.hpp"
#include "ui_project.h"
#include "imagetile.hpp"
#include "canvasprofiledialog.hpp"
#include "outputformatoptionswidget.hpp"

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QCheckBox>
#include <QCollator>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
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
#include <platemaker/infrastructure/project_editor/project_editor.hpp>

using namespace Platemaker::Models;

namespace {
// Sort keys stored in comboBoxSortingOpt item data.
enum SortKey { SortByName = 0, SortByCreated = 1, SortByModified = 2 };

} // namespace

void Project::refreshOutputProfileCombo()
{
    // Block signals to prevent triggering onOutputProfileChanged while updating the combo box.
    ui->comboBoxOutputProfile->blockSignals(true);
    ui->comboBoxOutputProfile->clear();
    ui->comboBoxOutputProfile->addItem(tr("Choose output profile"), QString{});

    // The selectable profiles are the user's own plus the code-defined presets. A preset is a
    // first-class render option — it differs only in being read-only and marked "(preset)" (in a
    // preset-blue colour). Presets carry a stable id, so a project can reference one exactly like
    // its own profile; selection is by id (the display label carries the marker, the id does not).
    const auto& project = m_workspace.projectItems[m_projectIndex];

    int selectedIdx = 0;
    int row = 0;
    const auto addProfile = [&](const OutputProfile& op, bool isPreset) {
        ++row;
        QString label = QString::fromStdString(op.name);
        if (isPreset) label += tr("   (preset)");
        ui->comboBoxOutputProfile->addItem(label, QString::fromStdString(op.id));
        if (isPreset)
            ui->comboBoxOutputProfile->setItemData(row, QColor("#7ac8f5"), Qt::ForegroundRole);
        if (op.id == project.outputProfileId())
            selectedIdx = row;
    };

    for (const auto& op : m_workspace.outputProfiles()) addProfile(op, false);
    for (const auto& op : outputProfilePresets())        addProfile(op, true);

    // Set the current index of the combo box to the selected profile index and unblock signals.
    ui->comboBoxOutputProfile->setCurrentIndex(selectedIdx);
    ui->comboBoxOutputProfile->blockSignals(false);
}

void Project::onOutputProfileChanged(int index)
{
    // Fires only on a genuine user change — refreshOutputProfileCombo() blocks signals while it
    // repopulates, so this never runs during populate()/restore. Selecting a profile is project-scope.
    const QString id = ui->comboBoxOutputProfile->itemData(index).toString();
    commitEdit(tr("Change output profile"), [&]{
        auto& proj = m_workspace.projectItems[m_projectIndex];
        // The combo lists user profiles and presets, so the id always resolves; the editor validates
        // it anyway (an empty id means "workspace default").
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
            .setProjectOutputProfile(proj, id.toStdString());
        refreshFormatControls();   // reflect the newly-selected profile's format/options
        emit projectModified();
    }, EditScope::OutputProfile);
}

void Project::refreshFormatControls()
{
    // Resolve against user profiles ∪ presets so a preset-selected project shows its format too.
    const std::string& id = m_workspace.projectItems[m_projectIndex].outputProfileId();
    const auto resolved = resolveOutputProfile(m_workspace, id);

    // A preset's options are shown but not editable: it is read-only, and its meaning must be
    // identical in every workspace. Inline editing is offered only for the user's own profiles.
    const bool isPreset = outputPresetDefById(id) != nullptr;
    m_formatOptions->setEnabled(resolved.has_value() && !isPreset);
    if (resolved)
        m_formatOptions->setFromProfile(*resolved);
}

void Project::refreshOutputDirectoryDisplay()
{
    // Update the output directory display in the UI to reflect the current output directory of the project.
    ui->textOutputDirectory->setPlainText(QString::fromStdString(
        m_workspace.projectItems[m_projectIndex].getOutputDirectory()));
}

void Project::onSelectOutputDir()
{
    auto& item = m_workspace.projectItems[m_projectIndex];

    // Start the directory selection dialog in the current output directory, or fall back to the last used output directory from settings.
    QString start = QString::fromStdString(item.getOutputDirectory());
    if (start.isEmpty())
        start = QSettings().value(QStringLiteral("lastOutputDir")).toString();

    // Open a QFileDialog to allow the user to select an output directory. If the user cancels or selects nothing, return early.
    // If the user selects a directory, update the project's output directory and refresh the display.
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"), start);
    if (dir.isEmpty()) return;

    commitEdit(tr("Set output directory"), [&]{
        // Update the project's output directory and store it in settings (a UI convenience, not undone).
        item.getOutputDirectory() = dir.toStdString();
        QSettings().setValue(QStringLiteral("lastOutputDir"), dir);
        refreshOutputDirectoryDisplay();
        emit projectModified();
    }, EditScope::OutputDirectory);
}

void Project::onClearOutputDir()
{
    commitEdit(tr("Clear output directory"), [&]{
        m_workspace.projectItems[m_projectIndex].getOutputDirectory().clear();
        refreshOutputDirectoryDisplay();
        emit projectModified();
    }, EditScope::OutputDirectory);
}

void Project::onFormatOptionsEdited()
{
    // The format-options widget changed — write back into the selected user profile through the
    // editor (the palette is private). Presets are read-only and never edited here.
    const std::string& id = m_workspace.projectItems[m_projectIndex].outputProfileId();
    if (id.empty() || Platemaker::Models::outputPresetDefById(id) != nullptr)
        return;

    auto profiles = std::vector<OutputProfile>(
        m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());
    const auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const OutputProfile& p){ return p.id == id; });
    if (it == profiles.end()) return;

    m_formatOptions->applyToProfile(*it);
    // Editing an output profile's format changes a *workspace* palette (shared by every project that
    // selected it), so it goes on the workspace undo timeline, not this project's.
    commitWorkspaceEdit(tr("Edit output profile"), [&]{
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(std::move(profiles));
        emit projectModified();
    });
}

void Project::onJumpToInput()
{
    ui->tabWidget->setCurrentWidget(ui->tabInput);
}

void Project::setRendering(bool rendering)
{
    // Update the UI to reflect the rendering state. Change the text and style of the Render/Stop button based on whether rendering is in progress.
    ui->pushButtonRender->setText(rendering ? tr("Stop") : tr("Render"));
    ui->pushButtonRender->setStyleSheet(rendering
        ? QStringLiteral("background-color:#b41414; color:#ffffff; font-weight:bold;")
        : QString{});

    // Lock output configuration while a render runs.
    ui->groupBoxOutputProfile->setEnabled(!rendering);
    ui->groupBoxOutputDirectory->setEnabled(!rendering);
    ui->groupOutputImageOptions->setEnabled(!rendering);
    ui->pushButtonJumpToInput->setEnabled(!rendering);
}

void Project::setOutputTile(int index, const QString& name, const QString& fullPath)
{
    Q_UNUSED(name);
    // Called for each slice as the render writes it, with its 0-based output row. This is
    // **positional**: the tile at row `index` is replaced in place, so a re-render that changes the
    // format (output_001.jpg → output_001.png) or the slice count replaces the old (amber,
    // out-of-sync) tile instead of appending a mismatched-name one beside it — which used to leave
    // stale tiles on screen until the post-render populate() rebuilt the list. Works the same for a
    // partial re-render (each dirty slice hits its own existing row) and a full one (rows filled in
    // order). Trailing rows a shorter render no longer produces are cleaned by the finish populate().
    QListWidget* list = ui->listOutputImageTile;

    // The pipeline delivers slices in increasing index; extend the list if this row is new.
    while (list->count() <= index)
        new QListWidgetItem(list);

    QListWidgetItem* item = list->item(index);
    item->setData(Qt::UserRole, fullPath);

    auto* tile = new ImageTile(this);
    tile->setMoveControlsVisible(false);   // output order is fixed — no reorder arrows
    tile->setFileInfo(fullPath, FileStatus::Done, m_cacheDir);   // replaces any existing widget at this row
    item->setSizeHint(tile->sizeHint());
    list->setItemWidget(item, tile);
}

void Project::addOutputImageTile(const OutputFile& file)
{
    // Add an output image tile to the output list in the UI based on the provided OutputFile information. This is used to populate the output list with existing output images.
    const QString dir = QString::fromStdString(
        m_workspace.projectItems[m_projectIndex].getOutputDirectory());
    const QString path = QDir(dir).filePath(QString::fromStdString(file.fileName));

    // Create a new QListWidgetItem for the output image tile and store the file path in its UserRole data.
    auto* listItem = new QListWidgetItem(ui->listOutputImageTile);
    listItem->setData(Qt::UserRole, path);

    // Create a new ImageTile widget for the output image, set its file information, and add it to the output list in the UI.
    auto* tile = new ImageTile(this);
    tile->setMoveControlsVisible(false);   // output order is fixed — no reorder arrows
    tile->setFileInfo(path, file.status, m_cacheDir);

    // Set the size hint of the list item to match the tile's size and set the tile as the widget for the list item in the output list.
    listItem->setSizeHint(tile->sizeHint());
    ui->listOutputImageTile->setItemWidget(listItem, tile);
}

void Project::refreshOutputTiles()
{
    // Rebuild the output image tiles in the UI based on the current output images in the project.
    // This is used to refresh the output list after changes to the project's output images.
    const auto& project = m_workspace.projectItems[m_projectIndex];

    ui->listOutputImageTile->clear();
    const auto& outputs = project.getOutputImages();
    for (const auto& f : outputs)
        addOutputImageTile(f);
}

void Project::refreshTileStatuses()
{
    const auto& project = m_workspace.projectItems[m_projectIndex];

    // The output profile as the render would resolve it. One that resolves to nothing has nothing to
    // be compared with, so that axis is left out rather than judged against a default.
    const auto resolved = resolveOutputProfile(m_workspace, project.outputProfileId());
    auto report = project.detectFileStaleness(m_workspace.canvasProfiles(),
                                              resolved ? *resolved : OutputProfile{});
    if (!resolved)
        for (auto& out : report.outputs)
            out.outputProfile = false;

    // A disk fact (Missing, Modified, Pending…) is more specific than "the settings moved on", so only
    // a file the disk calls clean is shown out of sync.
    const auto shown = [](FileStatus status, FileStatus clean, const FileStaleness& why) {
        return (status == clean && why.any()) ? FileStatus::Desynchronized : status;
    };
    const auto reason = [](const FileStaleness& why) {
        QStringList lines;
        if (why.canvasProfile)   lines << tr("The canvas profile this page uses changed since the render.");
        if (why.composition)     lines << tr("Pages were added, removed or reordered since the render.");
        if (why.unrenderedPage)  lines << tr("A page in the strip has not been rendered yet.");
        if (why.outputProfile)   lines << tr("The output profile changed since the render.");
        if (why.processingSteps) lines << tr("The colour correction or the overlays changed since the render.");
        return lines.join(QLatin1Char('\n'));
    };

    // Input tiles are keyed by path (list order is the strip order, not the model's).
    const auto& inputs = project.getInputImages();
    QListWidget* inList = ui->listInputImageTile;
    for (int row = 0; row < inList->count(); ++row) {
        QListWidgetItem* item = inList->item(row);
        auto* tile = qobject_cast<ImageTile*>(inList->itemWidget(item));
        const std::string path = item->data(Qt::UserRole).toString().toStdString();
        for (std::size_t i = 0; tile && i < inputs.size(); ++i) {
            if (inputs[i].filePath != path)
                continue;
            const bool noProfile = inputs[i].status == FileStatus::Processed && inputs[i].canvasProfileId.empty();
            tile->setStatus(shown(inputs[i].status, FileStatus::Processed, report.inputs[i]), noProfile);
            tile->setToolTip(reason(report.inputs[i]));
            break;
        }
    }

    // Output tiles are built in getOutputImages() order, one row each (refreshOutputTiles()).
    const auto& outputs = project.getOutputImages();
    QListWidget* outList = ui->listOutputImageTile;
    for (int row = 0; row < outList->count() && row < static_cast<int>(outputs.size()); ++row) {
        if (auto* tile = qobject_cast<ImageTile*>(outList->itemWidget(outList->item(row)))) {
            const auto o = static_cast<std::size_t>(row);
            tile->setStatus(shown(outputs[o].status, FileStatus::Done, report.outputs[o]));
            tile->setToolTip(reason(report.outputs[o]));
        }
    }
}

void Project::onRefreshFiles()
{
    // "Refresh files" re-checks the OUTPUT files against disk. Input statuses are the last render's
    // (Processed / Skipped), so the inputs are not looked at; their tiles keep what they show.
    checkFilesOnDisk(/*outputsOnly*/ true);
}

void Project::onOpenOutputDir()
{
    // Open the output directory in the system file explorer. If no output directory is set or if the directory does not exist, inform the user with a message box.
    const QString dir = QString::fromStdString(
        m_workspace.projectItems[m_projectIndex].getOutputDirectory());
    if (dir.isEmpty()) {
        QMessageBox::information(this, tr("Open output directory"),
                                 tr("No output directory is set for this project."));
        return;
    }

    // Guard: if the output directory does not exist, inform the user with a warning message box and return early.
    if (!QFileInfo::exists(dir)) {
        QMessageBox::warning(this, tr("Open output directory"),
                             tr("The output directory does not exist:\n%1").arg(dir));
        return;
    }

    // Open the output directory using QDesktopServices, which will launch the system's default file explorer to display the contents of the specified directory.
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

