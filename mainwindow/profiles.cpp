#include "mainwindow.hpp"
#include "profileimpact.hpp"
#include "ui_mainwindow.h"
#include "project.hpp"
#include "canvasprofiledialog.hpp"
#include "managecanvasprofilesdialog.hpp"
#include "manageoutputprofilesdialog.hpp"
#include "outputprofiledialog.hpp"
#include "profilepickerdialog.hpp"
#include "templatesdialog.hpp"
#include "renderworker.hpp"
#include "presets/presetsdialog.hpp"
#include "presetstore.hpp"

#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <QAction>
#include <QCloseEvent>
#include <QCollator>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

void MainWindow::bindWorkspaceHistory(QWidget& window)
{
    // A modal window keeps the main window's Undo / Redo shortcuts from firing, so it carries its own.
    auto* undo = new QAction(&window);
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, m_workspaceUndoStack, &QUndoStack::undo);
    window.addAction(undo);
    auto* redo = new QAction(&window);
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, m_workspaceUndoStack, &QUndoStack::redo);
    window.addAction(redo);
}

// ---------------------------------------------------------------------------
// Canvas profile slots
//
// Profile ids come from the library (Infrastructure::makeUnique*ProfileId), never from this
// file.  They used to be a millisecond timestamp minted here, which meant every profile
// added in one pass of the manage dialog got the *same* id — and a shared id makes the
// second profile unreachable, so it silently dropped out of the "assign profile" list.
// The library draws random ids and rejects any that is already taken.
// ---------------------------------------------------------------------------

void MainWindow::onManageCanvasProfiles()
{
    manageCanvasProfiles({});
}

void MainWindow::manageCanvasProfiles(const QString& showId)
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // The dialog keeps no draft: each action is written here at once, as its own undo step, and the
    // list follows the workspace — after an action, and after an undo or redo made in the dialog.
    ManageCanvasProfilesDialog dlg(this);
    const auto show = [this, &dlg] {
        dlg.setProfiles({m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end()},
                        m_activeCanvasProfileName);
    };
    show();
    connect(this, &MainWindow::workspaceChanged, &dlg, show);
    connect(&dlg, &ManageCanvasProfilesDialog::activeProfileChanged, this,
            [this](const QString& name) { m_activeCanvasProfileName = name; });
    // Asked where the change is made: in the profile editor on Save, in the manager on Delete.
    dlg.setChangeCheck([this](QWidget* parent, const std::vector<Platemaker::Models::CanvasProfile>& palette,
                              PaletteChange change) {
        return confirmCanvasPalette(parent, m_workspace, palette, change);
    });
    // The editor mints ids for new profiles, deduplicates, and carries each profile's templates.
    dlg.setCommit([this](std::vector<Platemaker::Models::CanvasProfile> palette, const QString& text) {
        commitWorkspaceEdit(text, [&]{
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(std::move(palette));
            setDirty(true);
        });
    });
    bindWorkspaceHistory(dlg);

    // Quick-generate from the selected profile, which is the workspace's own: the template is written
    // onto it at once. A file is made, so this is not an undo step.
    const QString workspaceDir = QFileInfo(m_workspacePath).absolutePath();
    connect(&dlg, &ManageCanvasProfilesDialog::generateTemplatesRequested,
        this, [this, workspaceDir](const Platemaker::Models::CanvasProfile& selected) {
            // Ask before overwriting an existing template.
            if (!TemplatesDialog::confirmOverwrite(this, m_workspace.canvasProfiles(), selected, workspaceDir))
                return;

            // Render from a copy, then write the resulting template metadata onto the workspace profile
            // through the editor.
            Platemaker::Models::CanvasProfile render = selected;
            QString err;
            if (!TemplatesDialog::generateTemplate(m_workspace, workspaceDir, render, err)) {
                QMessageBox::critical(this, tr("Template"), err);
                return;
            }
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
                .setCanvasProfileTemplates(selected.id, render.templates);
            refreshAllAdvisories();   // a template it reported may be up to date now
            QMessageBox::information(this, tr("Template"),
                tr("Template generated for \"%1\".")
                    .arg(QString::fromStdString(selected.name)));
        });

    m_canvasManager = &dlg;
    if (!showId.isEmpty())   // once it shows: a row is outlined where it is drawn
        QTimer::singleShot(0, &dlg, [&dlg, showId] { dlg.showProfile(showId); });
    dlg.exec();
}

void MainWindow::onNewCanvasProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Prompt for a new profile. The dialog returns a fresh profile without an id or templates; we generate a stable id and leave templates empty.
    // A name may repeat: profiles of one name are told apart by what differs (profiledistinction), and
    // the dialog warns about an identical one instead of refusing it.
    CanvasProfileDialog dlg(this);
    dlg.setExistingProfiles(m_workspace.canvasProfiles());
    dlg.setSaveCheck([this, &dlg](const Platemaker::Models::CanvasProfile& cp) {
        return confirmCanvasPalette(&dlg, m_workspace, withProfile(
            {m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end()}, cp));
    });
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    commitWorkspaceEdit(tr("New canvas profile"), [&]{
        // The editor mints a stable id and appends it to the workspace palette.
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).addCanvasProfile(profile);

        // If this is the first profile, make it the active one.
        if (m_workspace.canvasProfiles().size() == 1)
            m_activeCanvasProfileName = QString::fromStdString(profile.name);

        setDirty(true);
    });
}

void MainWindow::onEditActiveCanvasProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Find the active profile by name. The dialog edits a copy, 
    // and we only write the results back to the workspace if the user clicks Accept. 
    // The dialog preserves templates by id, so we can snapshot them before
    // the dialog and re-attach them afterward (the dialog never edits them).
    // Edit a copy of the palette, then hand it back through the editor (it preserves the ids and
    // carries templates). The vectors are private, so we cannot mutate a live profile in place.
    auto profiles = std::vector<Platemaker::Models::CanvasProfile>(
        m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end());
    const auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const auto& p){ return p.name == m_activeCanvasProfileName.toStdString(); });

    if (it == profiles.end()) {
        QMessageBox::information(this, tr("No Active Profile"),
            tr("No active canvas profile. Use Canvas → Manage profiles to create one."));
        return;
    }

    CanvasProfileDialog dlg(this);
    dlg.setProfile(*it);
    dlg.setExistingProfiles(m_workspace.canvasProfiles());
    dlg.setSaveCheck([this, &dlg, &profiles](const Platemaker::Models::CanvasProfile& cp) {
        return confirmCanvasPalette(&dlg, m_workspace, withProfile(profiles, cp));
    });
    if (dlg.exec() != QDialog::Accepted) return;

    const std::string oldName = it->name;
    // CanvasProfileDialog hands back the profile it was given with its fields applied, so the id
    // (project links) and templates survive: if a canvas-affecting field changed, the kept
    // fingerprint won't match → Outdated.
    *it = dlg.profile();

    if (m_activeCanvasProfileName == QString::fromStdString(oldName))
        m_activeCanvasProfileName = QString::fromStdString(it->name);

    commitWorkspaceEdit(tr("Edit canvas profile"), [&]{
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceCanvasProfiles(std::move(profiles));

        setDirty(true);
    });
}
// ---------------------------------------------------------------------------
// Output profile slots
// ---------------------------------------------------------------------------

void MainWindow::onManageOutputProfiles()
{
    manageOutputProfiles({});
}

void MainWindow::manageOutputProfiles(const QString& showId)
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // The dialog keeps no draft: each action is written here at once, as its own undo step, and the
    // list follows the workspace — after an action, and after an undo or redo made in the dialog.
    // It shows the user's own profiles and the code-defined presets in one list. Presets are read-only
    // in the dialog (Edit/Delete disabled) and never persisted; customising one is a Duplicate, which
    // gives it an id of its own and so an ordinary user profile.
    ManageOutputProfilesDialog dlg(this);
    const auto show = [this, &dlg] {
        QList<Platemaker::Models::OutputProfile> profiles(
            m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());
        for (const auto& preset : Platemaker::Models::outputProfilePresets())
            profiles.append(preset);
        dlg.setProfiles(profiles, m_activeOutputProfileId);
    };
    show();
    connect(this, &MainWindow::workspaceChanged, &dlg, show);
    connect(&dlg, &ManageOutputProfilesDialog::activeProfileChanged, this,
            [this](const QString& id) { m_activeOutputProfileId = id; });
    // The editor drops any preset (presets live in code), mints ids for new profiles, and deduplicates.
    dlg.setCommit([this](std::vector<Platemaker::Models::OutputProfile> profiles, const QString& text) {
        commitWorkspaceEdit(text, [&]{
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(std::move(profiles));
            setDirty(true);
        });
    });
    bindWorkspaceHistory(dlg);

    m_outputManager = &dlg;
    if (!showId.isEmpty())
        QTimer::singleShot(0, &dlg, [&dlg, showId] { dlg.showProfile(showId); });
    dlg.exec();
}

void MainWindow::onNewOutputProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    OutputProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto profile = dlg.profile();

    // Ensure the name is unique within the workspace.
    for (const auto& p : m_workspace.outputProfiles()) {
        if (p.name == profile.name) {
            QMessageBox::warning(this, tr("Duplicate name"),
                tr("An output profile named \"%1\" already exists.")
                    .arg(QString::fromStdString(profile.name)));
            return;
        }
    }

    commitWorkspaceEdit(tr("New output profile"), [&]{
        // The editor mints a stable user id (never a preset id) and appends it.
        const std::string newId =
            Platemaker::Infrastructure::WorkspaceEditor(m_workspace).addOutputProfile(profile);

        if (m_workspace.outputProfiles().size() == 1)
            m_activeOutputProfileId = QString::fromStdString(newId);

        setDirty(true);
    });
}

void MainWindow::onEditActiveOutputProfile()
{
    // Skip if no workspace is loaded
    if (m_workspacePath.isEmpty()) {
        QMessageBox::information(this, tr("No Workspace"), tr("Open a workspace first."));
        return;
    }

    // Resolve the active profile by id. The dialog edits a copy, and we only write the results
    // back to the workspace if the user clicks Accept.
    const std::string activeId = m_activeOutputProfileId.toStdString();

    // Presets are read-only. This is the *second* way into the editor, next to the Manage
    // dialog's Edit button; leaving it open would make that dialog's guard bypassable with one
    // click from the menu. A preset is never in outputProfiles (code-defined, not persisted), so
    // an active preset id is caught here first.
    if (Platemaker::Models::outputPresetDefById(activeId) != nullptr) {
        const auto preset = Platemaker::Models::resolveOutputProfile(m_workspace, activeId);
        QMessageBox::information(this, tr("Preset profile"),
            tr("\"%1\" is a built-in preset and cannot be edited — it is shared by every "
               "workspace, so it has to mean the same thing everywhere.\n\n"
               "Use Output → Manage → Duplicate to make your own version of it, which you "
               "can then change freely.")
                .arg(preset ? QString::fromStdString(preset->name) : m_activeOutputProfileId));
        return;
    }

    // Edit a copy of the palette, then hand it back through the editor (vectors are private).
    auto profiles = std::vector<Platemaker::Models::OutputProfile>(
        m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());
    const auto it = std::find_if(profiles.begin(), profiles.end(),
        [&](const auto& p){ return p.id == activeId; });

    if (it == profiles.end()) {
        QMessageBox::information(this, tr("No Active Profile"),
            tr("No active output profile. Use Output → Manage to create one."));
        return;
    }

    OutputProfileDialog dlg(this);
    dlg.setProfile(*it);
    if (dlg.exec() != QDialog::Accepted) return;

    // OutputProfileDialog hands back the profile it was given with its fields applied, so the id
    // projects reference — and the active profile is tracked by — is unchanged even if the name is.
    *it = dlg.profile();

    commitWorkspaceEdit(tr("Edit output profile"), [&]{
        Platemaker::Infrastructure::WorkspaceEditor(m_workspace).replaceOutputProfiles(std::move(profiles));

        setDirty(true);
    });
}

// ---------------------------------------------------------------------------
// Profile portability (import / export)
//
// Import always goes through Infrastructure::WorkspaceEditor::importProfiles (fresh ids, template
// cleared, presets dropped) — the single transfer rule shared with the CLI — so the workspace stays
// self-contained. The "user library" is a GUI convenience only: a profile bundle stored in the OS
// app-data dir, offered as one import source and one export target. The library never auto-injects
// into a workspace; the lib knows nothing about it.
// ---------------------------------------------------------------------------

namespace {

const char* formatName(Platemaker::Models::OutputFormat f)
{
    switch (f) {
        case Platemaker::Models::OutputFormat::PNG:  return "PNG";
        case Platemaker::Models::OutputFormat::JPEG: return "JPEG";
        case Platemaker::Models::OutputFormat::WebP: return "WebP";
    }
    return "PNG";
}

const char* policyName(Platemaker::Models::LastSlicePolicy p)
{
    switch (p) {
        case Platemaker::Models::LastSlicePolicy::Crop:     return "Crop";
        case Platemaker::Models::LastSlicePolicy::PadWhite: return "Pad with white";
        case Platemaker::Models::LastSlicePolicy::KeepAsIs: return "Keep as is";
    }
    return "Keep as is";
}

const char* subsamplingName(Platemaker::Models::JpegSubsampling s)
{
    switch (s) {
        case Platemaker::Models::JpegSubsampling::YUV_444: return "4:4:4";
        case Platemaker::Models::JpegSubsampling::YUV_422: return "4:2:2";
        case Platemaker::Models::JpegSubsampling::YUV_420: return "4:2:0";
    }
    return "4:4:4";
}

// A read-only value field for the inspection panels — selectable so a number can be copied out.
QLabel* valueLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

// A colour swatch rendered exactly like the profile editor's colour buttons. The colour is the
// profile's *data*, so filling with its rgba() is legitimate (not UI theming); alpha shows by letting
// the fill blend over the panel behind it, matching the editor.
QFrame* colourSwatch(const Platemaker::Models::RGBA& c)
{
    auto* f = new QFrame;
    f->setMinimumHeight(22);
    f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    f->setStyleSheet(QStringLiteral(
        "background-color: rgba(%1,%2,%3,%4); border: 1px solid #555555; border-radius: 3px;")
        .arg(static_cast<int>(c.r)).arg(static_cast<int>(c.g))
        .arg(static_cast<int>(c.b)).arg(static_cast<int>(c.a)));
    return f;
}

// Read-only inspection panel for a canvas profile, grouped like the editor (Canvas size / Margins /
// Colours). The Margins group is omitted entirely when every margin is zero, and Safe area is shown
// only when it actually differs from the canvas.
QWidget* canvasInfoWidget(const Platemaker::Models::CanvasProfile& cp)
{
    const int  safeW    = cp.canvasSize.width  - cp.margins.left - cp.margins.right;
    const int  safeH    = cp.canvasSize.height - cp.margins.top  - cp.margins.bottom;
    const bool margined = cp.margins.top || cp.margins.right || cp.margins.bottom || cp.margins.left;

    auto* root = new QWidget;
    auto* v    = new QVBoxLayout(root);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    {
        auto* g  = new QGroupBox(QObject::tr("Profile"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Name:"), valueLabel(QString::fromStdString(cp.name)));
        v->addWidget(g);
    }
    {
        auto* g  = new QGroupBox(QObject::tr("Canvas size"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Width:"),  valueLabel(QObject::tr("%1 px").arg(cp.canvasSize.width)));
        fl->addRow(QObject::tr("Height:"), valueLabel(QObject::tr("%1 px").arg(cp.canvasSize.height)));
        if (margined)
            fl->addRow(QObject::tr("Safe area:"),
                       valueLabel(QObject::tr("%1 × %2 px").arg(safeW).arg(safeH)));
        v->addWidget(g);
    }
    if (margined) {
        auto* g  = new QGroupBox(QObject::tr("Margins (px)"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Top:"),    valueLabel(QString::number(cp.margins.top)));
        fl->addRow(QObject::tr("Bottom:"), valueLabel(QString::number(cp.margins.bottom)));
        fl->addRow(QObject::tr("Left:"),   valueLabel(QString::number(cp.margins.left)));
        fl->addRow(QObject::tr("Right:"),  valueLabel(QString::number(cp.margins.right)));
        v->addWidget(g);
    }
    {
        auto* g  = new QGroupBox(QObject::tr("Colours"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Visual / margin colour:"), colourSwatch(cp.visualColour));
        fl->addRow(QObject::tr("Background colour:"),      colourSwatch(cp.backgroundColour));
        v->addWidget(g);
    }
    v->addStretch(1);
    return root;
}

// Read-only inspection panel for an output profile, grouped like the editor.
QWidget* outputInfoWidget(const Platemaker::Models::OutputProfile& op)
{
    using OF = Platemaker::Models::OutputFormat;

    auto* root = new QWidget;
    auto* v    = new QVBoxLayout(root);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    {
        auto* g  = new QGroupBox(QObject::tr("Profile"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Name:"), valueLabel(QString::fromStdString(op.name)));
        v->addWidget(g);
    }
    {
        auto* g  = new QGroupBox(QObject::tr("Output size"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Target width:"), valueLabel(QObject::tr("%1 px").arg(op.targetWidth)));
        fl->addRow(QObject::tr("Slice height:"), valueLabel(QObject::tr("%1 px").arg(op.sliceHeight)));
        v->addWidget(g);
    }
    {
        auto* g  = new QGroupBox(QObject::tr("Format"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Format:"), valueLabel(QString::fromUtf8(formatName(op.outputFormat))));
        switch (op.outputFormat) {
            case OF::JPEG:
                fl->addRow(QObject::tr("Quality:"),     valueLabel(QString::number(op.jpegOptions.quality)));
                fl->addRow(QObject::tr("Subsampling:"), valueLabel(QString::fromUtf8(subsamplingName(op.jpegOptions.subsampling))));
                fl->addRow(QObject::tr("Optimize:"),    valueLabel(op.jpegOptions.optimize    ? QObject::tr("yes") : QObject::tr("no")));
                fl->addRow(QObject::tr("Progressive:"), valueLabel(op.jpegOptions.progressive ? QObject::tr("yes") : QObject::tr("no")));
                break;
            case OF::PNG:
                fl->addRow(QObject::tr("Compression:"), valueLabel(QString::number(op.pngOptions.compression)));
                fl->addRow(QObject::tr("Interlaced:"),  valueLabel(op.pngOptions.interlaced ? QObject::tr("yes") : QObject::tr("no")));
                break;
            case OF::WebP:
                if (op.webpOptions.lossless)
                    fl->addRow(QObject::tr("Mode:"), valueLabel(QObject::tr("lossless")));
                else
                    fl->addRow(QObject::tr("Quality:"), valueLabel(QString::number(op.webpOptions.quality)));
                fl->addRow(QObject::tr("Effort:"), valueLabel(QString::number(op.webpOptions.effort)));
                break;
        }
        v->addWidget(g);
    }
    {
        auto* g  = new QGroupBox(QObject::tr("Slicing"));
        auto* fl = new QFormLayout(g);
        fl->addRow(QObject::tr("Last slice:"),  valueLabel(QString::fromUtf8(policyName(op.lastSlicePolicy))));
        fl->addRow(QObject::tr("Start index:"), valueLabel(QString::number(op.startIndex)));
        v->addWidget(g);
    }
    v->addStretch(1);
    return root;
}

ProfilePickerDialog::Row canvasRow(const Platemaker::Models::CanvasProfile& cp)
{
    const bool margined = cp.margins.top || cp.margins.right || cp.margins.bottom || cp.margins.left;

    ProfilePickerDialog::Row r;
    r.title   = QString::fromStdString(cp.name);
    r.summary = QStringLiteral("%1 × %2").arg(cp.canvasSize.width).arg(cp.canvasSize.height);
    if (margined)
        r.summaryBadges.append({ QObject::tr("margins"), QColor(0x9F, 0xD8, 0x9F),   // light green
                                 QObject::tr("This profile crops the page: %1 top, %2 right, "
                                             "%3 bottom, %4 left.")
                                     .arg(cp.margins.top).arg(cp.margins.right)
                                     .arg(cp.margins.bottom).arg(cp.margins.left) });
    r.details = canvasInfoWidget(cp);
    return r;
}

ProfilePickerDialog::Row outputRow(const Platemaker::Models::OutputProfile& op)
{
    ProfilePickerDialog::Row r;
    r.title   = QString::fromStdString(op.name);
    r.summary = QStringLiteral("%1 × %2  ·  %3")
                    .arg(op.targetWidth).arg(op.sliceHeight).arg(formatName(op.outputFormat));
    r.details = outputInfoWidget(op);
    return r;
}

} // namespace

QString MainWindow::userProfileLibraryPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("user.platemaker.profiles.json"));
}

QStringList MainWindow::recentBundles() const
{
    return QSettings().value(QStringLiteral("recentBundles")).toStringList();
}

void MainWindow::addToRecentBundles(const QString& path)
{
    if (path.isEmpty()) return;
    QStringList list = recentBundles();
    list.removeAll(path);          // de-dup, then promote to the front
    list.prepend(path);
    while (list.size() > k_maxRecentBundles)
        list.removeLast();
    QSettings().setValue(QStringLiteral("recentBundles"), list);
}

void MainWindow::populateImportMenu(QMenu* menu, bool canvasKind)
{
    // Rebuilt on every open (aboutToShow), so the recent lists stay current. Only leaf actions are
    // added (no nested QMenus), so QMenu::clear() releases them cleanly each time.
    menu->clear();

    if (m_workspacePath.isEmpty()) {
        menu->addAction(tr("(open a workspace first)"))->setEnabled(false);
        return;
    }

    connect(menu->addAction(tr("Browse for a file…")), &QAction::triggered, this,
        [this, canvasKind]{
            importProfilesFlow(canvasKind, QFileDialog::getOpenFileName(
                this, tr("Import profiles from"), defaultDialogDir(),
                tr("Platemaker profiles (*.platemaker.profiles.json *.platemaker.json);;All files (*)")));
        });

    const QString libPath = userProfileLibraryPath();
    if (QFileInfo::exists(libPath))
        connect(menu->addAction(tr("From your profile library")), &QAction::triggered, this,
                [this, canvasKind, libPath]{ importProfilesFlow(canvasKind, libPath); });

    menu->addSection(tr("Recent workspaces"));
    bool anyWs = false;
    for (const QString& p : recentWorkspaces()) {
        if (p == m_workspacePath) continue;                 // skip the workspace we're importing into
        QAction* a = menu->addAction(QFileInfo(p).fileName());
        a->setToolTip(QDir::toNativeSeparators(p));
        connect(a, &QAction::triggered, this, [this, canvasKind, p]{ importProfilesFlow(canvasKind, p); });
        anyWs = true;
    }
    if (!anyWs) menu->addAction(tr("(none)"))->setEnabled(false);

    menu->addSection(tr("Recent bundles"));
    bool anyBundle = false;
    for (const QString& p : recentBundles()) {
        QAction* a = menu->addAction(QFileInfo(p).fileName());
        a->setToolTip(QDir::toNativeSeparators(p));
        connect(a, &QAction::triggered, this, [this, canvasKind, p]{ importProfilesFlow(canvasKind, p); });
        anyBundle = true;
    }
    if (!anyBundle) menu->addAction(tr("(none)"))->setEnabled(false);
}

void MainWindow::populateExportMenu(QMenu* menu, bool canvasKind)
{
    menu->clear();
    if (m_workspacePath.isEmpty()) {
        menu->addAction(tr("(open a workspace first)"))->setEnabled(false);
        return;
    }
    connect(menu->addAction(tr("To a bundle file…")), &QAction::triggered, this,
            [this, canvasKind]{ exportProfilesFlow(canvasKind, /*toFile=*/true); });
    connect(menu->addAction(tr("Add to my profile library")), &QAction::triggered, this,
            [this, canvasKind]{ exportProfilesFlow(canvasKind, /*toFile=*/false); });
}

bool MainWindow::loadProfilesFromFile(const QString&                                  path,
                                      std::vector<Platemaker::Models::CanvasProfile>& canvasOut,
                                      std::vector<Platemaker::Models::OutputProfile>& outputOut)
{
    // Peek at the JSON: a full workspace carries a "projectItems" array; a bundle does not. This lets
    // one action accept either a .platemaker.json workspace or a .platemaker.profiles.json bundle.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import"),
            tr("Cannot open:\n%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const bool isWorkspace = doc.isObject() && doc.object().contains(QStringLiteral("projectItems"));

    try {
        if (isWorkspace) {
            Platemaker::Models::Workspace ws = m_serializer.load(path.toStdString());
            canvasOut.assign(ws.canvasProfiles().begin(), ws.canvasProfiles().end());
            outputOut.assign(ws.outputProfiles().begin(), ws.outputProfiles().end());
        } else {
            Platemaker::Infrastructure::ProfileBundle b =
                Platemaker::Infrastructure::ProfileBundleSerializer{}.load(path.toStdString());
            canvasOut = std::move(b.canvasProfiles);
            outputOut = std::move(b.outputProfiles);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Import"),
            tr("Could not read profiles from:\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromUtf8(e.what())));
        return false;
    }
    return true;
}

bool MainWindow::addToUserLibrary(const std::vector<Platemaker::Models::CanvasProfile>& canvas,
                                  const std::vector<Platemaker::Models::OutputProfile>& output)
{
    const QString path = userProfileLibraryPath();

    // Start from the existing library (if any) so exporting one kind never clobbers the other.
    std::vector<Platemaker::Models::CanvasProfile> libCanvas;
    std::vector<Platemaker::Models::OutputProfile> libOutput;
    if (QFileInfo::exists(path) && !loadProfilesFromFile(path, libCanvas, libOutput))
        return false;

    // Upsert by name within each kind: a re-export updates the stored profile instead of piling up
    // duplicates. (ProfileBundleSerializer::save re-mints nothing; names are the user's handle here.)
    for (const auto& cp : canvas) {
        libCanvas.erase(std::remove_if(libCanvas.begin(), libCanvas.end(),
                        [&](const auto& e){ return e.name == cp.name; }), libCanvas.end());
        libCanvas.push_back(cp);
    }
    for (const auto& op : output) {
        libOutput.erase(std::remove_if(libOutput.begin(), libOutput.end(),
                        [&](const auto& e){ return e.name == op.name; }), libOutput.end());
        libOutput.push_back(op);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    try {
        Platemaker::Infrastructure::ProfileBundleSerializer{}.save(libCanvas, libOutput, path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export"),
            tr("Could not write the profile library:\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(path), QString::fromUtf8(e.what())));
        return false;
    }
    return true;
}

void MainWindow::importProfilesFlow(bool canvasKind, const QString& src)
{
    // Empty = the user cancelled the Browse dialog; no workspace = the submenu already guarded it.
    if (src.isEmpty() || m_workspacePath.isEmpty())
        return;

    std::vector<Platemaker::Models::CanvasProfile> canvas;
    std::vector<Platemaker::Models::OutputProfile> output;
    if (!loadProfilesFromFile(src, canvas, output))
        return;

    // Remember bundle sources (not the library, not workspaces) so re-importing another profile from
    // the same bundle later is one click away in the "Recent bundles" list.
    if (src != userProfileLibraryPath() &&
        src.endsWith(QStringLiteral(".platemaker.profiles.json"), Qt::CaseInsensitive))
        addToRecentBundles(src);

    QList<ProfilePickerDialog::Row> rows;
    if (canvasKind)
        for (const auto& cp : canvas) rows.append(canvasRow(cp));
    else
        for (const auto& op : output) rows.append(outputRow(op));

    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Import"),
            canvasKind ? tr("The selected source has no canvas profiles.")
                       : tr("The selected source has no output profiles."));
        return;
    }

    ProfilePickerDialog dlg(this);
    dlg.setWindowTitle(canvasKind ? tr("Import canvas profiles") : tr("Import output profiles"));
    dlg.setIntro(tr("From: %1").arg(QDir::toNativeSeparators(src)));
    dlg.setConfirmText(tr("Import"));
    dlg.setRows(rows);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QList<int> picked = dlg.checkedIndices();
    if (picked.isEmpty())
        return;

    std::vector<Platemaker::Models::CanvasProfile> chosenCanvas;
    std::vector<Platemaker::Models::OutputProfile> chosenOutput;
    if (canvasKind)
        for (int i : picked) chosenCanvas.push_back(std::move(canvas[i]));
    else
        for (int i : picked) chosenOutput.push_back(std::move(output[i]));

    Platemaker::Infrastructure::ImportProfilesReport report;
    commitWorkspaceEdit(canvasKind ? tr("Import canvas profiles") : tr("Import output profiles"), [&]{
        report = Platemaker::Infrastructure::WorkspaceEditor(m_workspace)
                     .importProfiles(std::move(chosenCanvas), std::move(chosenOutput));
        setDirty(true);
    });

    const int n = canvasKind ? static_cast<int>(report.canvasIds.size())
                             : static_cast<int>(report.outputIds.size());
    QMessageBox::information(this, tr("Import"),
        tr("Imported %1 profile(s).").arg(n));
}

void MainWindow::exportProfilesFlow(bool canvasKind, bool toFile)
{
    if (m_workspacePath.isEmpty())
        return; // the submenu already guards this

    // Snapshot the palette of the requested kind (the accessors return const views).
    std::vector<Platemaker::Models::CanvasProfile> canvas(
        m_workspace.canvasProfiles().begin(), m_workspace.canvasProfiles().end());
    std::vector<Platemaker::Models::OutputProfile> output(
        m_workspace.outputProfiles().begin(), m_workspace.outputProfiles().end());

    // For a library export, learn which names already live in the library so the picker can flag them
    // and we can confirm before overwriting. (Loaded once here; addToUserLibrary reloads to upsert —
    // a negligible re-read of a tiny file.)
    QStringList libraryNames;
    if (!toFile) {
        const QString libPath = userProfileLibraryPath();
        std::vector<Platemaker::Models::CanvasProfile> libC;
        std::vector<Platemaker::Models::OutputProfile> libO;
        if (QFileInfo::exists(libPath) && !loadProfilesFromFile(libPath, libC, libO))
            return;
        if (canvasKind) for (const auto& c : libC) libraryNames << QString::fromStdString(c.name);
        else            for (const auto& o : libO) libraryNames << QString::fromStdString(o.name);
    }
    const auto inLibrary = [&](const std::string& name) {
        return libraryNames.contains(QString::fromStdString(name));
    };

    const QColor kInLibraryColour(0xE0, 0x87, 0x2C); // orange
    const QString kInLibraryDetail =
        tr("A profile of this name is already in your library. Exporting it there will replace it.");
    QList<ProfilePickerDialog::Row> rows;
    if (canvasKind)
        for (const auto& cp : canvas) {
            ProfilePickerDialog::Row r = canvasRow(cp);
            if (inLibrary(cp.name))
                r.titleBadges.append({ tr("already in library"), kInLibraryColour, kInLibraryDetail });
            rows.append(r);
        }
    else
        for (const auto& op : output) {
            ProfilePickerDialog::Row r = outputRow(op);
            if (inLibrary(op.name))
                r.titleBadges.append({ tr("already in library"), kInLibraryColour, kInLibraryDetail });
            rows.append(r);
        }

    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Export"),
            canvasKind ? tr("This workspace has no canvas profiles to export.")
                       : tr("This workspace has no output profiles to export."));
        return;
    }

    ProfilePickerDialog dlg(this);
    dlg.setWindowTitle(canvasKind ? tr("Export canvas profiles") : tr("Export output profiles"));
    if (!toFile)
        dlg.setIntro(tr("Exporting to your profile library. Profiles already there are marked."));
    dlg.setConfirmText(tr("Export…"));
    dlg.setRows(rows);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QList<int> picked = dlg.checkedIndices();
    if (picked.isEmpty())
        return;

    std::vector<Platemaker::Models::CanvasProfile> chosenCanvas;
    std::vector<Platemaker::Models::OutputProfile> chosenOutput;
    if (canvasKind)
        for (int i : picked) chosenCanvas.push_back(canvas[i]);
    else
        for (int i : picked) chosenOutput.push_back(output[i]);

    // The destination was chosen by which submenu item invoked this (toFile) — no dialog needed here.
    if (!toFile) {
        // Confirm before overwriting any same-named library entries, and name them.
        QStringList overwrites;
        for (int i : picked) {
            const QString nm = canvasKind ? QString::fromStdString(canvas[i].name)
                                          : QString::fromStdString(output[i].name);
            if (libraryNames.contains(nm)) overwrites << nm;
        }
        if (!overwrites.isEmpty()) {
            const auto ret = QMessageBox::question(this, tr("Overwrite in library?"),
                tr("These %1 profile(s) already exist in your library and will be overwritten:\n\n%2\n\nContinue?")
                    .arg(overwrites.size()).arg(overwrites.join(QStringLiteral("\n"))),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
                return;
        }
        if (addToUserLibrary(chosenCanvas, chosenOutput)) {
            const int added = static_cast<int>(picked.size()) - static_cast<int>(overwrites.size());
            QMessageBox::information(this, tr("Export"),
                overwrites.isEmpty()
                    ? tr("Added %1 profile(s) to your library.").arg(picked.size())
                    : tr("Library updated: %1 new, %2 overwritten.").arg(added).arg(overwrites.size()));
        }
        return;
    }

    const QString suggested = QDir(defaultDialogDir())
        .filePath(QStringLiteral("profiles.platemaker.profiles.json"));
    const QString out = QFileDialog::getSaveFileName(
        this, tr("Export profiles to"), suggested,
        tr("Platemaker profile bundle (*.platemaker.profiles.json)"));
    if (out.isEmpty())
        return;

    try {
        Platemaker::Infrastructure::ProfileBundleSerializer{}.save(
            chosenCanvas, chosenOutput, out.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export"),
            tr("Could not write the bundle:\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(out), QString::fromUtf8(e.what())));
        return;
    }
    addToRecentBundles(out); // surfaces under "Recent bundles" for future imports
    QMessageBox::information(this, tr("Export"),
        tr("Exported %1 profile(s) to:\n%2").arg(picked.size()).arg(QDir::toNativeSeparators(out)));
}

// ---------------------------------------------------------------------------
// Bubble-preset packs (Tools) — the application's presets, not the workspace's
// ---------------------------------------------------------------------------

void MainWindow::onImportBubblePresets()
{
    StripEdit::importPresetPack(this, *m_presets);
}

void MainWindow::onExportBubblePresets()
{
    StripEdit::exportPresetPack(this, *m_presets);
}
