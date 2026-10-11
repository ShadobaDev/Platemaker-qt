#include "templatesdialog.hpp"
#include "ui_templatesdialog.h"
#include "badgeitemdelegate.hpp"
#include "profiledistinction.hpp"

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

#include <algorithm>

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

// The file the profile's template is written to, as its settings stand: one name per format. A profile
// that shares its name with a different one carries what sets it apart ("3p-m_2000x8080.psd"), so the
// two never write one file; an identical twin keeps the plain name and shares it, byte for byte.
QString templateFileName(const std::vector<CanvasProfile>& palette, const CanvasProfile& cp)
{
    return sanitizeFileStem(cp.name) + profileFileSuffix(palette, cp)
        + (cp.templateSettings.format == TemplateFormat::Psd ? QStringLiteral(".psd") : QStringLiteral(".png"));
}

QString formatName(TemplateFormat format)
{
    return format == TemplateFormat::Psd ? QStringLiteral("PSD") : QStringLiteral("PNG");
}

// One changed signature field as the artist reads it: "Resolution 25.4 → 300 dpi".
QString differenceText(const Platemaker::Core::TemplateDifference& d)
{
    const auto t   = [](const char* s) { return TemplatesDialog::tr(s); };
    const QString was = QString::fromStdString(d.was);
    const QString now = QString::fromStdString(d.now);
    const QString from = was.isEmpty() ? t("not recorded") : was;
    struct Label { const char* field; const char* text; };
    static const Label labels[] = {
        {"cw", "Canvas width"},  {"ch", "Canvas height"},
        {"mt", "Top margin"},    {"mr", "Right margin"}, {"mb", "Bottom margin"}, {"ml", "Left margin"},
        {"vc", "Margin colour"}, {"bg", "Background colour"},
    };
    for (const auto& l : labels)
        if (d.field == l.field) return QStringLiteral("%1 %2 → %3").arg(t(l.text), from, now);
    if (d.field == "dpi")
        return t("Resolution %1 → %2 dpi").arg(from, now);
    if (d.field == "fmt")
        return t("Format %1 → %2").arg(was.isEmpty() ? from : was.toUpper(), now.toUpper());
    if (d.field == "layer") {
        const auto yesNo = [&](const QString& v) {
            return v.isEmpty() ? t("not recorded") : v == QLatin1String("1") ? t("yes") : t("no");
        };
        return t("Empty layer %1 → %2").arg(yesNo(was), yesNo(now));
    }
    return QStringLiteral("%1 %2 → %3").arg(QString::fromStdString(d.field), from, now);
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
    const QString fileName = templateFileName(ws.canvasProfiles(), cp);
    const QString absPath  = QDir(templatesDir).filePath(fileName);

    // TemplateGenerator still takes an output profile, but it is currently unused: the slice-guide
    // lines it would drive are compiled out in the lib (see template_generator.cpp). We pass one only
    // to satisfy the signature — any will do, so use the first user profile, else the Webtoon Standard
    // preset (a workspace may legitimately have none of its own, since presets live in code). Either
    // way it is not part of the template's tracked identity.
    const OutputProfile op = ws.outputProfiles().empty()
        ? Platemaker::Models::webtoonStandardPreset() : ws.outputProfiles().front();

    CanvasTemplateInfo entry;
    try {
        // The hash is of the bytes written, not of a read-back that a synced folder may serve stale.
        entry.sha256 = TemplateGenerator{}.generate(cp, op, absPath.toStdString(), options);
    } catch (const std::exception& e) {
        err = tr("Template generation failed:\n%1").arg(e.what());
        return false;
    }

    // This format's entry is replaced; the other format's, if any, stays: both are the user's.
    entry.path        = (QStringLiteral("templates/") + fileName).toStdString();
    entry.format      = options.format;
    entry.fingerprint = TemplateGenerator::canvasSignature(cp);
    entry.generatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    cp.templates.erase(std::remove_if(cp.templates.begin(), cp.templates.end(),
                                      [&](const CanvasTemplateInfo& t) { return t.format == entry.format; }),
                       cp.templates.end());
    cp.templates.push_back(std::move(entry));
    return true;
}

TemplatesDialog::TemplateState TemplatesDialog::stateOf(const std::vector<CanvasProfile>& palette,
                                                        const CanvasProfile& cp,
                                                        const QString& workspaceDir,
                                                        TemplateFormat format)
{
    TemplateState state;
    state.entry = cp.templateIn(format);
    if (!state.entry)
        return state;   // NotGenerated

    const QString abs = QDir(workspaceDir).filePath(QString::fromStdString(state.entry->path));
    const auto check  = TemplateGenerator::check(palette, cp, *state.entry, abs.toStdString());

    // Most decisive first: a missing file, then one another profile overwrote, then one edited
    // elsewhere, then what changed in the profile.
    if (check.fileMissing)
        state.reasons << tr("File missing");
    if (!check.sharedWith.empty())
        state.reasons << tr("Overwritten by another “%1” — regenerate to separate them")
                             .arg(QString::fromStdString(check.sharedWith));
    if (check.changedOutside)
        state.reasons << tr("Changed outside Platemaker");
    for (const auto& d : check.differences)
        state.reasons << differenceText(d);

    state.status = check.fileMissing ? TemplateStatus::FileMissing
                 : check.upToDate()  ? TemplateStatus::UpToDate
                                     : TemplateStatus::Outdated;
    return state;
}

TemplatesDialog::TemplateState TemplatesDialog::stateOf(const std::vector<CanvasProfile>& palette,
                                                        const CanvasProfile& cp,
                                                        const QString& workspaceDir)
{
    return stateOf(palette, cp, workspaceDir, cp.templateSettings.format);
}

QString TemplatesDialog::stateText(const TemplateState& state)
{
    const QString text = statusText(state.status);
    return state.status == TemplateStatus::Outdated && !state.reasons.isEmpty()
        ? text + QStringLiteral(" — ") + state.reasons.first()
        : text;
}

bool TemplatesDialog::confirmOverwrite(QWidget* parent,
                                       const std::vector<CanvasProfile>& palette,
                                       const CanvasProfile& cp,
                                       const QString& workspaceDir)
{
    // Only the file this generation writes can be overwritten. A template in the other format, or
    // under a name set apart from a namesake, is written beside — with none at this path, nothing to ask.
    const QString target = QDir(templatesDirFor(workspaceDir)).filePath(templateFileName(palette, cp));
    if (!QFileInfo::exists(target))
        return true;

    const TemplateState state = stateOf(palette, cp, workspaceDir);
    const bool tracked = state.entry
        && QFileInfo(QDir(workspaceDir).filePath(QString::fromStdString(state.entry->path))) == QFileInfo(target);
    const QString detail = (tracked && state.status == TemplateStatus::UpToDate)
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
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // the name and its chips, in full
    header->setSectionResizeMode(1, QHeaderView::Stretch);          // the status takes what is left, eliding its reason
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // generated: snug to the timestamp
    // However narrow the window, the status itself stays readable — the longest of the words alone; the
    // reason after it may elide, and the cell's tooltip always carries all of it.
    const QFontMetrics fm(ui->tableTemplates->font());
    int statusMin = 0;
    for (const auto st : {TemplateStatus::NotGenerated, TemplateStatus::UpToDate, TemplateStatus::Outdated,
                          TemplateStatus::FileMissing})
        statusMin = qMax(statusMin, fm.horizontalAdvance(statusText(st)));
    header->setMinimumSectionSize(statusMin + 24);   // the cell's padding either side

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

    // Delete removes the template in the format the profile asks for — the one its row speaks about.
    bool hasTemplate = false;
    if (row >= 0) {
        const auto& cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];
        hasTemplate = cp.templateIn(cp.templateSettings.format) != nullptr;
    }
    ui->buttonDeleteTemplate->setEnabled(hasTemplate);
    syncSettings();
}

void TemplatesDialog::onSettingsEdited()
{
    const int row = selectedRow();
    if (m_syncing || row < 0) return;

    // Through the editor, as the CLI's mod-canvas-profile does: a copy of the palette with this
    // profile's settings changed, handed back whole (ids and templates are kept).
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

    // Edit a copy — the palette is private. generateTemplate writes cp.templates, which we then
    // persist onto the workspace profile through the editor.
    CanvasProfile cp = m_workspace.canvasProfiles()[static_cast<std::size_t>(row)];

    if (!confirmOverwrite(this, m_workspace.canvasProfiles(), cp, m_workspaceDir))
        return;

    QString err;
    if (!generateTemplate(m_workspace, m_workspaceDir, cp, err)) {
        QMessageBox::critical(this, tr("Template"), err);
        return;
    }
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).setCanvasProfileTemplates(cp.id, cp.templates);
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
    // editor. setCanvasProfileTemplates only edits templates, so it does not invalidate the
    // accessor's reference we are iterating.
    for (const auto& orig : m_workspace.canvasProfiles()) {
        if (onlyOutdated && stateOf(m_workspace.canvasProfiles(), orig, m_workspaceDir).status
                                == TemplateStatus::UpToDate)
            continue;
        CanvasProfile cp = orig;
        QString err;
        if (generateTemplate(m_workspace, m_workspaceDir, cp, err)) {
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace).setCanvasProfileTemplates(cp.id, cp.templates);
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
    const TemplateFormat format = cp.templateSettings.format;
    const CanvasTemplateInfo* entry = cp.templateIn(format);
    if (!entry) return;

    const QString abs = QDir(m_workspaceDir).filePath(QString::fromStdString(entry->path));

    if (QMessageBox::question(this, tr("Delete template"),
            tr("Delete the %3 template for \"%1\"?\n\n%2\n\n"
               "The file goes to the Recycle Bin the next time the workspace is opened, unless undo "
               "brings the template back first.")
                .arg(QString::fromStdString(cp.name), abs, formatName(format)))
        != QMessageBox::Yes)
        return;

    // The file is left where it is. This edit is undoable, and an undo that restored the reference to a
    // file already deleted would restore nothing; the sweep at the next open removes it once nothing
    // references it (MainWindow::sweepWorkspaceFolder()).
    // Drop that format's entry through the editor (an exact value — a carry heuristic could not express
    // a deliberate removal). The other format's template, if any, stays.
    std::vector<CanvasTemplateInfo> kept;
    for (const auto& t : cp.templates)
        if (t.format != format) kept.push_back(t);
    Platemaker::Infrastructure::WorkspaceEditor(m_workspace).setCanvasProfileTemplates(cp.id, std::move(kept));

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
        const auto& palette_ = m_workspace.canvasProfiles();
        const auto& cp = palette_[static_cast<std::size_t>(row)];
        // The row speaks about the template in the format the profile asks for.
        const TemplateState state = stateOf(palette_, cp, m_workspaceDir);
        const TemplateStatus status = state.status;

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(cp.name));
        // One chip per file on disk — a PNG and a PSD can both be there — each with its own state, then
        // what sets the profile apart from a namesake.
        QList<Badge> chips;
        for (const auto& t : cp.templates) {
            const bool psd = t.format == TemplateFormat::Psd;
            const TemplateState own = stateOf(palette_, cp, m_workspaceDir, t.format);
            QString detail = psd
                ? tr("The PSD template: open it and it is the canvas, the template on its own layer.")
                : tr("The PNG template: an image to place on a canvas of the profile's size.");
            detail += QStringLiteral("\n") + statusText(own.status);
            if (!own.reasons.isEmpty())
                detail += QStringLiteral(":\n• ") + own.reasons.join(QStringLiteral("\n• "));
            Badge chip = toneBadge(BadgeTone::Neutral, formatName(t.format), detail, palette());
            // The one chip that names its own colours, on purpose: a PSD wears the Photoshop file
            // icon's — cyan on deep navy — so it is told from the PNG chip at a glance. Dark on any
            // window, it reads the same in either theme.
            if (psd) {
                chip.fill       = QColor(0x00, 0x1e, 0x36);
                chip.border     = QColor(0x31, 0xa8, 0xff);
                chip.textColour = QColor(0x31, 0xa8, 0xff);
            }
            chips << chip;
        }
        chips << profileDistinctionBadges(palette_, cp, palette());
        nameItem->setData(BadgeItemDelegate::k_badgesRole, QVariant::fromValue(chips));

        auto* statusItem = new QTableWidgetItem(stateText(state));
        // The column may cut the reason short; the tooltip never does.
        statusItem->setToolTip(state.reasons.isEmpty()
            ? statusText(status)
            : statusText(status) + QStringLiteral(":\n• ") + state.reasons.join(QStringLiteral("\n• ")));
        auto* genItem = new QTableWidgetItem(
            state.entry ? QString::fromStdString(state.entry->generatedAt) : QString{});

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
