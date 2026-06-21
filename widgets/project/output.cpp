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

void Project::onOutputProfileChanged(int index)
{
    const QString id = ui->comboBoxOutputProfile->itemData(index).toString();
    m_workspace.projectItems[m_projectIndex].outputProfileId = id.toStdString();
    refreshFormatControls();   // reflect the newly-selected profile's format/options
    emit projectModified();
}

Platemaker::Models::OutputProfile* Project::selectedOutputProfile() const
{
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
    auto* listItem = new QListWidgetItem(ui->listOutputImageTile);
    listItem->setData(Qt::UserRole, filePath);

    auto* tile = new ImageTile(this);
    tile->setFileInfo(filePath, FileStatus::Done, m_cacheDir);

    listItem->setSizeHint(tile->sizeHint());
    ui->listOutputImageTile->setItemWidget(listItem, tile);
}

void Project::addOutputImageTile(const OutputFile& file)
{
    const QString dir = QString::fromStdString(
        m_workspace.projectItems[m_projectIndex].getOutputDirectory());
    const QString path = QDir(dir).filePath(QString::fromStdString(file.fileName));

    auto* listItem = new QListWidgetItem(ui->listOutputImageTile);
    listItem->setData(Qt::UserRole, path);

    auto* tile = new ImageTile(this);
    tile->setFileInfo(path, file.status, m_cacheDir);

    listItem->setSizeHint(tile->sizeHint());
    ui->listOutputImageTile->setItemWidget(listItem, tile);
}

void Project::refreshOutputTiles()
{
    ui->listOutputImageTile->clear();
    const auto& outputs = m_workspace.projectItems[m_projectIndex].getOutputImages();
    for (const auto& f : outputs)
        addOutputImageTile(f);
}

void Project::onRefreshFiles()
{
    // Re-scan inputs + outputs against disk and repaint tiles. Statuses are
    // transient (recomputed on demand), so this does not mark the workspace
    // dirty; an actual render does.
    m_workspace.projectItems[m_projectIndex].sanitize();
    populate();
}

