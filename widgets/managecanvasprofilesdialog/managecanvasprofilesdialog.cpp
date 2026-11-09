#include "managecanvasprofilesdialog.hpp"
#include "ui_managecanvasprofilesdialog.h"
#include "canvasprofiledialog.hpp"
#include "badgeitemdelegate.hpp"
#include "profiledistinction.hpp"
#include "dockattention.hpp"

#include <QListWidgetItem>
#include <QMessageBox>

ManageCanvasProfilesDialog::ManageCanvasProfilesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ManageCanvasProfilesDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    // Profiles that share a name carry chips: what sets each apart, or that it is a duplicate.
    ui->listWidgetProfiles->setItemDelegate(new BadgeItemDelegate(ui->listWidgetProfiles));

    connect(ui->listWidgetProfiles,      &QListWidget::itemSelectionChanged,
            this, &ManageCanvasProfilesDialog::onSelectionChanged);
    connect(ui->listWidgetProfiles,      &QListWidget::itemDoubleClicked,
            this, &ManageCanvasProfilesDialog::onEditClicked);

    connect(ui->buttonNew,               &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onNewClicked);
    connect(ui->buttonEdit,              &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onEditClicked);
    connect(ui->buttonDuplicate,         &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onDuplicateClicked);
    connect(ui->buttonDelete,            &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onDeleteClicked);
    connect(ui->buttonSetActive,         &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onSetActiveClicked);
    connect(ui->buttonGenerateTemplates, &QPushButton::clicked, this, &ManageCanvasProfilesDialog::onGenerateTemplatesClicked);

    // Generation acts on the selected row, not the active profile (set in code to
    // avoid editing the .ui).
    ui->buttonGenerateTemplates->setText(tr("Generate template from selected…"));

    updateButtonStates();
}

ManageCanvasProfilesDialog::~ManageCanvasProfilesDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ManageCanvasProfilesDialog::setProfiles(const QList<Platemaker::Models::CanvasProfile> &profiles,
                                              const QString &activeProfileName)
{
    m_profiles           = profiles;
    m_activeProfileName  = activeProfileName;
    rebuildList();
}

void ManageCanvasProfilesDialog::setChangeCheck(ChangeCheck check)
{
    m_changeCheck = std::move(check);
}

void ManageCanvasProfilesDialog::setCommit(Commit commit)
{
    m_commit = std::move(commit);
}

void ManageCanvasProfilesDialog::showProfile(const QString &id)
{
    for (int row = 0; row < m_profiles.size(); ++row)
        if (QString::fromStdString(m_profiles[row].id) == id) {
            showRowAttention(ui->listWidgetProfiles, row);
            return;
        }
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void ManageCanvasProfilesDialog::onSelectionChanged()
{
    updateButtonStates();
}

void ManageCanvasProfilesDialog::onNewClicked()
{
    // A name may repeat — the list tells such profiles apart — and an identical one is warned about.
    // Its Save asks what the new profile puts out of sync, in the editor, so Cancel goes back to it.
    const auto palette = profilePalette();
    CanvasProfileDialog dlg(this);
    dlg.setExistingProfiles(palette);
    dlg.setSaveCheck([this, &dlg, &palette](const Platemaker::Models::CanvasProfile& cp) {
        return !m_changeCheck || m_changeCheck(&dlg, withProfile(palette, cp), PaletteChange::Save);
    });
    if (dlg.exec() != QDialog::Accepted) return;

    const Platemaker::Models::CanvasProfile newProfile = dlg.profile();
    if (m_profiles.isEmpty())   // the first profile is the active one
        setActive(QString::fromStdString(newProfile.name));
    m_commit(withProfile(palette, newProfile), tr("New canvas profile"));
    ui->listWidgetProfiles->setCurrentRow(ui->listWidgetProfiles->count() - 1);   // appended
}

void ManageCanvasProfilesDialog::onEditClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    const auto palette = profilePalette();
    CanvasProfileDialog dlg(this);
    dlg.setProfile(m_profiles[row]);
    dlg.setExistingProfiles(palette);
    dlg.setSaveCheck([this, &dlg, &palette](const Platemaker::Models::CanvasProfile& cp) {
        return !m_changeCheck || m_changeCheck(&dlg, withProfile(palette, cp), PaletteChange::Save);
    });
    if (dlg.exec() != QDialog::Accepted) return;

    // CanvasProfileDialog hands back the profile it was given with its fields applied, so the id
    // (project links) and templates survive the edit. If template-affecting fields changed, the
    // fingerprint check flags the template Outdated.
    const Platemaker::Models::CanvasProfile edited = dlg.profile();
    if (m_activeProfileName == QString::fromStdString(m_profiles[row].name))   // a rename of the active one
        setActive(QString::fromStdString(edited.name));
    m_commit(withProfile(palette, edited), tr("Edit canvas profile"));
    ui->listWidgetProfiles->setCurrentRow(row);
}

void ManageCanvasProfilesDialog::onDuplicateClicked()
{
    // Duplicate & Edit: seed a copy of the selected profile (name + " (copy)") and open the editor on
    // it straight away, mirroring the output-profile dialog so both behave the same.
    const int row = selectedRow();
    if (row < 0) return;

    Platemaker::Models::CanvasProfile copy = m_profiles[row];
    copy.name += " (copy)";
    copy.id.clear();             // the library mints its id when it is written
    copy.templates.clear();      // a duplicate has no template of its own yet

    auto palette = profilePalette();
    CanvasProfileDialog dlg(this);
    dlg.setProfile(copy);
    dlg.setExistingProfiles(palette);
    dlg.setSaveCheck([this, &dlg, &palette](const Platemaker::Models::CanvasProfile& cp) {
        return !m_changeCheck || m_changeCheck(&dlg, withProfile(palette, cp), PaletteChange::Save);
    });
    // Cancelling the editor abandons the whole action: only a saved copy is added.
    if (dlg.exec() != QDialog::Accepted) return;

    palette.insert(palette.begin() + row + 1, dlg.profile());
    m_commit(std::move(palette), tr("Duplicate canvas profile"));
    ui->listWidgetProfiles->setCurrentRow(row + 1);
}

void ManageCanvasProfilesDialog::onDeleteClicked()
{
    // Delete the selected profile after confirmation. If it was the active profile, switch to the first one in the list.
    const int row = selectedRow();
    if (row < 0) return;

    const QString name = QString::fromStdString(m_profiles[row].name);

    if (m_profiles.size() == 1) {
        QMessageBox::information(this, tr("Cannot delete"),
            tr("At least one canvas profile must exist."));
        return;
    }

    const auto answer = QMessageBox::question(this, tr("Delete profile"),
        tr("Delete \"%1\"?").arg(name));
    if (answer != QMessageBox::Yes) return;

    auto palette = profilePalette();
    palette.erase(palette.begin() + row);
    if (m_changeCheck && !m_changeCheck(this, palette, PaletteChange::Delete))
        return;

    if (m_activeProfileName == name)
        setActive(QString::fromStdString(palette.front().name));
    m_commit(std::move(palette), tr("Delete canvas profile"));
}

void ManageCanvasProfilesDialog::onSetActiveClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    setActive(QString::fromStdString(m_profiles[row].name));
    rebuildList();
}

void ManageCanvasProfilesDialog::onGenerateTemplatesClicked()
{
    // Generate for the selected row — MainWindow handles the rest.
    const int row = selectedRow();
    if (row < 0) return;
    emit generateTemplatesRequested(m_profiles[row]);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<Platemaker::Models::CanvasProfile> ManageCanvasProfilesDialog::profilePalette() const
{
    return {m_profiles.begin(), m_profiles.end()};
}

void ManageCanvasProfilesDialog::setActive(const QString &name)
{
    m_activeProfileName = name;
    emit activeProfileChanged(name);
}

void ManageCanvasProfilesDialog::rebuildList()
{
    // Preserve the current selection so we can restore it after rebuilding the list.
    const int previousRow = ui->listWidgetProfiles->currentRow();

    // Rebuild the list widget with the current profiles, marking the active one with a star.
    ui->listWidgetProfiles->clear();
    for (const auto &p : std::as_const(m_profiles)) {
        const QString name = QString::fromStdString(p.name);
        const bool    isActive = (name == m_activeProfileName);
        auto *item = new QListWidgetItem(isActive ? "★  " + name : "     " + name);
        // A name may repeat: what sets this profile apart from its namesakes, or that it is a duplicate.
        item->setData(BadgeItemDelegate::k_badgesRole,
                      QVariant::fromValue(profileDistinctionBadges(profilePalette(), p, ui->listWidgetProfiles->palette())));
        if (isActive) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setForeground(QColor("#7ac8f5"));
        }
        ui->listWidgetProfiles->addItem(item);
    }

    // Restore selection
    const int newRow = qBound(0, previousRow, ui->listWidgetProfiles->count() - 1);
    if (ui->listWidgetProfiles->count() > 0)
        ui->listWidgetProfiles->setCurrentRow(newRow);

    ui->labelActiveInfo->setText(
        m_activeProfileName.isEmpty()
            ? "★ Active profile: —"
            : "★ Active profile: " + m_activeProfileName);

    updateButtonStates();
}

void ManageCanvasProfilesDialog::updateButtonStates()
{
    const bool hasSelection = (selectedRow() >= 0);
    const bool isActive     = hasSelection &&
        (QString::fromStdString(m_profiles[selectedRow()].name) == m_activeProfileName);

    ui->buttonEdit->setEnabled(hasSelection);
    ui->buttonDuplicate->setEnabled(hasSelection);
    ui->buttonDelete->setEnabled(hasSelection && m_profiles.size() > 1);
    ui->buttonSetActive->setEnabled(hasSelection && !isActive);
    ui->buttonGenerateTemplates->setEnabled(hasSelection);
}

int ManageCanvasProfilesDialog::selectedRow() const
{
    return ui->listWidgetProfiles->currentRow();
}
