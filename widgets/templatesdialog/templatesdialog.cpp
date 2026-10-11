#include "templatesdialog.hpp"
#include "ui_templatesdialog.h"
#include "badgeitemdelegate.hpp"

#include <platemaker/core/template_generator/template_generator.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QUrl>

using namespace Platemaker::Models;
using Platemaker::Core::TemplateGenerator;

namespace {

// Turns a profile name into a safe, recognisable template filename stem.
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

// Absolute path of the workspace's "templates" subfolder (may not exist yet).
QString templatesDirFor(const QString& workspaceDir)
{
    return QDir(workspaceDir).filePath(QStringLiteral("templates"));
}

// The file the profile's template is written to, as its settings stand: one name per format.
QString templateFileName(const CanvasProfile& cp)
{
    return sanitizeFileStem(cp.name)
        + (cp.templateSettings.format == TemplateFormat::Psd ? QStringLiteral(".psd") : QStringLiteral(".png"));
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

    const auto    options  = TemplateGenerator::optionsFor(cp);
    const QString fileName = templateFileName(cp);
    const QString absPath  = QDir(templatesDir).filePath(fileName);

    // TemplateGenerator still takes an output profile, but it is currently unused: the slice-guide
    // lines it would drive are compiled out in the lib (see template_generator.cpp). We pass one only
    // to satisfy the signature — any will do, so use the first user profile, else the Webtoon Standard
    // preset (a workspace may legitimately have none of its own, since presets live in code). Either
    // way it is not part of the template's tracked identity.
    const OutputProfile op = ws.outputProfiles().empty()
        ? Platemaker::Models::webtoonStandardPreset() : ws.outputProfiles().front();

    try {
        TemplateGenerator{}.generate(cp, op, absPath.toStdString(), options);
    } catch (const std::exception& e) {
        err = tr("Template generation failed:\n%1").arg(e.what());
        return false;
    }

    cp.templateInfo.path        = (QStringLiteral("templates/") + fileName).toStdString();
    cp.templateInfo.fingerprint = TemplateGenerator::canvasSignature(cp);
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

    if (TemplateGenerator::canvasSignature(cp) != cp.templateInfo.fingerprint)
        return TemplateStatus::Outdated;

    return TemplateStatus::UpToDate;
}

bool TemplatesDialog::confirmOverwrite(QWidget* parent,
                                       const CanvasProfile& cp,
                                       const QString& workspaceDir)
{
    // Only the file this generation writes can be overwritten. A template in the other format is
    // written beside, not over — so with none at this path there is nothing to ask.
    const QString target = QDir(templatesDirFor(workspaceDir)).filePath(templateFileName(cp));
    if (!QFileInfo::exists(target))
        return true;

    const bool tracked = !cp.templateInfo.path.empty()
        && QFileInfo(QDir(workspaceDir).filePath(QString::fromStdString(cp.templateInfo.path)))
               == QFileInfo(target);
    const QString detail = (tracked && statusOf(cp, workspaceDir) == TemplateStatus::UpToDate)
        ? tr("It is already up to date — regenerating will overwrite an identical file.")
        : tr("It is outdated and will be re-rendered.");

    return QMessageBox::question(parent, tr("Regenerate template"),
        tr("A template for \"%1\" already exists.\n%2\n\nOverwrite it?")
            .arg(QString::fromStdString(cp.name), detail),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
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
    // The name column carries a chip saying which kind of file the profile's template is.
    ui->tableTemplates->setItemDelegateForColumn(0, new BadgeItemDelegate(ui->tableTemplates));
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
    connect(ui->buttonRegenerateAll, &QPushButton::clicked,
            this, &TemplatesDialog::onRegenerateAll);
    connect(ui->buttonOpenFolder, &QPushButton::clicked,
            this, &TemplatesDialog::onOpenFolder);
    connect(ui->buttonDeleteTemplate, &QPushButton::clicked,
            this, &TemplatesDialog::onDeleteTemplate);
    connect(ui->comboFormat, &QComboBox::currentIndexChanged, this, &TemplatesDialog::onSettingsEdited);
    connect(ui->checkEmptyLayer, &QCheckBox::toggled, this, &TemplatesDialog::onSettingsEdited);

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
    ui->buttonRegenerateAll->setEnabled(row >= 0);   // it spreads the selected profile's settings

    bool hasTemplate = false;
    if (row >= 0)
        hasTemplate = !m_workspace.canvasProfiles()[static_cast<std::size_t>(row)]
                          .templateInfo.path.empty();
    ui->buttonDeleteTemplate->setEnabled(hasTemplate);
    syncSettings();
}

void TemplatesDialog::onSettingsEdited()
{
    const int row = selectedRow();
    if (m_syncing || row < 0) return;

    // Through the editor, as the CLI's mod-canvas-profile does: a copy of the palette with this
    // profile's settings changed, handed back whole (ids and templateInfo are kept).
    auto profiles = m_workspace.canvasProfiles();
    auto& settings = profiles[static_cast<std::size_t>(row)].templateSettings;
    settings.format     = ui->comboFormat->currentIndex() == 0 ? TemplateFormat::Psd : TemplateFormat::Png;
    settings.emptyLayer = ui->checkEmptyLayer->isChecked();
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(std::move(profiles));
    rebuildTable();   // the settings are part of the template's signature: the status may change
    emit workspaceModified();
}

void TemplatesDialog::onGenerateSelected()
{
    const int row = selectedRow();
    if (row < 0) return;

    // Edit a copy — the palette is private. generateTemplate writes cp.templateInfo, which we then
    // persist onto the workspace profile through the editor.
    CanvasProfile cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];

    if (!confirmOverwrite(this, cp, m_workspaceDir))
        return;

    QString err;
    if (!generateTemplate(m_workspace, m_workspaceDir, cp, err)) {
        QMessageBox::critical(this, tr("Template"), err);
        return;
    }
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
        .setCanvasProfileTemplateInfo(cp.id, cp.templateInfo);
    rebuildTable();
    ui->tableTemplates->selectRow(row);
    emit workspaceModified();

    QMessageBox::information(this, tr("Template"),
        tr("Template generated for \"%1\".").arg(QString::fromStdString(cp.name)));
}

void TemplatesDialog::onGenerateAll()
{
    generateMany(/*onlyOutdated=*/true);
}

void TemplatesDialog::onRegenerateAll()
{
    const int row = selectedRow();
    if (row < 0) return;
    const CanvasTemplateSettings settings =
        m_workspace.canvasProfiles()[static_cast<std::size_t>(row)].templateSettings;

    const QString what = settings.format == TemplateFormat::Png ? tr("PNG")
                       : settings.emptyLayer                    ? tr("PSD with an empty layer")
                                                                : tr("PSD");
    // It edits every profile's settings and rewrites every file, so it asks once. The edit is part of
    // this dialog's undo step, like the rest of it.
    if (QMessageBox::question(this, tr("Regenerate all"),
            tr("Apply the template settings shown (%1) to all %2 canvas profiles and regenerate every "
               "template?").arg(what).arg(m_workspace.canvasProfiles().size()))
        != QMessageBox::Yes)
        return;

    auto profiles = m_workspace.canvasProfiles();
    for (auto& p : profiles)
        p.templateSettings = settings;
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(std::move(profiles));
    generateMany(/*onlyOutdated=*/false);
}

void TemplatesDialog::generateMany(bool onlyOutdated)
{
    int generated = 0;
    QStringList failures;

    // Work on copies (the palette is private), writing each generated template back through the
    // editor. setCanvasProfileTemplateInfo only edits templateInfo, so it does not invalidate the
    // accessor's reference we are iterating.
    for (const auto& orig : m_workspace.canvasProfiles()) {
        if (onlyOutdated && statusOf(orig, m_workspaceDir) == TemplateStatus::UpToDate)
            continue;
        CanvasProfile cp = orig;
        QString err;
        if (generateTemplate(m_workspace, m_workspaceDir, cp, err)) {
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
                .setCanvasProfileTemplateInfo(cp.id, cp.templateInfo);
            ++generated;
        } else {
            failures << QString::fromStdString(cp.name) + ": " + err;
        }
    }

    rebuildTable();
    if (generated > 0 || !onlyOutdated)   // regenerating all also edited the settings
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

    const CanvasProfile& cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];
    if (cp.templateInfo.path.empty()) return;

    const QString abs = QDir(m_workspaceDir).filePath(
        QString::fromStdString(cp.templateInfo.path));

    if (QMessageBox::question(this, tr("Delete template"),
            tr("Delete the template for \"%1\"?\n\n%2\n\n"
               "The file goes to the Recycle Bin the next time the workspace is opened, unless undo "
               "brings the template back first.")
                .arg(QString::fromStdString(cp.name), abs))
        != QMessageBox::Yes)
        return;

    // The file is left where it is. This edit is undoable, and an undo that restored the reference to a
    // file already deleted would restore nothing; the sweep at the next open removes it once nothing
    // references it (MainWindow::sweepWorkspaceFolder()).
    // Clear the template metadata through the editor (an exact empty value — a carry heuristic
    // could not express a deliberate clear).
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
        .setCanvasProfileTemplateInfo(cp.id, CanvasTemplateInfo{});

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
        static_cast<int>(m_workspace.canvasProfiles().size()));

    for (int row = 0; row < static_cast<int>(m_workspace.canvasProfiles().size()); ++row) {
        const auto& cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];
        const TemplateStatus status = statusOf(cp, m_workspaceDir);

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(cp.name));
        // The generated file's format, read from the file it is: after a switch of format the status
        // says Outdated, and this says what is on disk until it is regenerated.
        const QString suffix = QFileInfo(QString::fromStdString(cp.templateInfo.path)).suffix().toLower();
        if (suffix == QLatin1String("psd") || suffix == QLatin1String("png")) {
            const bool psd = suffix == QLatin1String("psd");
            Badge chip = toneBadge(BadgeTone::Neutral, psd ? tr("PSD") : tr("PNG"),
                psd ? tr("The template is a PSD: open it and it is the canvas, the template on its own layer.")
                    : tr("The template is a PNG: an image to place on a canvas of the profile's size."),
                palette());
            // The one chip that names its own colours, on purpose: a PSD wears the Photoshop file
            // icon's — cyan on deep navy — so it is told from the PNG chip at a glance. Dark on any
            // window, it reads the same in either theme.
            if (psd) {
                chip.fill       = QColor(0x00, 0x1e, 0x36);
                chip.border     = QColor(0x31, 0xa8, 0xff);
                chip.textColour = QColor(0x31, 0xa8, 0xff);
            }
            nameItem->setData(BadgeItemDelegate::k_badgesRole, QVariant::fromValue(QList<Badge>{chip}));
        }
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

void TemplatesDialog::syncSettings()
{
    const int row = selectedRow();
    ui->groupBoxSettings->setEnabled(row >= 0);
    if (row < 0) {
        ui->labelDpi->clear();
        ui->labelSizeWarning->hide();
        return;
    }
    const CanvasProfile& cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];
    const bool psd = cp.templateSettings.format == TemplateFormat::Psd;

    m_syncing = true;
    ui->comboFormat->setCurrentIndex(psd ? 0 : 1);
    ui->checkEmptyLayer->setChecked(cp.templateSettings.emptyLayer);
    m_syncing = false;
    ui->checkEmptyLayer->setEnabled(psd);   // a PNG is one flat image

    // The resolution is the canvas profile's: the template is an image of that canvas.
    ui->labelDpi->setText(tr("%1 dpi — set in the canvas profile").arg(cp.dpi));

    // Procreate refuses a canvas over 16384 px a side (less on some devices). The PSD is still
    // written — other apps open it — but say so before anyone carries it to Procreate.
    constexpr int k_procreateMaxSide = 16384;
    const bool tooBig = psd && (cp.canvasSize.width > k_procreateMaxSide
                                || cp.canvasSize.height > k_procreateMaxSide);
    ui->labelSizeWarning->setVisible(tooBig);
    if (tooBig)
        ui->labelSizeWarning->setText(
            tr("This canvas is %1×%2 px. Procreate opens canvases up to %3 px a side (less on some "
               "devices), so it will not open this PSD; other apps will.")
                .arg(cp.canvasSize.width).arg(cp.canvasSize.height).arg(k_procreateMaxSide));
}

int TemplatesDialog::selectedRow() const
{
    const auto rows = ui->tableTemplates->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}
