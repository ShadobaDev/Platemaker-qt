#include "bubblepanel.h"

#include "properties/shapeeditor.h"
#include "properties/skineditor.h"
#include "properties/styleeditor.h"
#include "properties/tailseditor.h"
#include "properties/texteditor.h"
#include "artifactpainter.h"
#include "ui_bubblepanel.h"
#include "flowlayout.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QEvent>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace StripEdit {

namespace {
// --- presets ---------------------------------------------------------------

const auto k_presetsKey = QStringLiteral("bubblePresets");
//! Marks a file as a pack rather than any other JSON array that happens to parse.
const auto k_packMarker = QStringLiteral("platemakerBubblePresets");

/**
 * @brief A preset is an artifact with its content removed.
 *
 * Dropping the keys rather than listing the ones to keep is what makes this stay correct: a styling
 * field added to TextArtifact is carried by artifactToJson() and lands in presets for free, while a new
 * *content* field is the only thing that needs a line here.
 */
QJsonObject presetToJson(const BubblePreset& p)
{
    QJsonObject j = artifactToJson(p.artifact);
    for (const QString& key : {QStringLiteral("text"), QStringLiteral("w"), QStringLiteral("h"),
                               QStringLiteral("tails"), QStringLiteral("styleSeed")})
        j.remove(key);
    j.insert(QStringLiteral("name"), p.name);
    return j;
}

//! The absent content keys fall back to TextArtifact's own defaults, which is exactly what is wanted.
BubblePreset presetFromJson(const QJsonObject& j)
{
    return {j.value(QStringLiteral("name")).toString(), artifactFromJson(j)};
}

QJsonArray presetsToArray(const QList<BubblePreset>& presets, int from)
{
    QJsonArray arr;
    for (int i = from; i < presets.size(); ++i)
        arr.append(presetToJson(presets.at(i)));
    return arr;
}

QList<BubblePreset> presetsFromArray(const QJsonArray& arr)
{
    QList<BubblePreset> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        BubblePreset p = presetFromJson(v.toObject());
        if (!p.name.isEmpty())
            out.append(std::move(p));   // an unnamed preset has nothing to pick it by
    }
    return out;
}

/**
 * @brief The looks a lettering session starts from, as code rather than as a shipped file.
 *
 * They are parametric — a shape name and a handful of numbers — so there is nothing to install, nothing
 * to find at runtime and nothing to lose. They are also not deletable: "restore defaults" is a feature
 * that does not need writing if the defaults were never removable in the first place.
 */
QList<BubblePreset> builtinPresets()
{
    QList<BubblePreset> out;

    TextArtifact dialogue;                       // the struct's own defaults are already a speech balloon
    out.append({BubblePanel::tr("Dialogue"), dialogue});

    TextArtifact whisper = dialogue;
    whisper.shape.kind         = TextArtifact::Shape::Ellipse;
    whisper.skin.strokeWidth = 3;
    whisper.skin.stroke      = QColor(90, 90, 90);
    whisper.text.colour    = QColor(70, 70, 70);
    whisper.text.pixelSize = 26;
    out.append({BubblePanel::tr("Whisper"), whisper});

    TextArtifact thought = dialogue;
    thought.shape.kind       = TextArtifact::Shape::Thought;
    thought.skin.strokeWidth = 4;
    out.append({BubblePanel::tr("Thought"), thought});

    TextArtifact shout = dialogue;
    shout.shape.kind         = TextArtifact::Shape::Shout;
    shout.text.bold          = true;
    shout.text.pixelSize = 38;
    shout.skin.strokeWidth   = 7;
    shout.style.kind         = TextArtifact::Style::Marker;
    out.append({BubblePanel::tr("Shout"), shout});

    TextArtifact caption = dialogue;
    caption.shape.kind       = TextArtifact::Shape::Caption;
    caption.skin.fill        = QColor(16, 16, 16);
    caption.text.colour       = QColor(245, 245, 245);
    caption.skin.stroke      = QColor(245, 245, 245);
    caption.skin.strokeWidth = 2;
    caption.text.align       = Qt::AlignLeft;
    out.append({BubblePanel::tr("Caption"), caption});

    return out;
}
}

BubblePanel::BubblePanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::BubblePanel)
    , m_groups(this)
{
    ui->setupUi(this);  // provides the empty verticalLayout container; the controls are built here

    // --- Presets ---------------------------------------------------------------------------------
    // Above the controls it sets, and outside the Shape group, because a preset also carries the font
    // and the colours the Text tool uses.
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setToolTip(tr("The look the next bubble you place will start from."));
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
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(presetSave);
    presetRow->addWidget(presetMore);

    loadPresets();
    refreshPresetCombo(0);

    // --- Shape (Bubble tool only) ---------------------------------------------------------------
    //
    // Four groups, each bringing its own controls and owning its own properties. The order is the one
    // they had as loose widgets: the shape, then its tails, then its surface, then how the line is
    // drawn. This panel neither reads nor writes any of it.
    m_shapeGroup = new QGroupBox(tr("Shape"), this);
    auto* shapeLay = new QVBoxLayout(m_shapeGroup);

    shapeLay->addWidget(m_groups.shape());
    shapeLay->addWidget(m_groups.tails());
    shapeLay->addWidget(m_groups.skin());
    shapeLay->addWidget(m_groups.style());

    // --- Text (both tools) ----------------------------------------------------------------------
    m_textGroup = new QGroupBox(tr("Text"), this);
    auto* textLay = new QVBoxLayout(m_textGroup);
    textLay->addWidget(m_groups.text());

    // Nothing here acts on an object, because there is no object yet: no text to type, no tail to add
    // to, nothing to fit or delete. Those live with the selection, in ObjectStatePanel.
    m_groups.text()->setContentVisible(false);
    m_groups.tails()->setAddVisible(false);

    auto* lay = qobject_cast<QVBoxLayout*>(layout());
    if (lay) {
        lay->addLayout(presetRow);
        lay->addWidget(m_shapeGroup);
        lay->addWidget(m_textGroup);
        lay->addStretch(1);
    }

    // --- Wiring ---------------------------------------------------------------------------------
    //
    // The one cross-group rule, connected first so it runs first: picking a shape gives you the shape
    // its tile shows, tail and all, and the tails editor has to hear about it before the change is
    // collected. Qt runs slots in connection order, which is the whole reason this line is up here.
    connect(m_groups.shape(), &ShapeEditor::edited, this, [this] {
        m_groups.tails()->shapeChanged(m_groups.shape()->values().kind);
    });

    for (PropertyGroupEditor* e : m_groups.all())
        connect(e, &PropertyGroupEditor::edited, this, [this] { onControlChanged(); });

    // activated(), not currentIndexChanged(): only a human picking an entry applies a preset, so
    // rebuilding the list never restyles anything, and re-picking the current entry re-applies it.
    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) {
        m_presetDelete->setEnabled(currentPresetIsCustom());
        if (i >= 0 && i < m_presets.size())
            applyPreset(m_presets.at(i));
    });
    connect(presetSave,     &QToolButton::clicked, this, [this] { onSavePreset(); });
    connect(m_presetDelete, &QAction::triggered,   this, [this] { onDeletePreset(); });
    connect(importAct,      &QAction::triggered,   this, [this] { onImportPack(); });
    connect(exportAct,      &QAction::triggered,   this, [this] { onExportPack(); });

    syncFromModel();
}

BubblePanel::~BubblePanel()
{
    delete ui;
}

// ---------------------------------------------------------------------------

void BubblePanel::setShapeControlsVisible(bool visible)
{
    m_shapeVisible = visible;
    m_shapeGroup->setVisible(visible);
}

TextArtifact BubblePanel::prototype() const
{
    TextArtifact a;
    // Shape first: the tails editor reads it, because a shapeless artifact has nothing to grow a tail
    // from. Everything else is order-independent by construction — no two groups touch a property.
    m_groups.shape()->applyTo(a);
    m_groups.skin()->applyTo(a);
    m_groups.style()->applyTo(a);
    m_groups.text()->applyTo(a);

    // A placement takes the look and not the line. The same distinction a preset makes — everything a
    // balloon is, minus everything it says — and the same reason: the words belong to one balloon.
    a.text.body.clear();
    // …and to one balloon's aim: applyToNew() makes a first tail rather than copying the selected
    // balloon's, which would give every new bubble the last one's tail.
    m_groups.tails()->applyToNew(a);

    // A fresh seed per bubble, so a page of marker balloons does not wear one repeated wobble. Set at
    // placement and then left alone — re-rolling it on every edit would make the outline crawl as you
    // type. It belongs to no group precisely so that no editor and no preset can copy it.
    a.styleSeed = QRandomGenerator::global()->generate();
    return a;
}

// ---------------------------------------------------------------------------

void BubblePanel::onControlChanged()
{
    if (m_populating)
        return;

    // Five calls, in place of thirteen properties read out of thirteen widgets by hand. Shape goes
    // first because the tails editor reads it; the rest cannot collide, since no property has two
    // owners.
    m_groups.collect(m_artifact);

    // Nothing is emitted: these values describe an object that does not exist yet, so there is nothing
    // to preview and nothing to persist until one is placed.
}

void BubblePanel::syncFromModel()
{
    // No signal blockers and no populating dance here any more: bind() never emits, by contract.
    m_populating = true;
    m_groups.bind(m_artifact);
    m_populating = false;
}

void BubblePanel::loadPresets()
{
    m_presets      = builtinPresets();
    m_builtinCount = int(m_presets.size());
    const QJsonDocument doc =
        QJsonDocument::fromJson(QSettings().value(k_presetsKey).toString().toUtf8());
    m_presets += presetsFromArray(doc.array());
}

void BubblePanel::savePresets()
{
    // Only the artist's own: the built-ins are code, so storing them would freeze today's version of
    // them into the config and quietly outvote any later improvement.
    QSettings().setValue(k_presetsKey,
                         QString::fromUtf8(QJsonDocument(presetsToArray(m_presets, m_builtinCount))
                                               .toJson(QJsonDocument::Compact)));
}

void BubblePanel::refreshPresetCombo(int current)
{
    QSignalBlocker block(m_presetCombo);
    m_presetCombo->clear();
    for (int i = 0; i < m_presets.size(); ++i) {
        const BubblePreset& p = m_presets.at(i);
        // No separator row between built-ins and the artist's own: a separator is an entry, and every
        // index here doubles as an index into m_presets.
        m_presetCombo->addItem(QIcon(bubbleThumbnail(p.artifact.shape.kind, p.artifact.skin.fill,
                                                     p.artifact.skin.stroke, p.artifact.text.colour)),
                               p.name);
    }
    m_presetCombo->setCurrentIndex(qBound(-1, current, int(m_presets.size()) - 1));
    m_presetDelete->setEnabled(currentPresetIsCustom());
}

bool BubblePanel::currentPresetIsCustom() const
{
    const int i = m_presetCombo->currentIndex();
    return i >= m_builtinCount && i < m_presets.size();
}

void BubblePanel::applyPreset(const BubblePreset& p)
{
    TextArtifact a = p.artifact;
    // A preset is a look, not a line: whatever the bubble says, how big it is and where its tails point
    // survive being restyled. Without this, picking a preset would erase the lettering.
    a.text.body  = m_artifact.text.body;
    a.box   = m_artifact.box;
    a.tails.items = m_artifact.tails.items;
    // With the Text tool the shape picker is hidden, so a preset must not change a shape the author
    // cannot see, and cannot put back.
    if (!m_shapeVisible)
        a.shape.kind = m_artifact.shape.kind;
    if (a.shape.kind == TextArtifact::Shape::None)
        a.tails.items.clear();
    // Keep the bubble's own seed. Re-rolling it would make an already-placed marker outline jump for a
    // reason the author did not ask for.
    a.styleSeed = m_artifact.styleSeed;
    if (a.style.kind != TextArtifact::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();

    m_artifact = a;
    syncFromModel();
}

void BubblePanel::onSavePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"),
                                               QLineEdit::Normal, m_presetCombo->currentText(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;

    // What is saved is what the *controls* say, not what the selection is: with nothing selected the
    // panel is still a valid look, and prototype() is already exactly "the panel as an artifact".
    BubblePreset p{name, prototype()};
    p.artifact.tails.items.clear();
    p.artifact.styleSeed = 0;   // content, not style: a stored seed would clone one bubble's wobble

    for (int i = m_builtinCount; i < m_presets.size(); ++i) {
        if (m_presets.at(i).name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (QMessageBox::question(this, tr("Save preset"),
                                  tr("A preset named “%1” already exists. Replace it?").arg(name))
            != QMessageBox::Yes)
            return;
        m_presets[i] = p;
        savePresets();
        refreshPresetCombo(i);
        return;
    }
    // A built-in is not replaceable, so saving over its name keeps both: the artist's own wins by
    // sitting below it, and the built-in is still there to go back to.
    m_presets.append(p);
    savePresets();
    refreshPresetCombo(int(m_presets.size()) - 1);
}

void BubblePanel::onDeletePreset()
{
    if (!currentPresetIsCustom())
        return;
    const int i = m_presetCombo->currentIndex();
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete the preset “%1”?").arg(m_presets.at(i).name))
        != QMessageBox::Yes)
        return;
    m_presets.removeAt(i);
    savePresets();
    refreshPresetCombo(qMin(i, int(m_presets.size()) - 1));
}

void BubblePanel::onImportPack()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import preset pack"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Bubble preset packs (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import preset pack"), tr("Cannot read %1.").arg(path));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains(k_packMarker)) {
        QMessageBox::warning(this, tr("Import preset pack"),
                             tr("%1 is not a Platemaker preset pack.").arg(QFileInfo(path).fileName()));
        return;
    }

    const QList<BubblePreset> incoming =
        presetsFromArray(root.value(QStringLiteral("presets")).toArray());
    for (const BubblePreset& p : incoming) {
        // Replace by name rather than accumulate: re-importing an updated pack should update it, not
        // leave the artist choosing between two entries wearing the same name.
        int at = -1;
        for (int i = m_builtinCount; i < m_presets.size(); ++i)
            if (m_presets.at(i).name.compare(p.name, Qt::CaseInsensitive) == 0)
                at = i;
        if (at >= 0)
            m_presets[at] = p;
        else
            m_presets.append(p);
    }
    savePresets();
    refreshPresetCombo(m_presetCombo->currentIndex());
    QMessageBox::information(this, tr("Import preset pack"),
                             tr("Imported %n preset(s).", nullptr, int(incoming.size())));
}

void BubblePanel::onExportPack()
{
    if (m_presets.size() <= m_builtinCount) {
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

    const QJsonObject root{
        {k_packMarker, 1},
        {QStringLiteral("presets"), presetsToArray(m_presets, m_builtinCount)},
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        QMessageBox::warning(this, tr("Export preset pack"), tr("Cannot write %1.").arg(path));
        return;
    }
}

// ---------------------------------------------------------------------------

}  // namespace StripEdit
