#include "tooloptionspanel.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include "presetstore.h"
#include "shapeeditor.h"
#include "skineditor.h"
#include "styleeditor.h"
#include "tailseditor.h"
#include "texteditor.h"

namespace StripEdit {

ToolOptionsPanel::ToolOptionsPanel(PresetStore& presets, QWidget* parent)
    : QWidget(parent)
    , m_presets(presets)
    , m_groups(this)
{
    auto* lay = new QVBoxLayout(this);

    // --- Presets ------------------------------------------------------------------------------------
    // Above the controls it fills, and outside the Shape group, because a preset also carries the font
    // and the colours the Text tool uses.
    m_presetCombo = new QComboBox(this);
    // **The picker offers; it never claims.** A drop-down showing an entry says *this is what is
    // selected*, and a preset is not a selection — it is a one-shot fill that stops describing anything
    // the moment any control moves. So it shows a prompt, and the chip beside it reports what the next
    // object's values actually are.
    m_presetCombo->setPlaceholderText(tr("Apply a preset…"));
    m_presetCombo->setToolTip(tr("Fills the controls below with a saved look. What the next object "
                                 "will be is what the controls say, and the chip reports it."));
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_presetCombo->setIconSize(QSize(k_bubbleThumbW, k_bubbleThumbH));

    auto* presetSave = new QToolButton(this);
    presetSave->setText(tr("Save…"));
    presetSave->setToolTip(tr("Saves the current look — shape, colours, line style, font — under a name."));

    auto* presetMore = new QToolButton(this);
    presetMore->setText(QStringLiteral("⋯"));
    presetMore->setPopupMode(QToolButton::InstantPopup);
    auto* presetMenu = new QMenu(presetMore);
    m_presetDelete     = presetMenu->addAction(tr("Delete preset"));
    presetMenu->addSeparator();
    QAction* importAct = presetMenu->addAction(tr("Import pack…"));
    QAction* exportAct = presetMenu->addAction(tr("Export pack…"));
    presetMore->setMenu(presetMenu);

    auto* presetRow = new QHBoxLayout;

    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(presetSave);
    presetRow->addWidget(presetMore);
    lay->addLayout(presetRow);

    // --- Shape (Bubble tool only) -------------------------------------------------------------------
    m_shapeGroup = new QGroupBox(tr("Shape"), this);
    auto* shapeLay = new QVBoxLayout(m_shapeGroup);
    shapeLay->addWidget(m_groups.shape());
    m_tails = new TailsEditor(this);
    shapeLay->addWidget(m_tails);
    shapeLay->addWidget(m_groups.skin());
    shapeLay->addWidget(m_groups.style());
    lay->addWidget(m_shapeGroup);

    // --- Text (both tools) --------------------------------------------------------------------------
    m_textGroup = new QGroupBox(tr("Text"), this);
    auto* textLay = new QVBoxLayout(m_textGroup);
    textLay->addWidget(m_groups.text());
    lay->addWidget(m_textGroup);
    lay->addStretch(1);

    // Nothing here acts on an object, because there is no object yet: no line to type.
    m_groups.text()->setContentVisible(false);

    // --- Wiring -------------------------------------------------------------------------------------
    // The one cross-group rule, connected first so it runs first: picking a shape gives you the shape
    // its tile shows, tail and all, and the tails editor has to hear about it before the change is
    // collected. Qt runs slots in connection order, which is the whole reason this line is up here.
    connect(m_groups.shape(), &ShapeEditor::edited, this, [this] {
        m_tails->shapeChanged(m_groups.shape()->values().kind);
    });
    for (PropertyGroupEditor* e : m_groups.all())
        connect(e, &PropertyGroupEditor::edited, this, [this] { onControlChanged(); });
    connect(m_tails, &PropertyGroupEditor::edited, this, [this] { onControlChanged(); });

    // activated(), not currentIndexChanged(): only a human picking an entry applies a preset, so
    // rebuilding the list never restyles anything, and re-picking the current entry re-applies it.
    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) { applyPreset(i); });
    connect(presetSave,     &QToolButton::clicked, this, [this] { onSavePreset(); });
    connect(m_presetDelete, &QAction::triggered,   this, [this] { onDeletePreset(); });
    connect(importAct,      &QAction::triggered,   this, [this] { onImportPack(); });
    connect(exportAct,      &QAction::triggered,   this, [this] { onExportPack(); });
    connect(&m_presets, &PresetStore::changed, this, [this] { refreshPresetCombo(); });

    refreshPresetCombo();
    syncFromModel();
}

TextArtifact::Shape ToolOptionsPanel::balloonShape() const
{
    TextArtifact picked;
    m_groups.shape()->applyTo(picked);
    return picked.hasSilhouette() ? picked.shape.kind : TextArtifact::Shape::Speech;
}

void ToolOptionsPanel::setToolShape(std::optional<TextArtifact::Shape> shape)
{
    m_toolShape = shape;
    m_shapeGroup->setVisible(!shape.has_value());
}

TextArtifact ToolOptionsPanel::prototype() const
{
    TextArtifact a;
    // Shape first: the tails editor reads it, because a shapeless artifact has nothing to grow a tail
    // from. Everything else is order-independent by construction — no two groups touch a property.
    // The tool places its own kind if it has one; otherwise the tiles' balloon, which is never None.
    a.shape.kind = m_toolShape ? *m_toolShape : balloonShape();
    m_groups.skin()->applyTo(a);
    m_groups.style()->applyTo(a);
    m_groups.text()->applyTo(a);

    // A placement takes the look and not the line. The same distinction a preset makes — everything a
    // balloon is, minus everything it says — and the same reason: the words belong to one balloon.
    a.text.body.clear();
    // …and to one balloon's aim: applyToNew() makes a first tail rather than copying anyone else's.
    m_tails->applyToNew(a);

    // A fresh seed per bubble, so a page of marker balloons does not wear one repeated wobble. It
    // belongs to no group precisely so that no editor and no preset can copy it.
    a.styleSeed = QRandomGenerator::global()->generate();
    return a;
}

// ---------------------------------------------------------------------------

void ToolOptionsPanel::onControlChanged()
{
    if (m_populating)
        return;
    m_groups.collect(m_artifact, m_tails);
    refreshLook();   // one changed property and it is no longer that preset (Q40)
    // Nothing is emitted: these values describe an object that does not exist yet, so there is nothing
    // to preview and nothing to persist until one is placed.
}

void ToolOptionsPanel::syncFromModel()
{
    m_populating = true;
    m_groups.bind(m_artifact);
    m_tails->bindOne(m_artifact);
    m_populating = false;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

void ToolOptionsPanel::refreshPresetCombo()
{
    const QSignalBlocker block(m_presetCombo);
    m_presetCombo->clear();
    const QList<BubblePreset>& presets = m_presets.presets();
    for (const BubblePreset& p : presets) {
        // No separator row between built-ins and the artist's own: a separator is an entry, and every
        // index here doubles as an index into the store.
        m_presetCombo->addItem(QIcon(bubbleThumbnail(p.artifact.shape.kind, p.artifact.skin.fill,
                                                     p.artifact.skin.stroke, p.artifact.text.colour)),
                               p.name);
    }
    // No current entry, ever: see the placeholder above. Which preset the other two act on is the one
    // the controls currently *are*, which is what the chip reports — see refreshLook().
    m_presetCombo->setCurrentIndex(-1);
    refreshLook();
}

void ToolOptionsPanel::refreshLook()
{
    // **Delete acts on the look, not on a picker's selection.** With nothing claimed above, the honest
    // subject is the preset these values *are*, and only if it is the artist's own: a built-in cannot be
    // removed, and a look that is nobody's preset is not a preset to remove.
    m_presetDelete->setEnabled(m_presets.isCustom(m_presets.matching(m_artifact)));
}

void ToolOptionsPanel::applyPreset(int index)
{
    m_presetDelete->setEnabled(m_presets.isCustom(index));
    m_presetCombo->setCurrentIndex(-1);   // it applied a look; it is not now *showing* one
    if (index < 0 || index >= m_presets.presets().size())
        return;
    m_artifact = PresetStore::applied(m_presets.presets().at(index), m_artifact,
                                      /*keepShape=*/m_toolShape.has_value());
    syncFromModel();
}

void ToolOptionsPanel::onSavePreset()
{
    bool ok = false;
    // Offered back: the name these values already carry, so saving over a preset needs no retyping,
    // and *Custom* offers nothing rather than a word nobody meant as a name.
    const int     match   = m_presets.matching(m_artifact);
    const QString suggest = match >= 0 ? m_presets.presets().at(match).name : QString();
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"),
                                               QLineEdit::Normal, suggest, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;

    // What is saved is what the *controls* say: prototype() is already exactly "the panel as an
    // artifact", minus the content a preset never carries.
    int existing = -1;
    int at = m_presets.save(name, prototype(), /*replaceExisting=*/false, &existing);
    if (at < 0) {
        if (QMessageBox::question(this, tr("Save preset"),
                                  tr("A preset named “%1” already exists. Replace it?").arg(name))
            != QMessageBox::Yes)
            return;
        at = m_presets.save(name, prototype(), /*replaceExisting=*/true);
    }
    (void)at;
    refreshPresetCombo();
}

void ToolOptionsPanel::onDeletePreset()
{
    const int i = m_presets.matching(m_artifact);   // the one the chip names, and it is the artist's own
    if (!m_presets.isCustom(i))
        return;
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete the preset “%1”?").arg(m_presets.presets().at(i).name))
        != QMessageBox::Yes)
        return;
    m_presets.remove(i);
    refreshPresetCombo();   // the values stay as they are; they are simply nobody's preset now
}

void ToolOptionsPanel::onImportPack()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import preset pack"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Bubble preset packs (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    const int n = m_presets.importPack(path, &error);
    if (n < 0) {
        QMessageBox::warning(this, tr("Import preset pack"), error);
        return;
    }
    refreshPresetCombo();
    QMessageBox::information(this, tr("Import preset pack"),
                             tr("Imported %n preset(s).", nullptr, n));
}

void ToolOptionsPanel::onExportPack()
{
    if (m_presets.presets().size() <= m_presets.builtinCount()) {
        QMessageBox::information(this, tr("Export preset pack"),
                                 tr("There are no saved presets to export yet."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export preset pack"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/bubble-presets.json"),
        tr("Bubble preset packs (*.json)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");

    QString error;
    if (!m_presets.exportPack(path, &error))
        QMessageBox::warning(this, tr("Export preset pack"), error);
}

}  // namespace StripEdit
