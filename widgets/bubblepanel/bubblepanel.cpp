#include "bubblepanel.h"
#include "ui_bubblepanel.h"
#include "flowlayout.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QEvent>
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
    return shape == TextArtifact::Shape::Speech || shape == TextArtifact::Shape::Shout;
}

/**
 * @brief A miniature of \p shape, drawn by the very rasteriser that draws the real bubble.
 *
 * Reusing renderArtifact() means a tile cannot misrepresent its shape, and it costs no icon assets: the
 * picker is generated, not drawn by hand. Colours come from the palette rather than the artifact's own,
 * so the tiles read as UI chrome in either theme instead of as three white blobs.
 */
QPixmap shapeThumbnail(TextArtifact::Shape shape, const QPalette& pal)
{
    TextArtifact a;
    a.shape         = shape;
    a.box           = QSize(k_shapeIconW * k_shapeIconScale, k_shapeIconH * k_shapeIconScale);
    a.strokeWidth   = 2 * k_shapeIconScale;
    a.text          = QStringLiteral("Aa");
    a.fontPixelSize = a.box.height() / 3;
    a.fill          = pal.color(QPalette::Base);
    a.stroke        = pal.color(QPalette::WindowText);
    a.textColour    = pal.color(QPalette::WindowText);
    a.tail          = shapeSpeaks(shape) ? QPoint(a.box.width() / 4, a.box.height() - a.strokeWidth)
                                         : QPoint(0, -1);

    return QPixmap::fromImage(renderArtifact(a).scaled(QSize(k_shapeIconW, k_shapeIconH),
                                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    addShapeTile(TextArtifact::Shape::Speech,  tr("Speech balloon"));
    addShapeTile(TextArtifact::Shape::Shout,   tr("Shout"));
    addShapeTile(TextArtifact::Shape::Caption, tr("Caption box"));
    addShapeTile(TextArtifact::Shape::None,    tr("Text only — no balloon"));
    if (auto* first = m_shapeTiles->button(int(TextArtifact::Shape::Speech)))
        first->setChecked(true);
    refreshShapeTiles();
    shapeLay->addWidget(tileHost);

    auto* shapeForm = new QFormLayout;
    shapeLay->addLayout(shapeForm);

    m_tailCheck = new QCheckBox(tr("Tail"), m_shapeGroup);
    m_tailCheck->setToolTip(tr("Drag the round handle on the bubble to aim it."));
    shapeForm->addRow(QString(), m_tailCheck);

    m_fillSwatch   = new QPushButton(tr("Fill"),   m_shapeGroup);
    m_strokeSwatch = new QPushButton(tr("Stroke"), m_shapeGroup);
    auto* colourRow = new QHBoxLayout;
    colourRow->addWidget(m_fillSwatch);
    colourRow->addWidget(m_strokeSwatch);
    shapeForm->addRow(tr("Colours"), colourRow);

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
    connect(m_boldCheck,  &QCheckBox::toggled,             this, [this] { onControlChanged(); });
    connect(m_strokeWidth, &QSpinBox::valueChanged,        this, [this] { onControlChanged(); });
    connect(m_fontSize,    &QSpinBox::valueChanged,        this, [this] { onControlChanged(); });
    connect(m_fontCombo,   &QFontComboBox::currentFontChanged, this, [this] { onControlChanged(); });
    connect(m_textEdit,    &QPlainTextEdit::textChanged,   this, [this] { onControlChanged(); });

    connect(m_fillSwatch,   &QPushButton::clicked, this, [this] { pickColour(m_artifact.fill,       m_fillSwatch); });
    connect(m_strokeSwatch, &QPushButton::clicked, this, [this] { pickColour(m_artifact.stroke,     m_strokeSwatch); });
    connect(m_textSwatch,   &QPushButton::clicked, this, [this] { pickColour(m_artifact.textColour, m_textSwatch); });

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
    if (!m_tailCheck->isChecked() || a.shape == TextArtifact::Shape::None)
        a.tail = QPoint(0, -1);   // no tail
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

    // The tail's position is authored on the strip, not here — the checkbox only turns it on and off,
    // so re-enabling it has to restore a sensible tip rather than resurrect a stale one.
    const bool wantTail = m_tailCheck->isChecked() && m_artifact.shape != TextArtifact::Shape::None;
    if (!wantTail)
        m_artifact.tail = QPoint(0, -1);
    else if (m_artifact.tail.y() < 0)
        m_artifact.tail = QPoint(m_artifact.box.width() / 4, m_artifact.box.height() - 2);

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
        QSignalBlocker b5(m_fontCombo),  b6(m_fontSize),   b7(m_boldCheck),   b8(m_alignCombo);

        // Checkable buttons in an exclusive group do not emit on setChecked(), so no blocker is needed.
        if (auto* tile = m_shapeTiles->button(int(m_artifact.shape)))
            tile->setChecked(true);
        m_tailCheck->setChecked(m_artifact.tail.y() >= 0);
        m_strokeWidth->setValue(m_artifact.strokeWidth);

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
