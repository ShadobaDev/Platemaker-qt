#include "manageoutputprofilesdialog.h"
#include "ui_manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"

#include <platemaker/models/output_profile.hpp>

#include <QListWidgetItem>
#include <QMessageBox>

ManageOutputProfilesDialog::ManageOutputProfilesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ManageOutputProfilesDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    connect(ui->listWidgetProfiles, &QListWidget::itemSelectionChanged,
            this, &ManageOutputProfilesDialog::onSelectionChanged);
    connect(ui->listWidgetProfiles, &QListWidget::itemDoubleClicked,
            this, &ManageOutputProfilesDialog::onEditClicked);

    connect(ui->buttonNew,       &QPushButton::clicked, this, &ManageOutputProfilesDialog::onNewClicked);
    connect(ui->buttonEdit,      &QPushButton::clicked, this, &ManageOutputProfilesDialog::onEditClicked);
    connect(ui->buttonDuplicate, &QPushButton::clicked, this, &ManageOutputProfilesDialog::onDuplicateClicked);
    connect(ui->buttonDelete,    &QPushButton::clicked, this, &ManageOutputProfilesDialog::onDeleteClicked);
    connect(ui->buttonSetActive, &QPushButton::clicked, this, &ManageOutputProfilesDialog::onSetActiveClicked);

    updateButtonStates();
}

ManageOutputProfilesDialog::~ManageOutputProfilesDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ManageOutputProfilesDialog::setProfiles(const QList<Platemaker::Models::OutputProfile> &profiles,
                                              const QString &activeProfileName)
{
    m_profiles          = profiles;
    m_activeProfileName = activeProfileName;
    rebuildList();
}

QList<Platemaker::Models::OutputProfile> ManageOutputProfilesDialog::profiles() const
{
    return m_profiles;
}

QString ManageOutputProfilesDialog::activeProfileName() const
{
    return m_activeProfileName;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void ManageOutputProfilesDialog::onSelectionChanged()
{
    updateButtonStates();
}

void ManageOutputProfilesDialog::onNewClicked()
{
    // Create a new profile via OutputProfileDialog. If the user accepts, add it to the list.
    OutputProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    Platemaker::Models::OutputProfile newProfile = dlg.profile();

    for (const auto &p : std::as_const(m_profiles)) {
        if (QString::fromStdString(p.name) == QString::fromStdString(newProfile.name)) {
            QMessageBox::warning(this, "Duplicate name",
                "A profile with that name already exists. Choose a different name.");
            return;
        }
    }

    m_profiles.append(newProfile);

    if (m_profiles.size() == 1)
        m_activeProfileName = QString::fromStdString(newProfile.name);

    rebuildList();
}

void ManageOutputProfilesDialog::onEditClicked()
{
    // Edit the selected profile via OutputProfileDialog. If the user accepts, update the profile in the list.
    const int row = selectedRow();
    if (row < 0) return;

    // OutputProfileDialog returns a profile without an id — restore the stable id
    OutputProfileDialog dlg(this);
    dlg.setProfile(m_profiles[row]);
    if (dlg.exec() != QDialog::Accepted) return;

    // OutputProfileDialog returns a profile without an id — restore the stable id
    // so projects referencing this profile (by id) survive the edit.
    const QString oldName     = QString::fromStdString(m_profiles[row].name);
    const std::string savedId = m_profiles[row].id;
    m_profiles[row]           = dlg.profile();
    m_profiles[row].id        = savedId;
    const QString newName     = QString::fromStdString(m_profiles[row].name);

    if (m_activeProfileName == oldName)
        m_activeProfileName = newName;

    rebuildList();
}

void ManageOutputProfilesDialog::onDuplicateClicked()
{
    // Duplicate the selected profile, appending " (copy)" to the name. The new profile gets a fresh id.
    const int row = selectedRow();
    if (row < 0) return;

    // OutputProfileDialog returns a profile without an id — restore the stable id
    Platemaker::Models::OutputProfile copy = m_profiles[row];
    copy.name += " (copy)";
    copy.id.clear();   // a duplicate needs its own id (MainWindow assigns a fresh one)
    m_profiles.insert(row + 1, copy);
    rebuildList();
    ui->listWidgetProfiles->setCurrentRow(row + 1);
}

void ManageOutputProfilesDialog::onDeleteClicked()
{
    // Delete the selected profile after confirmation. If it was the active profile, switch to the first one in the list.
    const int row = selectedRow();
    if (row < 0) return;

    const QString name = QString::fromStdString(m_profiles[row].name);

    // Confirm deletion
    if (m_profiles.size() == 1) {
        QMessageBox::information(this, "Cannot delete",
            "At least one output profile must exist.");
        return;
    }

    // Confirm deletion
    const auto answer = QMessageBox::question(this, "Delete profile",
        QString("Delete \"%1\"?").arg(name));
    if (answer != QMessageBox::Yes) return;

    m_profiles.removeAt(row);

    if (m_activeProfileName == name)
        m_activeProfileName = QString::fromStdString(m_profiles.first().name);

    rebuildList();
}

void ManageOutputProfilesDialog::onSetActiveClicked()
{
    // Set the selected profile as the active one. The active profile is marked with a star in the list.
    const int row = selectedRow();
    if (row < 0) return;

    m_activeProfileName = QString::fromStdString(m_profiles[row].name);
    rebuildList();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ManageOutputProfilesDialog::rebuildList()
{
    // Preserve the current selection so we can restore it after rebuilding the list.
    const int previousRow = ui->listWidgetProfiles->currentRow();

    // Rebuild the list widget with the current profiles, marking the active one with a star.
    ui->listWidgetProfiles->clear();
    for (const auto &p : std::as_const(m_profiles)) {
        const QString name     = QString::fromStdString(p.name);
        const bool    isActive = (name == m_activeProfileName);
        // Marked from the id alone, so this dialog never needs to know a specific preset
        // and a new preset in the library shows up here without any change.
        const bool    isPreset = Platemaker::Models::isOutputProfilePresetId(p.id);

        QString label = isActive ? "★  " + name : "     " + name;
        if (isPreset) label += tr("   (preset)");

        auto *item = new QListWidgetItem(label);
        if (isActive) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setForeground(QColor("#7ac8f5"));
        }
        if (isPreset) {
            item->setToolTip(tr("Built-in preset — shared by every workspace, so it is "
                                "read-only. Use Duplicate to make your own version."));
        }
        ui->listWidgetProfiles->addItem(item);
    }

    // Restore the previous selection if possible, otherwise select the first item.
    const int newRow = qBound(0, previousRow, ui->listWidgetProfiles->count() - 1);
    if (ui->listWidgetProfiles->count() > 0)
        ui->listWidgetProfiles->setCurrentRow(newRow);

    ui->labelActiveInfo->setText(
        m_activeProfileName.isEmpty()
            ? "★ Active profile: —"
            : "★ Active profile: " + m_activeProfileName);

    updateButtonStates();
}

void ManageOutputProfilesDialog::updateButtonStates()
{
    // Enable/disable buttons based on the current selection and profile list.
    const bool hasSelection = (selectedRow() >= 0);
    const bool isActive     = hasSelection &&
        (QString::fromStdString(m_profiles[selectedRow()].name) == m_activeProfileName);

    // A preset identifier is the same in every workspace, which is what keeps a preset
    // recognisable across files and app updates — but only while it cannot be made to mean
    // something else. Hence read-only: no Edit, no Delete.
    //
    // Duplicate and Set active stay available. Duplicating is the intended route to a
    // customised version and already clears the id, so the copy is an ordinary profile.
    const bool isPreset = hasSelection &&
        Platemaker::Models::isOutputProfilePresetId(m_profiles[selectedRow()].id);

    ui->buttonEdit->setEnabled(hasSelection && !isPreset);
    ui->buttonDuplicate->setEnabled(hasSelection);
    ui->buttonDelete->setEnabled(hasSelection && !isPreset && m_profiles.size() > 1);
    ui->buttonSetActive->setEnabled(hasSelection && !isActive);

    const QString presetHint =
        tr("Presets are read-only — use Duplicate to make your own version.");
    ui->buttonEdit->setToolTip(isPreset ? presetHint : QString{});
    ui->buttonDelete->setToolTip(isPreset ? presetHint : QString{});
}

int ManageOutputProfilesDialog::selectedRow() const
{
    return ui->listWidgetProfiles->currentRow();
}
