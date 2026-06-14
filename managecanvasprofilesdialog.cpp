#include "managecanvasprofilesdialog.h"
#include "ui_managecanvasprofilesdialog.h"
#include "canvasprofiledialog.h"

#include <QListWidgetItem>
#include <QMessageBox>

ManageCanvasProfilesDialog::ManageCanvasProfilesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ManageCanvasProfilesDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

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

QList<Platemaker::Models::CanvasProfile> ManageCanvasProfilesDialog::profiles() const
{
    return m_profiles;
}

QString ManageCanvasProfilesDialog::activeProfileName() const
{
    return m_activeProfileName;
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
    CanvasProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    Platemaker::Models::CanvasProfile newProfile = dlg.profile();

    // Guard: name must be unique
    for (const auto &p : std::as_const(m_profiles)) {
        if (QString::fromStdString(p.name) == QString::fromStdString(newProfile.name)) {
            QMessageBox::warning(this, "Duplicate name",
                "A profile with that name already exists. Choose a different name.");
            return;
        }
    }

    m_profiles.append(newProfile);

    // Auto-activate if it's the first profile
    if (m_profiles.size() == 1)
        m_activeProfileName = QString::fromStdString(newProfile.name);

    rebuildList();
}

void ManageCanvasProfilesDialog::onEditClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    CanvasProfileDialog dlg(this);
    dlg.setProfile(m_profiles[row]);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString oldName = QString::fromStdString(m_profiles[row].name);
    m_profiles[row]       = dlg.profile();
    const QString newName = QString::fromStdString(m_profiles[row].name);

    // Keep activeProfileName in sync if this was the active one
    if (m_activeProfileName == oldName)
        m_activeProfileName = newName;

    rebuildList();
}

void ManageCanvasProfilesDialog::onDuplicateClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    Platemaker::Models::CanvasProfile copy = m_profiles[row];
    copy.name += " (copy)";
    m_profiles.insert(row + 1, copy);
    rebuildList();

    ui->listWidgetProfiles->setCurrentRow(row + 1);
}

void ManageCanvasProfilesDialog::onDeleteClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    const QString name = QString::fromStdString(m_profiles[row].name);

    if (m_profiles.size() == 1) {
        QMessageBox::information(this, "Cannot delete",
            "At least one canvas profile must exist.");
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

void ManageCanvasProfilesDialog::onSetActiveClicked()
{
    const int row = selectedRow();
    if (row < 0) return;

    m_activeProfileName = QString::fromStdString(m_profiles[row].name);
    rebuildList();
}

void ManageCanvasProfilesDialog::onGenerateTemplatesClicked()
{
    // Find the active profile and emit the signal — MainWindow handles the rest
    for (const auto &p : std::as_const(m_profiles)) {
        if (QString::fromStdString(p.name) == m_activeProfileName) {
            emit generateTemplatesRequested(p);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ManageCanvasProfilesDialog::rebuildList()
{
    const int previousRow = ui->listWidgetProfiles->currentRow();

    ui->listWidgetProfiles->clear();
    for (const auto &p : std::as_const(m_profiles)) {
        const QString name = QString::fromStdString(p.name);
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
    ui->buttonGenerateTemplates->setEnabled(!m_activeProfileName.isEmpty());
}

int ManageCanvasProfilesDialog::selectedRow() const
{
    return ui->listWidgetProfiles->currentRow();
}
