#include "manageoutputprofilesdialog.h"
#include "ui_manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"

#include <platemaker/models/output_profile.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>

#include <QListWidgetItem>
#include <QMessageBox>

#include <vector>

namespace {
// Mints an "op-" id that collides with nothing already in the dialog's list. Profiles get a
// stable id inside the dialog (not only when MainWindow commits) so the active profile can be
// tracked by id — names repeat once presets sit beside a user copy of the same name.
QString mintOutputProfileId(const QList<Platemaker::Models::OutputProfile> &profiles)
{
    const std::vector<Platemaker::Models::OutputProfile> taken(profiles.begin(), profiles.end());
    return QString::fromStdString(Platemaker::Infrastructure::makeUniqueOutputProfileId(taken));
}
} // namespace

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
                                              const QString &activeProfileId)
{
    m_profiles        = profiles;
    m_activeProfileId = activeProfileId;
    rebuildList();
}

QList<Platemaker::Models::OutputProfile> ManageOutputProfilesDialog::profiles() const
{
    return m_profiles;
}

QString ManageOutputProfilesDialog::activeProfileId() const
{
    return m_activeProfileId;
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

    // Give it a stable id now, so it is trackable by id (as the active profile, and on write-back).
    newProfile.id = mintOutputProfileId(m_profiles).toStdString();
    m_profiles.append(newProfile);

    if (m_profiles.size() == 1)
        m_activeProfileId = QString::fromStdString(newProfile.id);

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
    // so projects referencing this profile (by id) survive the edit. The id is unchanged,
    // so the active profile (tracked by id) needs no update even if the name changed.
    const std::string savedId = m_profiles[row].id;
    m_profiles[row]           = dlg.profile();
    m_profiles[row].id        = savedId;

    rebuildList();
}

void ManageOutputProfilesDialog::onDuplicateClicked()
{
    // Duplicate & Edit: seed a copy of the selected profile (name suffixed " (copy)", fresh id) and
    // open the editor on it straight away — duplicating is the sanctioned route to customising a
    // preset, so the user lands in the editor rather than having to find and Edit the copy afterwards.
    const int row = selectedRow();
    if (row < 0) return;

    // A duplicate is an ordinary user profile with its own fresh id — the copy is no longer a preset,
    // so it is editable. The id is minted before the editor so it is stable across the round trip.
    Platemaker::Models::OutputProfile copy = m_profiles[row];
    copy.name += " (copy)";
    copy.id = mintOutputProfileId(m_profiles).toStdString();

    OutputProfileDialog dlg(this);
    dlg.setProfile(copy);
    // Atomic: cancelling the editor abandons the whole action — nothing is inserted, so a stray
    // Duplicate never leaves an unwanted "… (copy)" behind. Only an accepted edit adds the profile.
    if (dlg.exec() != QDialog::Accepted) return;

    // OutputProfileDialog returns a profile without an id — restore the freshly-minted one so the
    // copy is trackable (as the active profile, and on write-back).
    const std::string copyId = copy.id;
    copy    = dlg.profile();
    copy.id = copyId;

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
    const QString id   = QString::fromStdString(m_profiles[row].id);

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

    if (m_activeProfileId == id)
        m_activeProfileId = m_profiles.isEmpty()
            ? QString{} : QString::fromStdString(m_profiles.first().id);

    rebuildList();
}

void ManageOutputProfilesDialog::onSetActiveClicked()
{
    // Set the selected profile as the active one. The active profile is marked with a star in the list.
    const int row = selectedRow();
    if (row < 0) return;

    m_activeProfileId = QString::fromStdString(m_profiles[row].id);
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
    // The active profile is matched by id, so a "Webtoon Standard" of the user's own and the
    // preset of the same name are told apart — only the genuinely active row gets the star.
    ui->listWidgetProfiles->clear();
    for (const auto &p : std::as_const(m_profiles)) {
        const QString name     = QString::fromStdString(p.name);
        const bool    isActive = (QString::fromStdString(p.id) == m_activeProfileId);
        // Marked from the id alone, so this dialog never needs to know a specific preset
        // and a new preset in the library shows up here without any change. Provenance is the
        // membership test on the catalogue — no "is a preset" field, no reserved id prefix.
        const bool    isPreset = Platemaker::Models::outputPresetDefById(p.id) != nullptr;

        QString label = isActive ? "★  " + name : "     " + name;
        if (isPreset) label += tr("   (preset)");

        // Two independent visual channels, so they combine cleanly: the *active* profile is a
        // star + bold; a *preset* is preset-blue (matching the Output tab's combo). An active
        // preset is therefore bold, starred and blue.
        auto *item = new QListWidgetItem(label);
        if (isActive) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        if (isPreset) {
            item->setForeground(QColor("#7ac8f5"));
            item->setToolTip(tr("Built-in preset — shared by every workspace, so it is "
                                "read-only. Use Duplicate to make your own version."));
        }
        ui->listWidgetProfiles->addItem(item);
    }

    // Restore the previous selection if possible, otherwise select the first item.
    const int newRow = qBound(0, previousRow, ui->listWidgetProfiles->count() - 1);
    if (ui->listWidgetProfiles->count() > 0)
        ui->listWidgetProfiles->setCurrentRow(newRow);

    // Resolve the active id back to a display name for the info label.
    QString activeName;
    for (const auto &p : std::as_const(m_profiles))
        if (QString::fromStdString(p.id) == m_activeProfileId) {
            activeName = QString::fromStdString(p.name);
            break;
        }
    ui->labelActiveInfo->setText(
        activeName.isEmpty()
            ? "★ Active profile: —"
            : "★ Active profile: " + activeName);

    updateButtonStates();
}

void ManageOutputProfilesDialog::updateButtonStates()
{
    // Enable/disable buttons based on the current selection and profile list.
    const bool hasSelection = (selectedRow() >= 0);
    const bool isActive     = hasSelection &&
        (QString::fromStdString(m_profiles[selectedRow()].id) == m_activeProfileId);

    // A preset identifier is the same in every workspace, which is what keeps a preset
    // recognisable across files and app updates — but only while it cannot be made to mean
    // something else. Hence read-only: no Edit, no Delete.
    //
    // Duplicate and Set active stay available. Duplicating is the intended route to a
    // customised version and already clears the id, so the copy is an ordinary profile.
    const bool isPreset = hasSelection &&
        Platemaker::Models::outputPresetDefById(m_profiles[selectedRow()].id) != nullptr;

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
