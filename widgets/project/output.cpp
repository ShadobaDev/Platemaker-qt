#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "canvasprofiledialog.h"
#include "outputformatoptionswidget.h"

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
    // When the output profile selection changes, update the project's output profile ID and refresh the format controls.
    const QString id = ui->comboBoxOutputProfile->itemData(index).toString();
    auto& proj = m_workspace.projectItems[m_projectIndex];
    // The combo lists user profiles and presets, so the id always resolves; the editor validates
    // it anyway (an empty id means "workspace default").
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
        .setProjectOutputProfile(proj, id.toStdString());
    refreshFormatControls();   // reflect the newly-selected profile's format/options
    emit projectModified();
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

    // Update the project's output directory and store the selected directory in settings for future reference.
    item.getOutputDirectory() = dir.toStdString();
    QSettings().setValue(QStringLiteral("lastOutputDir"), dir);

    // Refresh the output directory display in the UI and emit a projectModified signal to indicate that the project has been modified.
    refreshOutputDirectoryDisplay();
    emit projectModified();
}

void Project::onClearOutputDir()
{
    // Clear the project's output directory and refresh the display. Emit a projectModified signal to indicate that the project has been modified.
    m_workspace.projectItems[m_projectIndex].getOutputDirectory().clear();
    refreshOutputDirectoryDisplay();
    emit projectModified();
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
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(std::move(profiles));
    emit projectModified();
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
    tile->setFileInfo(path, file.status, m_cacheDir);

    // Set the size hint of the list item to match the tile's size and set the tile as the widget for the list item in the output list.
    listItem->setSizeHint(tile->sizeHint());
    ui->listOutputImageTile->setItemWidget(listItem, tile);
}

void Project::refreshOutputTiles()
{
    // Rebuild the output image tiles in the UI based on the current output images in the project.
    // This is used to refresh the output list after changes to the project's output images.
    auto& project = m_workspace.projectItems[m_projectIndex];

    // The output profile is the config axis sanitize() does not cover: a change there (e.g.
    // PNG→JPEG, slice height, quality) is invisible on disk — the old files still exist and
    // hash-match — so flag still-"Done" outputs as out-of-sync here, on *every* repaint, not only
    // after an explicit Refresh. This is what keeps render-start (startRender → populate) from
    // flashing the stale outputs green before the confirm dialog. Safe because a successful render
    // updates project.outputSignature *before* it repopulates (see MainWindow::onRenderFinished),
    // so freshly-rendered outputs are no longer stale and stay Done.
    if (outputsConfigStale())
        for (auto& of : project.getOutputImages())
            if (of.status == FileStatus::Done)
                of.status = FileStatus::Desynchronized;

    ui->listOutputImageTile->clear();
    const auto& outputs = project.getOutputImages();
    for (const auto& f : outputs)
        addOutputImageTile(f);
}

bool Project::outputsConfigStale() const
{
    // Determine if the existing outputs are stale compared to the current output configuration (format/size/quality).
    // This is used to indicate whether a re-render is required due to changes in the output configuration. Returns true if the outputs are stale, false otherwise.
    const auto& project = m_workspace.projectItems[m_projectIndex];
    if (project.getOutputImages().empty()) return false;

    // Resolve the project's profile (user or preset); if it resolves to nothing, nothing to compare.
    const auto resolved = resolveOutputProfile(m_workspace, project.outputProfileId());
    if (!resolved) return false;

    // Signature mismatch covers format / target width / slice height / quality once
    // a signature has been stored by a render.
    const std::string curSig = outputProfileSignature(*resolved);
    if (!project.outputSignature.empty() && project.outputSignature != curSig)
        return true;

    // Format change is detectable even without a stored signature (outputs rendered
    // before signatures existed): the recorded slice extension won't match.
    const std::string wantExt = outputFormatExtension(resolved->outputFormat);
    const std::string& firstName = project.getOutputImages().front().fileName;
    const auto dot = firstName.find_last_of('.');
    const std::string haveExt =
        (dot == std::string::npos) ? std::string{} : firstName.substr(dot);
    return !haveExt.empty() && haveExt != wantExt;
}

void Project::onRefreshFiles()
{
    // "Refresh files" re-checks the OUTPUT files against disk (and re-applies the output-profile
    // staleness overlay via populate()). It must NOT re-derive input statuses: those are owned by
    // the last render (Processed / Skipped), and sanitize() recomputes inputs from disk — which has
    // no notion of "skipped", so it would silently revert a skipped page to Processed/Pending.
    // sanitize() is the shared routine that also refreshes outputs + config staleness, so we run it
    // but preserve and restore the input statuses around it, leaving inputs untouched by a refresh.
    auto& project = m_workspace.projectItems[m_projectIndex];

    auto& inputs = project.getInputImages();
    std::vector<FileStatus> savedInputStatus;
    savedInputStatus.reserve(inputs.size());
    for (const auto& inf : inputs)
        savedInputStatus.push_back(inf.status);

    // Also flags outputs whose canvas profile changed since their render.
    project.sanitize(m_workspace.canvasProfiles());

    for (std::size_t i = 0; i < inputs.size(); ++i)
        inputs[i].status = savedInputStatus[i];

    // The output-profile staleness overlay (Done → Desynchronized) is applied by
    // refreshOutputTiles(), reached via populate(), so it no longer needs repeating here — and
    // that shared path is what makes render-start reflect it too.
    populate();
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

