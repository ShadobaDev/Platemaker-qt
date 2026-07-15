#include "project.h"
#include "ui_project.h"
#include "imagetile.h"
#include "canvasprofiledialog.h"
#include "outputformatoptionswidget.h"

#include <QCheckBox>
#include <QCollator>
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

    // Populate the output profile combo box with the available workspace output profiles. 
    // The first item is a placeholder for "Choose output profile", and subsequent items are added based on the workspace's output profiles. 
    // The currently selected profile is determined by matching the project's outputProfileId with the workspace profiles.
    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.outputProfiles;

    // Guard: if there are no workspace profiles, add a placeholder item and return early.
    if (wsProfiles.empty()) {
        ui->comboBoxOutputProfile->addItem(tr("No output profiles in workspace"), QString{});
        ui->comboBoxOutputProfile->setCurrentIndex(0);
        ui->comboBoxOutputProfile->blockSignals(false);
        return;
    }

    // Populate the combo box with the workspace output profiles, marking the currently selected profile if it exists.
    int selectedIdx = 0;
    for (int i = 0; i < static_cast<int>(wsProfiles.size()); ++i) {
        const auto& op = wsProfiles[i];
        ui->comboBoxOutputProfile->addItem(
            QString::fromStdString(op.name),
            QString::fromStdString(op.id));
        if (op.id == project.outputProfileId)
            selectedIdx = i + 1;
    }

    // Set the current index of the combo box to the selected profile index and unblock signals.
    ui->comboBoxOutputProfile->setCurrentIndex(selectedIdx);
    ui->comboBoxOutputProfile->blockSignals(false);
}

void Project::onOutputProfileChanged(int index)
{
    // When the output profile selection changes, update the project's output profile ID and refresh the format controls.
    const QString id = ui->comboBoxOutputProfile->itemData(index).toString();
    m_workspace.projectItems[m_projectIndex].outputProfileId = id.toStdString();
    refreshFormatControls();   // reflect the newly-selected profile's format/options
    emit projectModified();
}

Platemaker::Models::OutputProfile* Project::selectedOutputProfile() const
{
    // Return a pointer to the currently selected output profile based on the project's outputProfileId.
    // If no profile is selected or the ID does not match any workspace profiles, return nullptr.
    const std::string& id = m_workspace.projectItems[m_projectIndex].outputProfileId;
    if (id.empty()) return nullptr;
    for (auto& op : m_workspace.outputProfiles)
        if (op.id == id) return &op;
    return nullptr;
}

void Project::refreshFormatControls()
{
    OutputProfile* op = selectedOutputProfile();
    // Without a selected profile there is nothing to edit.
    m_formatOptions->setEnabled(op != nullptr);
    if (op)
        m_formatOptions->setFromProfile(*op);
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
    // The format-options widget changed — write back into the selected profile.
    if (OutputProfile* op = selectedOutputProfile()) {
        m_formatOptions->applyToProfile(*op);
        emit projectModified();
    }
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

void Project::addOutputTile(const QString& filePath)
{
    // Add a new output image tile to the output list in the UI. This is called during a live render to append newly generated output images.
    auto* listItem = new QListWidgetItem(ui->listOutputImageTile);
    listItem->setData(Qt::UserRole, filePath);

    // Create a new ImageTile widget for the output image and set its file information, including the file path, status, and cache directory.
    auto* tile = new ImageTile(this);
    tile->setFileInfo(filePath, FileStatus::Done, m_cacheDir);

    // Set the size hint of the list item to match the tile's size and set the tile as the widget for the list item in the output list.
    listItem->setSizeHint(tile->sizeHint());
    ui->listOutputImageTile->setItemWidget(listItem, tile);
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
    ui->listOutputImageTile->clear();
    const auto& outputs = m_workspace.projectItems[m_projectIndex].getOutputImages();
    for (const auto& f : outputs)
        addOutputImageTile(f);
}

bool Project::outputsConfigStale() const
{
    // Determine if the existing outputs are stale compared to the current output configuration (format/size/quality).
    // This is used to indicate whether a re-render is required due to changes in the output configuration. Returns true if the outputs are stale, false otherwise.
    const auto& project = m_workspace.projectItems[m_projectIndex];
    if (project.getOutputImages().empty()) return false;

    // Check if the selected output profile exists. If so, the outputs are considered stale.
    OutputProfile* op = selectedOutputProfile();
    if (!op) return false;

    // Signature mismatch covers format / target width / slice height / quality once
    // a signature has been stored by a render.
    const std::string curSig = outputProfileSignature(*op);
    if (!project.outputSignature.empty() && project.outputSignature != curSig)
        return true;

    // Format change is detectable even without a stored signature (outputs rendered
    // before signatures existed): the recorded slice extension won't match.
    const std::string wantExt = outputFormatExtension(op->outputFormat);
    const std::string& firstName = project.getOutputImages().front().fileName;
    const auto dot = firstName.find_last_of('.');
    const std::string haveExt =
        (dot == std::string::npos) ? std::string{} : firstName.substr(dot);
    return !haveExt.empty() && haveExt != wantExt;
}

void Project::onRefreshFiles()
{
    // Re-scan inputs + outputs against disk and repaint tiles. Statuses are
    // transient (recomputed on demand), so this does not mark the workspace
    // dirty; an actual render does.
    auto& project = m_workspace.projectItems[m_projectIndex];
    project.sanitize();

    // A config change (e.g. PNG→JPEG) is invisible on disk — the old files still
    // exist and hash-match — so flag the still-"Done" outputs as out-of-sync so the
    // user sees that a re-render is required.
    if (outputsConfigStale()) {
        for (auto& of : project.getOutputImages())
            if (of.status == FileStatus::Done)
                of.status = FileStatus::Desynchronized;
    }

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

