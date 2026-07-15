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

void Project::addImageTile(const InputFile& file)
{
    // Create a new QListWidgetItem for the image tile and store the file path in its UserRole data.
    auto* listItem = new QListWidgetItem(ui->listInputImageTile);
    listItem->setData(Qt::UserRole, QString::fromStdString(file.filePath));

    // Create a new ImageTile widget, set its file info, and connect its move up/down signals to the Project's slots.
    auto* tile = new ImageTile(this);
    tile->setFileInfo(QString::fromStdString(file.filePath), file.status, m_cacheDir);
    connect(tile, &ImageTile::moveUpRequested,   this, &Project::onTileMoveUp);
    connect(tile, &ImageTile::moveDownRequested, this, &Project::onTileMoveDown);

    // Set the size hint of the list item to match the tile's size and set the tile as the widget for the list item.
    listItem->setSizeHint(tile->sizeHint());
    ui->listInputImageTile->setItemWidget(listItem, tile);
}

void Project::refreshCanvasProfilesList()
{
    // Clear the current list of canvas profiles in the UI.
    ui->listWidgetCanvasProfiles->clear();

    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.canvasProfiles;

    // If the project has no assigned canvas profiles, display a placeholder item indicating that all workspace profiles are used.
    if (project.canvasProfileIds.empty()) {
        auto* placeholder = new QListWidgetItem(tr("(all workspace profiles)"));
        placeholder->setFlags(Qt::ItemIsEnabled);
        ui->listWidgetCanvasProfiles->addItem(placeholder);
        return;
    }

    // Iterate through the project's assigned canvas profile IDs and find the corresponding profiles in the workspace. For each matched profile, create a list item with a remove button to allow unassigning the profile.
    for (const auto& id : project.canvasProfileIds) {
        for (const auto& cp : wsProfiles) {
            // Match the profile by ID. If it matches, create a list item with a remove button.
            if (cp.id != id) continue;

            // Create a new QListWidgetItem for the canvas profile and store its ID in the UserRole data.
            auto* item = new QListWidgetItem();
            item->setData(Qt::UserRole, QString::fromStdString(cp.id));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->listWidgetCanvasProfiles->addItem(item);
        
            // Create a custom widget for the list item that includes the profile name and a remove button.
            auto* row = new QWidget;
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(4, 1, 2, 1);
            layout->setSpacing(4);

            // Create a label for the profile name and a remove button. The remove button will emit a signal to unassign the profile when clicked.
            auto* nameLabel = new QLabel(QString::fromStdString(cp.name));
            nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            // Create a remove button with a tooltip and connect its clicked signal to a lambda that removes the profile from the project's assigned profiles and refreshes the list.
            auto* removeBtn = new QToolButton;
            removeBtn->setText(QStringLiteral("×"));
            removeBtn->setAutoRaise(true);
            removeBtn->setFixedSize(16, 16);
            removeBtn->setToolTip(tr("Remove assignment"));

            // Add the name label and remove button to the layout of the custom row widget, set the size hint of the list item to match the row's size, and set the row as the widget for the list item.
            layout->addWidget(nameLabel);
            layout->addWidget(removeBtn);

            // Set the size hint of the list item to match the custom row widget's size and set the row as the widget for the list item.
            item->setSizeHint(row->sizeHint());
            ui->listWidgetCanvasProfiles->setItemWidget(item, row);

            // Connect the remove button's clicked signal to a lambda that removes the profile from the project's assigned profiles and refreshes the list. 
            //The lambda captures the profile ID and uses it to find and remove the profile from the project's canvasProfileIds vector.
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
void Project::onAssignCanvasProfiles()
{
    // Assign a canvas profile to the project. Opens a dialog to select from available workspace profiles that are not already assigned to this project.
    const auto& project = m_workspace.projectItems[m_projectIndex];
    const auto& wsProfiles = m_workspace.canvasProfiles;

    // Guard: if there are no workspace profiles, inform the user and return.
    if (wsProfiles.empty()) {
        QMessageBox::information(this, tr("Canvas Profiles"),
            tr("No canvas profiles in workspace. Create one via the Workspace menu."));
        return;
    }

    // Build a list of available profiles that are not already assigned to this project. 
    // If all profiles are already assigned, inform the user and return.
    QStringList available;
    for (const auto& cp : wsProfiles) {
        const bool linked = std::find(
            project.canvasProfileIds.begin(),
            project.canvasProfileIds.end(),
            cp.id) != project.canvasProfileIds.end();
        if (!linked)
            available << QString::fromStdString(cp.name);
    }

    // Guard: if there are no available profiles to assign, inform the user and return.
    if (available.isEmpty()) {
        QMessageBox::information(this, tr("Canvas Profiles"),
            tr("All workspace canvas profiles are already assigned to this project."));
        return;
    }

    // Prompt the user to select a profile from the available list using a QInputDialog. If the user cancels or selects nothing, return.
    bool ok;
    const QString chosen = QInputDialog::getItem(
        this, tr("Assign Canvas Profile"),
        tr("Select a profile to assign:"),
        available, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    // Find the ID of the chosen profile by matching its name in the workspace profiles. 
    // If found, add it to the project's assigned canvasProfileIds. 
    // If a conflict occurs (e.g., a linked profile already has the same canvas dimensions), inform the user and return.
    std::string profileId;
    for (const auto& cp : wsProfiles) {
        if (QString::fromStdString(cp.name) == chosen) {
            profileId = cp.id;
            break;
        }
    }

    // Guard: if the profile ID was not found (should not happen), inform the user and return.
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
    // Edit the selected canvas profile. Opens the CanvasProfileDialog for editing. 
    // If the user accepts, update the profile in the workspace and refresh the list.
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    // Find the profile in the workspace by ID. If not found, return.
    auto& wsProfiles = m_workspace.canvasProfiles;
    auto it = std::find_if(wsProfiles.begin(), wsProfiles.end(),
        [&](const CanvasProfile& p){ return p.id == id.toStdString(); });
    if (it == wsProfiles.end()) return;

    // Open the CanvasProfileDialog for editing the selected profile. If the user accepts, update the profile in the workspace while preserving its ID and templateInfo.
    CanvasProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    // Preserve id + templateInfo (the dialog returns them empty); keeping the
    // fingerprint lets the Templates dialog still detect a now-outdated template.
    const std::string savedId = it->id;
    const auto savedTpl = it->templateInfo;
    *it = dlg.profile();
    it->id           = savedId;
    it->templateInfo = savedTpl;

    refreshCanvasProfilesList();
    emit projectModified();
}

void Project::addInputPaths(const QStringList& newPaths)
{
    // Skip if no new paths are provided.
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

    // Helper lambda to push a path into the union list if it hasn't been seen yet. Normalizes the path to lower-case for case-insensitive comparison.
    const auto pushUnique = [&](const QString& absPath) {
        const QString key = absPath.toLower();
        if (seen.contains(key)) return;
        seen.insert(key);
        unionPaths.push_back(absPath.toStdString());
    };

    // Add existing inputs in order, then new paths. This ensures that existing inputs are preserved and new inputs are appended.
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
    // Prompt the user to select a directory containing input images. If a directory is selected, scan it for image files and add them to the project.
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Input Directory"));
    if (dir.isEmpty()) return;

    // Store the selected directory in the project's inputDirectory field for future reference.
    m_workspace.projectItems[m_projectIndex].inputDirectory = dir.toStdString();

    // Scan the selected directory for image files with specific extensions and add them to the project.
    const QStringList filters = {"*.jpg","*.jpeg","*.png","*.webp","*.tif","*.tiff"};
    const auto entries = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Name);

    // If no image files are found in the selected directory, inform the user and return.
    QStringList paths;
    paths.reserve(entries.size());
    for (const auto& e : entries)
        paths << e.absoluteFilePath();

    // If no image files were found, inform the user and return.
    addInputPaths(paths);
}

void Project::onAddFiles()
{
    // Prompt the user to select one or more input image files. If files are selected, add them to the project.
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Input Files"), {},
        tr("Images (*.jpg *.jpeg *.png *.webp *.tif *.tiff);;All files (*)"));
    addInputPaths(files);
}

void Project::onClearInputs()
{
    // Skip if there are no input images to clear.
    auto& item = m_workspace.projectItems[m_projectIndex];
    if (item.getInputImages().empty()) return;

    // Guard: if the user does not confirm the action, return without clearing inputs.
    if (QMessageBox::question(this, tr("Clear Inputs"),
            tr("Remove all input files from this project?\n\n"
               "Files on disk are not deleted."))
        != QMessageBox::Yes)
        return;

    // clears the list and marks outputs desynchronised
    item.mergeFileScan({}); 
    populate();
    emit projectModified();
}

void Project::onApplySort()
{
    // Sort the input images based on the selected sorting option in the UI. The sorting can be by name, creation date, or modification date.
    const int key = ui->comboBoxSortingOpt->currentData().toInt();

    // Guard: if there are fewer than 2 input images, sorting is not applicable, so return early.
    auto& inputs = m_workspace.projectItems[m_projectIndex].getInputImages();
    if (inputs.size() < 2) return;

    // Create a vector of pointers to the input files for sorting. 
    // This allows us to sort the inputs without modifying the original vector until we finalize the new order.
    std::vector<InputFile*> order;
    order.reserve(inputs.size());
    for (auto& f : inputs)
        order.push_back(&f);

    // Sort the input files based on the selected sorting key. 
    // If sorting by name, use a QCollator for case-insensitive and numeric-aware comparison. 
    // If sorting by creation or modification date, use QFileInfo to retrieve the appropriate timestamps for comparison.
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

    // Update the order field of each input file based on the new sorted order. 
    // This ensures that the input files are displayed in the correct order in the UI.
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        order[static_cast<std::size_t>(i)]->order = i;

    populate();
    emit projectModified();
}

void Project::onGoToOutput()
{
    // Switch to the Output tab in the UI when the "Go to Output" button is clicked. This allows the user to quickly navigate to the output section of the project.
    ui->tabWidget->setCurrentWidget(ui->tabOutput);
}

void Project::onRowsMoved()
{
    // Update the order of input images based on the current order of items in the QListWidget. This is called when the user drags and drops items to reorder them.
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

// Moves the tile with \p filePath one step (\p delta = -1 up, +1 down) by swapping
static void moveByOrder(std::vector<InputFile>& inputs, const QString& filePath, int delta)
{
    // Guard: if there are fewer than 2 input images, moving is not applicable, so return early.
    if (inputs.size() < 2) return;

    // Create a vector of pointers to the input files for sorting. This allows us to sort the inputs without modifying the original vector until we finalize the new order.
    std::vector<InputFile*> ordered;
    ordered.reserve(inputs.size());
    for (auto& f : inputs) ordered.push_back(&f);
    std::sort(ordered.begin(), ordered.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    // Find the index of the input file with the specified filePath in the ordered list. If not found, return early. Then calculate the target index by adding delta to the current index. If the target index is out of bounds, return early. Finally, swap the order values of the current and target input files to move the tile up or down in the order.
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ordered.size()); ++i)
        if (QString::fromStdString(ordered[static_cast<std::size_t>(i)]->filePath) == filePath) {
            idx = i; break;
        }
    const int target = idx + delta;
    if (idx < 0 || target < 0 || target >= static_cast<int>(ordered.size())) return;

    // Swap the order values of the current and target input files to move the tile up or down in the order.
    std::swap(ordered[static_cast<std::size_t>(idx)]->order,
              ordered[static_cast<std::size_t>(target)]->order);
}

void Project::onTileMoveUp(const QString& filePath)
{
    // Move the tile with the specified filePath up in the order by calling moveByOrder with a delta of -1. 
    // After moving, repopulate the UI and emit a projectModified signal to indicate that the project has been modified.
    moveByOrder(m_workspace.projectItems[m_projectIndex].getInputImages(), filePath, -1);
    populate();
    emit projectModified();
}

void Project::onTileMoveDown(const QString& filePath)
{
    // Move the tile with the specified filePath down in the order by calling moveByOrder with a delta of +1.
    // After moving, repopulate the UI and emit a projectModified signal to indicate that the project has been modified.
    moveByOrder(m_workspace.projectItems[m_projectIndex].getInputImages(), filePath, +1);
    populate();
    emit projectModified();
}

void Project::onInputContextMenu(const QPoint& pos)
{
    // Right-clicking a tile outside the current selection targets just that tile.
    if (QListWidgetItem* under = ui->listInputImageTile->itemAt(pos);
        under && !under->isSelected()) {
        ui->listInputImageTile->clearSelection();
        under->setSelected(true);
    }

    // Guard: if there are no selected items, return early.
    const auto selected = ui->listInputImageTile->selectedItems();
    if (selected.isEmpty()) return;

    const int n = static_cast<int>(selected.size());

    // Show a context menu with an option to remove the selected input files from the project. 
    //If the user confirms the action, remove the selected files from the project's input images and repopulate the UI.
    QMenu menu(this);
    QAction* removeAction = menu.addAction(tr("Remove %n file(s) from list", nullptr, n));
    if (menu.exec(ui->listInputImageTile->viewport()->mapToGlobal(pos)) != removeAction)
        return;

    // Guard: if the user does not confirm the action, return without removing inputs.
    if (QMessageBox::question(this, tr("Remove Inputs"),
            tr("Remove %n file(s) from this project?\n\nFiles on disk are not deleted.",
               nullptr, n))
        != QMessageBox::Yes)
        return;

    // Set of paths to drop.
    QSet<QString> drop;
    for (const QListWidgetItem* it : selected)
        drop.insert(it->data(Qt::UserRole).toString());

    // Rebuild the ordered path list without the dropped files, then merge-scan it
    // (handles removal + marks outputs desynchronised).
    auto& item = m_workspace.projectItems[m_projectIndex];
    auto& inputs = item.getInputImages();
    std::vector<const InputFile*> ordered;
    ordered.reserve(inputs.size());
    for (const auto& f : inputs) ordered.push_back(&f);
    std::sort(ordered.begin(), ordered.end(),
              [](const InputFile* a, const InputFile* b){ return a->order < b->order; });

    // Build a list of remaining file paths after removing the selected ones. This will be used to update the project's input images.
    std::vector<std::string> remaining;
    remaining.reserve(ordered.size());
    for (const InputFile* f : ordered)
        if (!drop.contains(QString::fromStdString(f->filePath)))
            remaining.push_back(f->filePath);

    // Merge-scan the remaining paths to update the project's input images. This will remove the selected files and mark outputs as desynchronized.
    item.mergeFileScan(remaining);
    populate();
    emit projectModified();
}
