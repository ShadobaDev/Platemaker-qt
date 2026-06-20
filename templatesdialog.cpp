#include "templatesdialog.h"
#include "ui_templatesdialog.h"

#include <platemaker/core/template_generator/template_generator.hpp>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QUrl>

using namespace Platemaker::Models;
using Platemaker::Core::TemplateGenerator;

namespace {

// Turns a profile name into a safe, recognisable PNG filename stem.
QString sanitizeFileStem(const std::string& name)
{
    QString out;
    out.reserve(static_cast<int>(name.size()));
    for (const QChar c : QString::fromStdString(name)) {
        if (c.isLetterOrNumber() || c == '-' || c == '_' || c == '.')
            out.append(c);
        else if (c.isSpace())
            out.append('_');
        // other characters are dropped
    }
    if (out.isEmpty())
        out = QStringLiteral("template");
    return out;
}

QString templatesDirFor(const QString& workspaceDir)
{
    return QDir(workspaceDir).filePath(QStringLiteral("templates"));
}

} // namespace

// ---------------------------------------------------------------------------
// Static helpers (reused by MainWindow's quick-generate button)
// ---------------------------------------------------------------------------

bool TemplatesDialog::generateTemplate(const Workspace& ws,
                                       const QString& workspaceDir,
                                       CanvasProfile& cp,
                                       QString& err)
{
    const QString templatesDir = templatesDirFor(workspaceDir);
    if (!QDir().mkpath(templatesDir)) {
        err = tr("Cannot create templates folder:\n%1").arg(templatesDir);
        return false;
    }

    const QString fileName = sanitizeFileStem(cp.name) + QStringLiteral(".png");
    const QString absPath  = QDir(templatesDir).filePath(fileName);

    // The output profile only supplies cosmetic slice-guide lines — use the
    // workspace default; it is not part of the template's tracked identity.
    const OutputProfile op = ws.outputProfiles.empty()
        ? OutputProfile{} : ws.outputProfiles.front();

    try {
        TemplateGenerator{}.generate(cp, op, absPath.toStdString());
    } catch (const std::exception& e) {
        err = tr("Template generation failed:\n%1").arg(e.what());
        return false;
    }

    cp.templateInfo.path        = (QStringLiteral("templates/") + fileName).toStdString();
    cp.templateInfo.fingerprint = TemplateGenerator::signature(cp);
    cp.templateInfo.generatedAt =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    return true;
}

TemplatesDialog::TemplateStatus TemplatesDialog::statusOf(
    const CanvasProfile& cp, const QString& workspaceDir)
{
    if (cp.templateInfo.path.empty())
        return TemplateStatus::NotGenerated;

    const QString abs = QDir(workspaceDir).filePath(
        QString::fromStdString(cp.templateInfo.path));
    if (!QFileInfo::exists(abs))
        return TemplateStatus::FileMissing;

    if (TemplateGenerator::signature(cp) != cp.templateInfo.fingerprint)
        return TemplateStatus::Outdated;

    return TemplateStatus::UpToDate;
}

QString TemplatesDialog::statusText(TemplateStatus status)
{
    switch (status) {
    case TemplateStatus::NotGenerated: return tr("Not generated");
    case TemplateStatus::UpToDate:     return tr("Up to date");
    case TemplateStatus::Outdated:     return tr("Outdated");
    case TemplateStatus::FileMissing:  return tr("File missing");
    }
    return {};
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TemplatesDialog::TemplatesDialog(Workspace& workspace,
                                 const QString& workspaceDir,
                                 QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::TemplatesDialog)
    , m_workspace(workspace)
    , m_workspaceDir(workspaceDir)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    ui->tableTemplates->setColumnCount(3);
    ui->tableTemplates->setHorizontalHeaderLabels(
        {tr("Canvas profile"), tr("Status"), tr("Generated")});
    ui->tableTemplates->verticalHeader()->setVisible(false);

    auto* header = ui->tableTemplates->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);          // name fills the gap
    header->setSectionResizeMode(1, QHeaderView::Fixed);            // status: fixed width
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // generated: snug to timestamp

    // Status column = widest status label plus ~4 alphanumeric chars of slack.
    const QFontMetrics fm(ui->tableTemplates->font());
    const int statusW = fm.horizontalAdvance(tr("Not generated"))
                        + fm.horizontalAdvance(QStringLiteral("0000")) + 24;
    ui->tableTemplates->setColumnWidth(1, statusW);

    connect(ui->tableTemplates, &QTableWidget::itemSelectionChanged,
            this, &TemplatesDialog::onSelectionChanged);
    connect(ui->buttonGenerateSelected, &QPushButton::clicked,
            this, &TemplatesDialog::onGenerateSelected);
    connect(ui->buttonGenerateAll, &QPushButton::clicked,
            this, &TemplatesDialog::onGenerateAll);
    connect(ui->buttonOpenFolder, &QPushButton::clicked,
            this, &TemplatesDialog::onOpenFolder);
    connect(ui->buttonDeleteTemplate, &QPushButton::clicked,
            this, &TemplatesDialog::onDeleteTemplate);

    rebuildTable();
}

TemplatesDialog::~TemplatesDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TemplatesDialog::onSelectionChanged()
{
    const int row = selectedRow();
    ui->buttonGenerateSelected->setEnabled(row >= 0);

    bool hasTemplate = false;
    if (row >= 0)
        hasTemplate = !m_workspace.canvasProfiles[static_cast<std::size_t>(row)]
                          .templateInfo.path.empty();
    ui->buttonDeleteTemplate->setEnabled(hasTemplate);
}

void TemplatesDialog::onGenerateSelected()
{
    const int row = selectedRow();
    if (row < 0) return;

    auto& cp = m_workspace.canvasProfiles[static_cast<std::size_t>(row)];
    QString err;
    if (!generateTemplate(m_workspace, m_workspaceDir, cp, err)) {
        QMessageBox::critical(this, tr("Template"), err);
        return;
    }
    rebuildTable();
    ui->tableTemplates->selectRow(row);
    emit workspaceModified();
}

void TemplatesDialog::onGenerateAll()
{
    int generated = 0;
    QStringList failures;

    for (auto& cp : m_workspace.canvasProfiles) {
        if (statusOf(cp, m_workspaceDir) == TemplateStatus::UpToDate)
            continue;
        QString err;
        if (generateTemplate(m_workspace, m_workspaceDir, cp, err))
            ++generated;
        else
            failures << QString::fromStdString(cp.name) + ": " + err;
    }

    rebuildTable();
    if (generated > 0)
        emit workspaceModified();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Template"),
            tr("%1 template(s) generated. The following failed:\n\n%2")
                .arg(generated).arg(failures.join(QStringLiteral("\n"))));
    } else {
        QMessageBox::information(this, tr("Template"),
            tr("%1 template(s) generated.").arg(generated));
    }
}

void TemplatesDialog::onOpenFolder()
{
    const QString dir = templatesDirFor(m_workspaceDir);
    if (!QDir(dir).exists()) {
        QMessageBox::information(this, tr("Templates"),
            tr("No templates folder yet — generate a template first."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TemplatesDialog::onDeleteTemplate()
{
    const int row = selectedRow();
    if (row < 0) return;

    auto& cp = m_workspace.canvasProfiles[static_cast<std::size_t>(row)];
    if (cp.templateInfo.path.empty()) return;

    const QString abs = QDir(m_workspaceDir).filePath(
        QString::fromStdString(cp.templateInfo.path));

    if (QMessageBox::question(this, tr("Delete template"),
            tr("Delete the template for \"%1\"?\n\n%2")
                .arg(QString::fromStdString(cp.name), abs))
        != QMessageBox::Yes)
        return;

    QFile::remove(abs); // ignore result — the file may already be gone
    cp.templateInfo = CanvasTemplateInfo{};

    rebuildTable();
    ui->tableTemplates->selectRow(row);
    emit workspaceModified();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void TemplatesDialog::rebuildTable()
{
    const int previousRow = selectedRow();

    ui->tableTemplates->setRowCount(
        static_cast<int>(m_workspace.canvasProfiles.size()));

    for (int row = 0; row < static_cast<int>(m_workspace.canvasProfiles.size()); ++row) {
        const auto& cp = m_workspace.canvasProfiles[static_cast<std::size_t>(row)];
        const TemplateStatus status = statusOf(cp, m_workspaceDir);

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(cp.name));
        auto* statusItem = new QTableWidgetItem(statusText(status));
        auto* genItem = new QTableWidgetItem(
            QString::fromStdString(cp.templateInfo.generatedAt));

        // Colour-code the status cell for quick scanning.
        QColor colour;
        switch (status) {
        case TemplateStatus::UpToDate:     colour = QColor(0x7a, 0xba, 0x7a); break;
        case TemplateStatus::Outdated:     colour = QColor(0xe0, 0xa0, 0x60); break;
        case TemplateStatus::FileMissing:  colour = QColor(0xe0, 0x60, 0x60); break;
        case TemplateStatus::NotGenerated: colour = QColor(0x88, 0x88, 0x88); break;
        }
        statusItem->setForeground(colour);

        ui->tableTemplates->setItem(row, 0, nameItem);
        ui->tableTemplates->setItem(row, 1, statusItem);
        ui->tableTemplates->setItem(row, 2, genItem);
    }

    if (previousRow >= 0 && previousRow < ui->tableTemplates->rowCount())
        ui->tableTemplates->selectRow(previousRow);

    onSelectionChanged();
}

int TemplatesDialog::selectedRow() const
{
    const auto rows = ui->tableTemplates->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}
