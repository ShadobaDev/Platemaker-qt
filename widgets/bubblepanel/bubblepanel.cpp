#include "bubblepanel.h"
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

namespace {
constexpr int k_commitDebounceMs = 300; //!< Coalesce typing into one undo step this long after it stops.
constexpr int k_swatchPx         = 16;  //!< Colour chip drawn on a swatch button.
//! Shape tile, matching the editor's tool rail so the two grids read as one family.
constexpr int k_shapeTilePx      = 44;
constexpr int k_shapeIconW       = 34;
constexpr int k_shapeIconH       = 26;
//! Supersampling factor for a tile's preview — rendered big, scaled down, so the stroke stays smooth.
constexpr int k_shapeIconScale   = 4;

//! Whether a shape normally speaks. Shared by the tile previews and by picking one, so a tile cannot
//! promise a shape that placing it does not give you.
bool shapeSpeaks(TextArtifact::Shape shape)
{
    // Someone is talking: a tail belongs. A caption, a banner or a scroll is narration — it has no
    // speaker to point at, so placing one should not sprout a tail the author then has to turn off.
    switch (shape) {
    case TextArtifact::Shape::Speech:
    case TextArtifact::Shape::Shout:
    case TextArtifact::Shape::Ellipse:
    case TextArtifact::Shape::Thought:
        return true;
    default:
        return false;
    }
}

/**
 * @brief A miniature of \p shape, drawn by the very rasteriser that draws the real bubble.
 *
 * Reusing renderArtifact() means a tile cannot misrepresent its shape, and it costs no icon assets: the
 * picker is generated, not drawn by hand. The caller chooses the colours, because a shape tile is UI
 * chrome (it wears the palette) while a preset's tile is a swatch of the preset itself.
 */
QPixmap bubbleThumbnail(TextArtifact::Shape shape, const QColor& fill, const QColor& stroke,
                        const QColor& ink)
{
    TextArtifact a;
    a.shape         = shape;
    a.box           = QSize(k_shapeIconW * k_shapeIconScale, k_shapeIconH * k_shapeIconScale);
    // The tile's own stroke, not the artifact's: a preset authored at 5 px on a 280 px balloon would be
    // a hairline here, and the icon is meant to say *which shape and what colours*, not how heavy.
    a.strokeWidth   = 2 * k_shapeIconScale;
    a.text          = QStringLiteral("Aa");
    a.fontPixelSize = a.box.height() / 3;
    a.fill          = fill;
    a.stroke        = stroke;
    a.textColour    = ink;
    if (shapeSpeaks(shape)) {
        Tail t;
        // A short tail: the tile is scaled to fit, so a long one would shrink the balloon itself and
        // leave the speaking shapes visibly smaller than the rest of the grid.
        t.tip       = QPointF(a.box.width() * 0.28, a.box.height() * 1.10);
        t.baseWidth = a.box.width() * 0.18;
        a.tails     = {t};
    }

    return QPixmap::fromImage(renderArtifact(a).scaled(QSize(k_shapeIconW, k_shapeIconH),
                                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

//! The shape picker's tiles: UI chrome, so they wear the palette rather than three white blobs.
QPixmap shapeThumbnail(TextArtifact::Shape shape, const QPalette& pal)
{
    return bubbleThumbnail(shape, pal.color(QPalette::Base), pal.color(QPalette::WindowText),
                           pal.color(QPalette::WindowText));
}

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
    whisper.shape         = TextArtifact::Shape::Ellipse;
    whisper.strokeWidth   = 3;
    whisper.stroke        = QColor(90, 90, 90);
    whisper.textColour    = QColor(70, 70, 70);
    whisper.fontPixelSize = 26;
    out.append({BubblePanel::tr("Whisper"), whisper});

    TextArtifact thought = dialogue;
    thought.shape       = TextArtifact::Shape::Thought;
    thought.strokeWidth = 4;
    out.append({BubblePanel::tr("Thought"), thought});

    TextArtifact shout = dialogue;
    shout.shape         = TextArtifact::Shape::Shout;
    shout.bold          = true;
    shout.fontPixelSize = 38;
    shout.strokeWidth   = 7;
    shout.style         = TextArtifact::Style::Marker;
    out.append({BubblePanel::tr("Shout"), shout});

    TextArtifact caption = dialogue;
    caption.shape       = TextArtifact::Shape::Caption;
    caption.fill        = QColor(16, 16, 16);
    caption.textColour  = QColor(245, 245, 245);
    caption.stroke      = QColor(245, 245, 245);
    caption.strokeWidth = 2;
    caption.align       = Qt::AlignLeft;
    out.append({BubblePanel::tr("Caption"), caption});

    return out;
}
}

BubblePanel::BubblePanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::BubblePanel)
{
    ui->setupUi(this);  // provides the empty verticalLayout container; the controls are built here

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] {
        if (m_hasSelection) emit committed(m_artifact);
    });

    // --- Presets ---------------------------------------------------------------------------------
    // Above the controls it sets, and outside the Shape group, because a preset also carries the font
    // and the colours the Text tool uses.
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setToolTip(tr("Restyles the selection, and starts the next bubble you place."));
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_presetCombo->setIconSize(QSize(k_shapeIconW, k_shapeIconH));

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
    m_shapeGroup = new QGroupBox(tr("Shape"), this);
    auto* shapeLay = new QVBoxLayout(m_shapeGroup);

    // The shape picker: a reflowing grid of preview tiles, built the same way the editor's tool rail is
    // (a flow layout cannot be expressed in a .ui). Each tile's icon comes from the rasteriser, so it
    // shows the shape rather than naming it.
    auto* tileHost = new QWidget(m_shapeGroup);
    auto* tileLay  = new FlowLayout(tileHost, 0, 4, 4);
    m_shapeTiles   = new QButtonGroup(this);
    m_shapeTiles->setExclusive(true);
    const auto addShapeTile = [&](TextArtifact::Shape shape, const QString& tip) {
        auto* b = new QToolButton(tileHost);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setIconSize(QSize(k_shapeIconW, k_shapeIconH));
        b->setFixedSize(k_shapeTilePx, k_shapeTilePx);
        tileLay->addWidget(b);
        m_shapeTiles->addButton(b, int(shape));
    };
    addShapeTile(TextArtifact::Shape::Speech,    tr("Speech balloon"));
    addShapeTile(TextArtifact::Shape::Ellipse,   tr("Round balloon"));
    addShapeTile(TextArtifact::Shape::Thought,   tr("Thought balloon"));
    addShapeTile(TextArtifact::Shape::Shout,     tr("Shout"));
    addShapeTile(TextArtifact::Shape::Caption,   tr("Caption box"));
    addShapeTile(TextArtifact::Shape::Trapezoid, tr("Caption plate"));
    addShapeTile(TextArtifact::Shape::Diamond,   tr("Diamond"));
    addShapeTile(TextArtifact::Shape::Banner,    tr("Banner"));
    addShapeTile(TextArtifact::Shape::Scroll,    tr("Scroll"));
    addShapeTile(TextArtifact::Shape::None,      tr("Text only — no balloon"));
    if (auto* first = m_shapeTiles->button(int(TextArtifact::Shape::Speech)))
        first->setChecked(true);
    refreshShapeTiles();
    shapeLay->addWidget(tileHost);

    auto* shapeForm = new QFormLayout;
    shapeLay->addLayout(shapeForm);

    m_tailCheck = new QCheckBox(tr("Tail"), m_shapeGroup);
    m_tailCheck->setToolTip(tr("Drag the round handle on the bubble to aim it — including outside it."));
    shapeForm->addRow(QString(), m_tailCheck);

    // Aiming is a drag on the strip; these are the two things a drag cannot say.
    m_tailWidth = new QSpinBox(m_shapeGroup);
    m_tailWidth->setRange(4, 400);
    m_tailWidth->setSuffix(tr(" px"));
    m_tailWidth->setToolTip(tr("How wide the tail is where it leaves the bubble."));
    shapeForm->addRow(tr("Tail width:"), m_tailWidth);

    m_tailBend = new QSpinBox(m_shapeGroup);
    m_tailBend->setRange(-100, 100);
    m_tailBend->setSuffix(tr(" %"));
    m_tailBend->setToolTip(tr("Curves the tail sideways. 0 is straight."));
    shapeForm->addRow(tr("Tail bend:"), m_tailBend);

    m_addTail = new QPushButton(tr("Add another tail"), m_shapeGroup);
    m_addTail->setToolTip(tr("For a sound with more than one source. Drag each handle to aim it."));
    shapeForm->addRow(QString(), m_addTail);

    m_fillSwatch   = new QPushButton(tr("Fill"),   m_shapeGroup);
    m_strokeSwatch = new QPushButton(tr("Stroke"), m_shapeGroup);
    auto* colourRow = new QHBoxLayout;
    colourRow->addWidget(m_fillSwatch);
    colourRow->addWidget(m_strokeSwatch);
    shapeForm->addRow(tr("Colours"), colourRow);

    m_styleCombo = new QComboBox(m_shapeGroup);
    m_styleCombo->addItem(tr("Clean"),  int(TextArtifact::Style::Clean));
    m_styleCombo->addItem(tr("Marker"), int(TextArtifact::Style::Marker));
    m_styleCombo->addItem(tr("Ink"),    int(TextArtifact::Style::Ink));
    m_styleCombo->setToolTip(tr("Roughens the outline as it is rendered. Shown here exactly as it will "
                                "be baked, because the library draws it."));
    shapeForm->addRow(tr("Line style:"), m_styleCombo);

    m_styleAmount = new QSpinBox(m_shapeGroup);
    m_styleAmount->setRange(0, 200);
    m_styleAmount->setSuffix(tr(" %"));
    m_styleAmount->setToolTip(tr("How strong the line style is. 100% is the preset's own strength."));
    shapeForm->addRow(tr("Style amount:"), m_styleAmount);

    m_strokeWidth = new QSpinBox(m_shapeGroup);
    m_strokeWidth->setRange(0, 40);
    m_strokeWidth->setSuffix(tr(" px"));
    shapeForm->addRow(tr("Stroke width"), m_strokeWidth);

    // --- Text (both tools) ----------------------------------------------------------------------
    m_textGroup = new QGroupBox(tr("Text"), this);
    auto* textLay = new QVBoxLayout(m_textGroup);

    m_textEdit = new QPlainTextEdit(m_textGroup);
    m_textEdit->setPlaceholderText(tr("Type the line…"));
    m_textEdit->setMinimumHeight(70);
    textLay->addWidget(m_textEdit);

    auto* textForm = new QFormLayout;
    m_fontCombo = new QFontComboBox(m_textGroup);
    textForm->addRow(tr("Font"), m_fontCombo);

    m_fontSize = new QSpinBox(m_textGroup);
    m_fontSize->setRange(6, 400);
    m_fontSize->setSuffix(tr(" px"));
    // Strip-scale pixels: the same number the render uses, so a size chosen here means the same thing
    // in the output. It is not a point size and does not follow the screen's DPI.
    m_fontSize->setToolTip(tr("Height in output pixels, at the project's target width."));
    textForm->addRow(tr("Size"), m_fontSize);

    m_boldCheck = new QCheckBox(tr("Bold"), m_textGroup);
    textForm->addRow(QString(), m_boldCheck);

    m_alignCombo = new QComboBox(m_textGroup);
    m_alignCombo->addItem(tr("Centre"), int(Qt::AlignHCenter));
    m_alignCombo->addItem(tr("Left"),   int(Qt::AlignLeft));
    m_alignCombo->addItem(tr("Right"),  int(Qt::AlignRight));
    textForm->addRow(tr("Align"), m_alignCombo);

    m_textSwatch = new QPushButton(tr("Colour"), m_textGroup);
    textForm->addRow(tr("Colour"), m_textSwatch);
    textLay->addLayout(textForm);

    // --- Actions --------------------------------------------------------------------------------
    auto* fitBtn = new QPushButton(tr("Fit to text"), this);
    fitBtn->setToolTip(tr("Grow the bubble until the whole line fits."));
    auto* delBtn = new QPushButton(tr("Delete"), this);
    auto* actions = new QHBoxLayout;
    actions->addWidget(fitBtn);
    actions->addWidget(delBtn);

    auto* lay = qobject_cast<QVBoxLayout*>(layout());
    if (lay) {
        lay->addLayout(presetRow);
        lay->addWidget(m_shapeGroup);
        lay->addWidget(m_textGroup);
        lay->addLayout(actions);
        lay->addStretch(1);
    }

    // --- Wiring ---------------------------------------------------------------------------------
    connect(m_shapeTiles, &QButtonGroup::idClicked, this, [this](int id) {
        // Picking a shape gives you the shape its tile shows — a narration box does not arrive wearing a
        // tail. The checkbox stays available for the cases that want one anyway.
        QSignalBlocker block(m_tailCheck);
        m_tailCheck->setChecked(shapeSpeaks(static_cast<TextArtifact::Shape>(id)));
        onControlChanged();
    });
    connect(m_alignCombo, &QComboBox::currentIndexChanged, this, [this] { onControlChanged(); });
    connect(m_tailCheck,  &QCheckBox::toggled,             this, [this] { onControlChanged(); });
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this] { onControlChanged(); });
    connect(m_styleAmount,&QSpinBox::valueChanged,         this, [this] { onControlChanged(); });
    connect(m_tailWidth,  &QSpinBox::valueChanged,         this, [this] { onControlChanged(); });
    connect(m_tailBend,   &QSpinBox::valueChanged,         this, [this] { onControlChanged(); });
    connect(m_addTail,    &QPushButton::clicked, this, [this] {
        // A new tail starts opposite the last one so it is visible rather than stacked on top of it.
        Tail t;
        t.baseWidth = m_tailWidth->value();
        t.bend      = m_tailBend->value() / 100.0;
        t.tip       = m_artifact.tails.isEmpty()
            ? QPointF(m_artifact.box.width() * 0.28, m_artifact.box.height() * 1.25)
            : QPointF(m_artifact.box.width() - m_artifact.tails.last().tip.x(),
                      m_artifact.tails.last().tip.y());
        m_artifact.tails.append(t);
        m_tailCheck->setChecked(true);
        onControlChanged();
    });
    connect(m_boldCheck,  &QCheckBox::toggled,             this, [this] { onControlChanged(); });
    connect(m_strokeWidth, &QSpinBox::valueChanged,        this, [this] { onControlChanged(); });
    connect(m_fontSize,    &QSpinBox::valueChanged,        this, [this] { onControlChanged(); });
    connect(m_fontCombo,   &QFontComboBox::currentFontChanged, this, [this] { onControlChanged(); });
    connect(m_textEdit,    &QPlainTextEdit::textChanged,   this, [this] { onControlChanged(); });

    connect(m_fillSwatch,   &QPushButton::clicked, this, [this] { pickColour(m_artifact.fill,       m_fillSwatch); });
    connect(m_strokeSwatch, &QPushButton::clicked, this, [this] { pickColour(m_artifact.stroke,     m_strokeSwatch); });
    connect(m_textSwatch,   &QPushButton::clicked, this, [this] { pickColour(m_artifact.textColour, m_textSwatch); });

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

    connect(fitBtn, &QPushButton::clicked, this, [this] { if (m_hasSelection) emit fitRequested(); });
    connect(delBtn, &QPushButton::clicked, this, [this] { if (m_hasSelection) emit deleteRequested(); });

    syncFromModel();
    clearSelection();
}

BubblePanel::~BubblePanel()
{
    delete ui;
}

// ---------------------------------------------------------------------------

void BubblePanel::setArtifact(const TextArtifact& a)
{
    m_artifact    = a;
    m_hasSelection = true;
    syncFromModel();
    m_textGroup->setEnabled(true);
    m_shapeGroup->setEnabled(true);
}

void BubblePanel::clearSelection()
{
    m_hasSelection = false;
    m_commitTimer->stop();
    // The controls stay readable and usable: with nothing selected they are the styling the *next*
    // placement will use (see prototype()), which is how a drawing tool's options normally behave.
    m_textEdit->setEnabled(false);
}

void BubblePanel::focusText()
{
    m_textEdit->setFocus(Qt::OtherFocusReason);
    m_textEdit->selectAll();   // a duplicate arrives with the original's line; typing should replace it
}

void BubblePanel::setShapeControlsVisible(bool visible)
{
    m_shapeVisible = visible;
    m_shapeGroup->setVisible(visible);
}

TextArtifact::Shape BubblePanel::currentShape() const
{
    return static_cast<TextArtifact::Shape>(m_shapeTiles->checkedId());
}

TextArtifact BubblePanel::prototype() const
{
    TextArtifact a;
    a.shape         = currentShape();
    a.fontFamily    = m_fontCombo->currentFont().family();
    a.fontPixelSize = m_fontSize->value();
    a.bold          = m_boldCheck->isChecked();
    a.align         = m_alignCombo->currentData().toInt();
    a.fill          = m_artifact.fill;
    a.stroke        = m_artifact.stroke;
    a.textColour    = m_artifact.textColour;
    a.strokeWidth   = m_strokeWidth->value();
    a.style         = static_cast<TextArtifact::Style>(m_styleCombo->currentData().toInt());
    a.styleAmount   = m_styleAmount->value() / 100.0;
    // A fresh seed per bubble, so a page of marker balloons does not wear one repeated wobble. Set at
    // placement and then left alone — re-rolling it on every edit would make the outline crawl as you
    // type.
    a.styleSeed     = QRandomGenerator::global()->generate();
    if (m_tailCheck->isChecked() && a.shape != TextArtifact::Shape::None) {
        Tail t;
        t.baseWidth = m_tailWidth->value();
        t.bend      = m_tailBend->value() / 100.0;
        t.tip       = QPointF(a.box.width() * 0.28, a.box.height() * 1.25);
        a.tails     = {t};
    }
    return a;
}

// ---------------------------------------------------------------------------

void BubblePanel::onControlChanged()
{
    if (m_populating)
        return;

    m_artifact.shape         = currentShape();
    m_artifact.text          = m_textEdit->toPlainText();
    m_artifact.fontFamily    = m_fontCombo->currentFont().family();
    m_artifact.fontPixelSize = m_fontSize->value();
    m_artifact.bold          = m_boldCheck->isChecked();
    m_artifact.align         = m_alignCombo->currentData().toInt();
    m_artifact.strokeWidth   = m_strokeWidth->value();
    m_artifact.style         = static_cast<TextArtifact::Style>(m_styleCombo->currentData().toInt());
    m_artifact.styleAmount   = m_styleAmount->value() / 100.0;
    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (m_artifact.style != TextArtifact::Style::Clean && m_artifact.styleSeed == 0)
        m_artifact.styleSeed = QRandomGenerator::global()->generate();
    m_styleAmount->setEnabled(m_artifact.style != TextArtifact::Style::Clean);

    // A tail's *aim* is authored on the strip, not here. The checkbox only turns tails on and off, so
    // re-enabling has to place a sensible first tip rather than resurrect a stale one; width and bend
    // apply to all of them (see m_tailWidth).
    const bool wantTail = m_tailCheck->isChecked() && m_artifact.shape != TextArtifact::Shape::None;
    if (!wantTail) {
        m_artifact.tails.clear();
    } else {
        if (m_artifact.tails.isEmpty()) {
            Tail t;
            t.tip = QPointF(m_artifact.box.width() * 0.28, m_artifact.box.height() * 1.25);
            m_artifact.tails.append(t);
        }
        for (Tail& t : m_artifact.tails) {
            t.baseWidth = m_tailWidth->value();
            t.bend      = m_tailBend->value() / 100.0;
        }
    }
    m_tailWidth->setEnabled(wantTail);
    m_tailBend->setEnabled(wantTail);
    m_addTail->setEnabled(wantTail);

    if (!m_hasSelection)
        return;   // styling the next placement, nothing to preview or persist yet

    emit changed(m_artifact);
    m_commitTimer->start();
}

void BubblePanel::syncFromModel()
{
    m_populating = true;
    {
        QSignalBlocker b2(m_tailCheck),  b3(m_strokeWidth), b4(m_textEdit);
        QSignalBlocker b9(m_tailWidth),  b10(m_tailBend);
        QSignalBlocker b11(m_styleCombo), b12(m_styleAmount);
        QSignalBlocker b5(m_fontCombo),  b6(m_fontSize),   b7(m_boldCheck),   b8(m_alignCombo);

        // Checkable buttons in an exclusive group do not emit on setChecked(), so no blocker is needed.
        if (auto* tile = m_shapeTiles->button(int(m_artifact.shape)))
            tile->setChecked(true);
        const bool hasTail = !m_artifact.tails.isEmpty();
        m_tailCheck->setChecked(hasTail);
        if (hasTail) {
            m_tailWidth->setValue(qRound(m_artifact.tails.first().baseWidth));
            m_tailBend->setValue(qRound(m_artifact.tails.first().bend * 100.0));
        }
        m_tailWidth->setEnabled(hasTail);
        m_tailBend->setEnabled(hasTail);
        m_addTail->setEnabled(hasTail);
        m_strokeWidth->setValue(m_artifact.strokeWidth);
        m_styleCombo->setCurrentIndex(m_styleCombo->findData(int(m_artifact.style)));
        m_styleAmount->setValue(qRound(m_artifact.styleAmount * 100.0));
        m_styleAmount->setEnabled(m_artifact.style != TextArtifact::Style::Clean);

        if (m_textEdit->toPlainText() != m_artifact.text)
            m_textEdit->setPlainText(m_artifact.text);   // guarded: setPlainText resets the caret
        m_textEdit->setEnabled(true);

        if (!m_artifact.fontFamily.isEmpty())
            m_fontCombo->setCurrentFont(QFont(m_artifact.fontFamily));
        m_fontSize->setValue(m_artifact.fontPixelSize);
        m_boldCheck->setChecked(m_artifact.bold);
        const int alignIdx = m_alignCombo->findData(m_artifact.align);
        if (alignIdx >= 0)
            m_alignCombo->setCurrentIndex(alignIdx);
    }
    paintSwatch(m_fillSwatch,   m_artifact.fill);
    paintSwatch(m_strokeSwatch, m_artifact.stroke);
    paintSwatch(m_textSwatch,   m_artifact.textColour);
    m_populating = false;
}

void BubblePanel::pickColour(QColor& target, QPushButton* swatch)
{
    const QColor picked = QColorDialog::getColor(target, this, tr("Choose colour"));
    if (!picked.isValid())
        return;
    target = picked;
    paintSwatch(swatch, picked);
    if (!m_hasSelection)
        return;
    emit changed(m_artifact);
    emit committed(m_artifact);   // a dialog choice is discrete — commit it without waiting on the timer
}

void BubblePanel::refreshShapeTiles()
{
    if (!m_shapeTiles)
        return;
    for (QAbstractButton* b : m_shapeTiles->buttons())
        b->setIcon(QIcon(shapeThumbnail(static_cast<TextArtifact::Shape>(m_shapeTiles->id(b)), palette())));
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

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
        m_presetCombo->addItem(QIcon(bubbleThumbnail(p.artifact.shape, p.artifact.fill,
                                                     p.artifact.stroke, p.artifact.textColour)),
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
    a.text  = m_artifact.text;
    a.box   = m_artifact.box;
    a.tails = m_artifact.tails;
    // With the Text tool the shape picker is hidden, so a preset must not change a shape the author
    // cannot see, and cannot put back.
    if (!m_shapeVisible)
        a.shape = m_artifact.shape;
    if (a.shape == TextArtifact::Shape::None)
        a.tails.clear();
    // Keep the bubble's own seed. Re-rolling it would make an already-placed marker outline jump for a
    // reason the author did not ask for.
    a.styleSeed = m_artifact.styleSeed;
    if (a.style != TextArtifact::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();

    m_artifact = a;
    syncFromModel();
    if (!m_hasSelection) {
        // The panel is simply the next placement's styling now. syncFromModel() re-enables the text box
        // on the way through, and with nothing selected there is nothing to type into.
        clearSelection();
        return;
    }
    emit changed(m_artifact);
    emit committed(m_artifact);   // a discrete choice, like the colour dialog: one undo step, no timer
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
    p.artifact.tails.clear();
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

void BubblePanel::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    // The tiles are drawn in palette colours, so a theme flip has to redraw them or they stay in the
    // previous theme's ink (the app follows the Windows light/dark setting).
    if (e->type() == QEvent::PaletteChange)
        refreshShapeTiles();
}

void BubblePanel::paintSwatch(QPushButton* swatch, const QColor& c)
{
    // An icon, not a stylesheet: the button keeps the theme's own look (see the "inherit, don't
    // hardcode colours" rule) and only carries the chosen colour as a chip.
    QPixmap pm(k_swatchPx, k_swatchPx);
    pm.fill(c);
    swatch->setIcon(QIcon(pm));
}
