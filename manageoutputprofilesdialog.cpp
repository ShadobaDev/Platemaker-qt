#include "manageoutputprofilesdialog.h"
#include "ui_manageoutputprofilesdialog.h"
#include "outputprofiledialog.h"

#include <platemaker\models\output_profile.hpp>

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
    const int row = selectedRow();
    if (row < 0) return;

    OutputProfileDialog dlg(this);
    dlg.setProfile(m_profiles[row]);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString oldName = QString::fromStdString(m_profiles[row].name);
    m_profiles[row]       = dlg.profile();
    const QString newName = QString::fromStdString(m_profiles[row].name);

    if (m_activeProfileName == oldName)
        m_activeProfileName = newName;

    rebuildList();
}

void ManageOutputProfilesDialog::onDuplicateClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    Platemaker::Models::OutputProfile copy = m_profiles[row];
    copy.name += " (copy)";
    m_profiles.insert(row + 1, copy);
    rebuildList();
    ui->listWidgetProfiles->setCurrentRow(row + 1);
}

void ManageOutputProfilesDialog::onDeleteClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    const QString name = QString::fromStdString(m_profiles[row].name);

    if (m_profiles.size() == 1) {
        QMessageBox::information(this, "Cannot delete",
            "At least one output profile must exist.");
        return;
    }

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
    const int previousRow = ui->listWidgetProfiles->currentRow();

    ui->listWidgetProfiles->clear();
    for (const auto &p : std::as_const(m_profiles)) {
        const QString name     = QString::fromStdString(p.name);
        const bool    isActive = (name == m_activeProfileName);
        auto *item = new QListWidgetItem(isActive ? "★  " + name : "     " + name);
        if (isActive) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setForeground(QColor("#7ac8f5"));
        }
        ui->listWidgetProfiles->addItem(item);
    }

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
    const bool hasSelection = (selectedRow() >= 0);
    const bool isActive     = hasSelection &&
        (QString::fromStdString(m_profiles[selectedRow()].name) == m_activeProfileName);

    ui->buttonEdit->setEnabled(hasSelection);
    ui->buttonDuplicate->setEnabled(hasSelection);
    ui->buttonDelete->setEnabled(hasSelection && m_profiles.size() > 1);
    ui->buttonSetActive->setEnabled(hasSelection && !isActive);
}

int ManageOutputProfilesDialog::selectedRow() const
{
    return ui->listWidgetProfiles->currentRow();
}
