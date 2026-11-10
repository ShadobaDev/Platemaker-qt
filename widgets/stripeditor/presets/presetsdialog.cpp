#include "presets/presetsdialog.hpp"
#include "ui_presetsdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabBar>
#include <QTreeWidget>

#include "presetstore.hpp"
#include "properties/effectseditor.hpp"
#include "properties/shapeeditor.hpp"   // bubbleThumbnail()
#include "properties/skineditor.hpp"
#include "properties/styleeditor.hpp"
#include "properties/texteditor.hpp"
#include "properties/textureeditor.hpp"
#include "recordpainter.hpp"
#include "shapes.hpp"

namespace StripEdit {

namespace {

constexpr int k_sampleW = 260;   //!< The drawn sample's width in the dialog, in pixels.
constexpr int k_sampleH = 170;   //!< ...and its height.

//! A preset's look as it is stored: what save() keeps of a record, so two can be compared.
ObjectRecord stored(ObjectRecord a)
{
    a.tails.items.clear();
    a.styleSeed = 0;
    a.text.body.clear();
    a.presetOrigin = {};
    return a;
}

//! @p look drawn as a balloon saying something, with a tail when its shape speaks.
QPixmap sampleOf(const ObjectRecord& look)
{
    ObjectRecord a = look;
    a.box          = QSize(k_sampleW, k_sampleH * 3 / 4);
    a.text.body    = PresetsDialog::tr("Sample");
    if (Geometry::shapeSpeaks(a.shape)) {
        Tail t;
        t.tip         = QPointF(a.box.width() * 0.3, a.box.height() * 1.25);
        t.baseWidth   = a.box.width() * 0.12;
        a.tails.items = {t};
    }
    topUpStyleSeed(a);
    return QPixmap::fromImage(Painter::render(a).scaled(QSize(k_sampleW, k_sampleH), Qt::KeepAspectRatio,
                                                        Qt::SmoothTransformation));
}

//! The form, set up on @p dialog: built in the initialiser list, so the editors can be built on its host.
Ui::PresetsDialog* formOn(QDialog* dialog)
{
    auto* form = new Ui::PresetsDialog;
    form->setupUi(dialog);
    return form;
}

}  // namespace

PresetsDialog::PresetsDialog(PresetStore& store, const QString& select, PresetUses uses, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
    , ui(formOn(this))
    , m_groups(ui->editorsHost)
    , m_uses(std::move(uses))
{
    ui->listPresets->setIconSize(QSize(k_bubbleThumbW, k_bubbleThumbH));
    // One kind of preset at a time. A tab bar, not a tab widget: the list under it is the same list.
    // Built here because Designer offers no bare tab bar; it sits at the top of the form's list column.
    m_kindTabs = new QTabBar(this);
    m_kindTabs->addTab(tr("Bubbles"));
    m_kindTabs->addTab(tr("Text"));
    ui->verticalLayoutList->insertWidget(0, m_kindTabs);

    // OBJECT STATE's editors, every row, in the order the balloon is drawn, ruled apart as TOOL OPTIONS
    // has them; the shape's parameters belong to its tiles.
    bool first = true;
    for (QWidget* part : {static_cast<QWidget*>(m_groups.shape()), m_groups.shape()->parameters(),
                          static_cast<QWidget*>(m_groups.skin()), static_cast<QWidget*>(m_groups.style()),
                          static_cast<QWidget*>(m_groups.texture()), static_cast<QWidget*>(m_groups.effects()),
                          static_cast<QWidget*>(m_groups.text())}) {
        if (!first && part != m_groups.shape()->parameters()) {
            auto* rule = new QFrame(ui->editorsHost);
            rule->setFrameShape(QFrame::HLine);
            rule->setFrameShadow(QFrame::Sunken);
            ui->verticalLayoutEditors->addWidget(rule);
            m_balloonParts << rule;   // the one above the lettering too: with nothing above it, it parts nothing
        }
        ui->verticalLayoutEditors->addWidget(part);
        if (part != m_groups.text())
            m_balloonParts << part;
        first = false;
    }
    ui->verticalLayoutEditors->addStretch(1);
    m_groups.text()->setContentVisible(false);   // a preset carries no lettering

    // --- Wiring ---------------------------------------------------------------------------------------
    for (PropertyGroupEditor* e : m_groups.all())
        connect(e, &PropertyGroupEditor::changed, this, &PresetsDialog::onEdited);
    connect(ui->listPresets, &QListWidget::currentRowChanged, this, [this](int row) {
        const QListWidgetItem* item  = ui->listPresets->item(row);
        const int              index = item ? item->data(Qt::UserRole).toInt() : -1;
        if (index == m_shown)
            return;
        if (!settleEdits()) {
            const QSignalBlocker block(ui->listPresets);
            ui->listPresets->setCurrentRow(rowOf(m_shown));   // cancelled: stay where the edits are
            return;
        }
        showPreset(index);
    });
    connect(m_kindTabs, &QTabBar::currentChanged, this, [this](int tab) {
        if (!settleEdits()) {
            const QSignalBlocker block(m_kindTabs);
            m_kindTabs->setCurrentIndex(1 - tab);   // cancelled: stay on the kind being edited
            return;
        }
        rebuildList(QString());
    });
    connect(ui->pushButtonSave, &QPushButton::clicked, this, &PresetsDialog::save);
    connect(ui->pushButtonRevert, &QPushButton::clicked, this, &PresetsDialog::revert);
    connect(ui->pushButtonDuplicate, &QPushButton::clicked, this, &PresetsDialog::duplicate);
    connect(ui->pushButtonRename, &QPushButton::clicked, this, &PresetsDialog::rename);
    connect(ui->pushButtonDelete, &QPushButton::clicked, this, &PresetsDialog::remove);
    connect(ui->pushButtonImport, &QPushButton::clicked, this, [this] {
        if (settleEdits())
            importPresetPack(this, m_store);
    });
    connect(ui->pushButtonExport, &QPushButton::clicked, this, [this] { exportPresetPack(this, m_store); });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // *Show* closes the dialog for the object: it opens behind a modal dialog otherwise.
    const auto show = [this] {
        const QTreeWidgetItem* item = ui->treeUses->currentItem();
        const QVariant         at   = item ? item->data(0, Qt::UserRole) : QVariant();
        if (!at.isValid())
            return;   // a project's row, or nothing
        m_shownUse = m_listed.at(at.toInt());
        accept();
    };
    connect(ui->pushButtonShow, &QPushButton::clicked, this, show);
    connect(ui->treeUses, &QTreeWidget::itemDoubleClicked, this, show);
    connect(ui->treeUses, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        ui->pushButtonShow->setEnabled(item && item->data(0, Qt::UserRole).isValid());
    });
    // The store changes from here and from elsewhere (an import under Tools): the list follows it.
    connect(&m_store, &PresetStore::changed, this, [this] {
        rebuildList(m_shown >= 0 && m_shown < m_store.presets().size() ? m_store.presets().at(m_shown).uid
                                                                        : QString());
    });

    rebuildList(select);
}

PresetsDialog::~PresetsDialog()
{
    delete ui;
}

void PresetsDialog::done(int result)
{
    if (settleEdits())
        QDialog::done(result);
}

void PresetsDialog::rebuildList(const QString& select)
{
    const QSignalBlocker block(ui->listPresets);
    ui->listPresets->clear();
    const QList<BubblePreset>& presets = m_store.presets();
    // The tab of the preset to open on; the tab already shown when there is none.
    if (const int wanted = m_store.indexOf(select); wanted >= 0) {
        const QSignalBlocker blockTabs(m_kindTabs);
        m_kindTabs->setCurrentIndex(isTextPreset(presets.at(wanted)) ? 1 : 0);
    }
    const bool textKind = m_kindTabs->currentIndex() == 1;
    for (QWidget* w : std::as_const(m_balloonParts))
        w->setVisible(!textKind);   // a text preset has no shape, fill or line to edit
    int        index    = -1;
    for (int i = 0; i < presets.size(); ++i) {
        const BubblePreset& p = presets.at(i);
        if (isTextPreset(p) != textKind)
            continue;
        if (index < 0 || p.uid == select)
            index = i;
        auto* item = new QListWidgetItem(
            QIcon(bubbleThumbnail(p.record.shape, p.record.skin.fill.colour, p.record.style.stroke.colour,
                                  p.record.text.fill.colour)),
            p.name, ui->listPresets);
        if (!m_store.isCustom(i)) {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
            item->setToolTip(tr("Built-in: duplicate it to make a copy you can edit."));
        }
        item->setData(Qt::UserRole, i);   // the store's index
    }
    ui->listPresets->setCurrentRow(rowOf(index));
    showPreset(index);
}

int PresetsDialog::rowOf(int index) const
{
    for (int row = 0; row < ui->listPresets->count(); ++row)
        if (ui->listPresets->item(row)->data(Qt::UserRole).toInt() == index)
            return row;
    return -1;
}

void PresetsDialog::showPreset(int index)
{
    m_shown = index;
    const bool any = index >= 0 && index < m_store.presets().size();
    m_edited = any ? m_store.presets().at(index).record : ObjectRecord();
    m_populating = true;
    m_groups.bind(m_edited);
    m_populating = false;
    ui->editorsHost->setEnabled(any && m_store.isCustom(index));
    ui->labelSample->setPixmap(any ? sampleOf(m_edited) : QPixmap());
    refreshButtons();
    listUses();
}

void PresetsDialog::listUses()
{
    ui->treeUses->clear();
    m_listed.clear();
    const bool any = m_shown >= 0 && m_shown < m_store.presets().size();
    if (any && m_uses)
        m_listed = m_uses(m_store.presets().at(m_shown).uid);
    QHash<QString, QTreeWidgetItem*> projects;
    for (int i = 0; i < m_listed.size(); ++i) {
        const PresetUse& use     = m_listed.at(i);
        QTreeWidgetItem*&project = projects[use.projectUid];
        if (!project) {
            project = new QTreeWidgetItem(ui->treeUses, {use.projectName});
            project->setExpanded(true);
        }
        auto* row = new QTreeWidgetItem(project, {use.label});
        row->setData(0, Qt::UserRole, i);
    }
    ui->labelUses->setText(m_listed.isEmpty() ? tr("No object in this workspace is made from it.")
                                              : tr("Objects made from it"));
    ui->pushButtonShow->setEnabled(false);
}

void PresetsDialog::onEdited()
{
    if (m_populating)
        return;
    m_groups.collect(m_edited);
    ui->labelSample->setPixmap(sampleOf(m_edited));
    refreshButtons();
}

void PresetsDialog::refreshButtons()
{
    const bool own = m_store.isCustom(m_shown);
    ui->pushButtonSave->setEnabled(dirty());
    ui->pushButtonRevert->setEnabled(dirty());
    ui->pushButtonDuplicate->setEnabled(m_shown >= 0 && m_shown < m_store.presets().size());
    ui->pushButtonRename->setEnabled(own);
    ui->pushButtonDelete->setEnabled(own);
}

bool PresetsDialog::dirty() const
{
    return m_store.isCustom(m_shown) && !(stored(m_edited) == stored(m_store.presets().at(m_shown).record));
}

bool PresetsDialog::settleEdits()
{
    if (!dirty())
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Manage presets"),
        tr("Save the changes to “%1”?").arg(m_store.presets().at(m_shown).name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        save();
    else
        revert();
    return true;
}

void PresetsDialog::save()
{
    if (!dirty())
        return;
    (void)m_store.save(m_store.presets().at(m_shown).name, m_edited, /*replaceExisting=*/true);
}

void PresetsDialog::revert()
{
    showPreset(m_shown);
}

void PresetsDialog::duplicate()
{
    if (!settleEdits() || m_shown < 0 || m_shown >= m_store.presets().size())
        return;
    const BubblePreset source = m_store.presets().at(m_shown);
    QString name = tr("%1 copy").arg(source.name);
    int     at   = m_store.save(name, source.record, /*replaceExisting=*/false);
    for (int n = 2; at < 0; ++n) {
        name = tr("%1 copy %2").arg(source.name).arg(n);
        at   = m_store.save(name, source.record, /*replaceExisting=*/false);
    }
    rebuildList(m_store.presets().at(at).uid);
}

void PresetsDialog::rename()
{
    if (!m_store.isCustom(m_shown) || !settleEdits())
        return;
    const QString was = m_store.presets().at(m_shown).name;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename preset"), tr("Preset name:"), QLineEdit::Normal,
                                               was, &ok).trimmed();
    if (!ok || name.isEmpty() || name == was)
        return;
    if (!m_store.rename(m_shown, name))
        QMessageBox::information(this, tr("Rename preset"),
                                 tr("You already have a preset named “%1”.").arg(name));
}

void PresetsDialog::remove()
{
    if (!m_store.isCustom(m_shown))
        return;
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete the preset “%1”? Objects made from it keep their look.")
                                  .arg(m_store.presets().at(m_shown).name))
        != QMessageBox::Yes)
        return;
    m_edited = m_store.presets().at(m_shown).record;   // nothing left unsaved to ask about
    m_store.remove(m_shown);
}

// ---------------------------------------------------------------------------
// Packs
// ---------------------------------------------------------------------------

void importPresetPack(QWidget* parent, PresetStore& store)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, PresetsDialog::tr("Import bubble presets"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        PresetsDialog::tr("Bubble preset packs (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    const int n = store.importPack(path, &error);
    if (n < 0) {
        QMessageBox::warning(parent, PresetsDialog::tr("Import bubble presets"), error);
        return;
    }
    // Every picker follows the store's changed() by itself.
    QMessageBox::information(parent, PresetsDialog::tr("Import bubble presets"),
                             PresetsDialog::tr("Imported %n preset(s).", nullptr, n));
}

void exportPresetPack(QWidget* parent, const PresetStore& store)
{
    if (store.presets().size() <= store.builtinCount()) {
        QMessageBox::information(parent, PresetsDialog::tr("Export bubble presets"),
                                 PresetsDialog::tr("There are no saved presets to export yet."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        parent, PresetsDialog::tr("Export bubble presets"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/bubble-presets.json"),
        PresetsDialog::tr("Bubble preset packs (*.json)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");

    QString error;
    if (!store.exportPack(path, &error))
        QMessageBox::warning(parent, PresetsDialog::tr("Export bubble presets"), error);
}

}  // namespace StripEdit
