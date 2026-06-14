#include "canvasprofiledialog.h"
#include "ui_canvasprofiledialog.h"

#include <platemaker/models/canvas_profile.hpp>

#include <QColorDialog>
#include <QPushButton>
#include <QPainter>

// ---------------------------------------------------------------------------

CanvasProfileDialog::CanvasProfileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CanvasProfileDialog)
    , m_visualColour(255, 105, 180, 128)   // default: pink overlay
    , m_bgColour(0, 0, 0, 0)              // default: transparent
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    applyColourToButton(ui->buttonVisualColour, m_visualColour);
    applyColourToButton(ui->buttonBgColour,     m_bgColour);

    connect(ui->buttonSave,         &QPushButton::clicked, this, &CanvasProfileDialog::onSaveClicked);
    connect(ui->buttonVisualColour, &QPushButton::clicked, this, &CanvasProfileDialog::onPickVisualColour);
    connect(ui->buttonBgColour,     &QPushButton::clicked, this, &CanvasProfileDialog::onPickBgColour);

    // Refresh preview whenever dimensions or margins change
    connect(ui->spinBoxWidth,        &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxHeight,       &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginTop,    &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginBottom, &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginLeft,   &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginRight,  &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);

    updatePreview();
}

CanvasProfileDialog::~CanvasProfileDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CanvasProfileDialog::setProfile(const Platemaker::Models::CanvasProfile &profile)
{
    ui->lineEditName->setText(QString::fromStdString(profile.name));

    ui->spinBoxWidth->setValue(profile.canvasSize.width);
    ui->spinBoxHeight->setValue(profile.canvasSize.height);

    ui->spinBoxMarginTop->setValue(profile.margins.top);
    ui->spinBoxMarginBottom->setValue(profile.margins.bottom);
    ui->spinBoxMarginLeft->setValue(profile.margins.left);
    ui->spinBoxMarginRight->setValue(profile.margins.right);

    m_visualColour = QColor(profile.visualColour.r,
                            profile.visualColour.g,
                            profile.visualColour.b,
                            profile.visualColour.a);
    m_bgColour     = QColor(profile.backgroundColour.r,
                            profile.backgroundColour.g,
                            profile.backgroundColour.b,
                            profile.backgroundColour.a);

    applyColourToButton(ui->buttonVisualColour, m_visualColour);
    applyColourToButton(ui->buttonBgColour,     m_bgColour);

    updatePreview();
}

Platemaker::Models::CanvasProfile CanvasProfileDialog::profile() const
{
    Platemaker::Models::CanvasProfile cp;
    cp.name              = ui->lineEditName->text().toStdString();
    cp.canvasSize        = { ui->spinBoxWidth->value(), ui->spinBoxHeight->value() };
    cp.margins           = { ui->spinBoxMarginTop->value(),
                             ui->spinBoxMarginBottom->value(),
                             ui->spinBoxMarginLeft->value(),
                             ui->spinBoxMarginRight->value() };
    cp.visualColour      = { static_cast<uint8_t>(m_visualColour.red()),
                             static_cast<uint8_t>(m_visualColour.green()),
                             static_cast<uint8_t>(m_visualColour.blue()),
                             static_cast<uint8_t>(m_visualColour.alpha()) };
    cp.backgroundColour  = { static_cast<uint8_t>(m_bgColour.red()),
                             static_cast<uint8_t>(m_bgColour.green()),
                             static_cast<uint8_t>(m_bgColour.blue()),
                             static_cast<uint8_t>(m_bgColour.alpha()) };
    return cp;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void CanvasProfileDialog::onSaveClicked()
{
    if (ui->lineEditName->text().trimmed().isEmpty()) {
        ui->lineEditName->setFocus();
        ui->lineEditName->setStyleSheet("border: 1px solid #e06060;");
        return;
    }
    accept();
}

void CanvasProfileDialog::onPickVisualColour()
{
    QColor picked = QColorDialog::getColor(
        m_visualColour, this, "Visual / margin colour",
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;
    m_visualColour = picked;
    applyColourToButton(ui->buttonVisualColour, m_visualColour);
    updatePreview();
}

void CanvasProfileDialog::onPickBgColour()
{
    QColor picked = QColorDialog::getColor(
        m_bgColour, this, "Background colour",
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;
    m_bgColour = picked;
    applyColourToButton(ui->buttonBgColour, m_bgColour);
    updatePreview();
}

void CanvasProfileDialog::onDimensionsChanged()
{
    const int w = ui->spinBoxWidth->value();
    const int h = ui->spinBoxHeight->value();
    ui->labelPreviewDimensions->setText(QString("%1 × %2 px").arg(w).arg(h));
    updatePreview();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CanvasProfileDialog::applyColourToButton(QPushButton *button, const QColor &colour)
{
    // Show a solid swatch of the chosen colour as the button background.
    // A checkerboard pattern would be nicer for alpha < 255, but this keeps
    // the code dependency-free — swap in whatever you prefer.
    QString style = QString(
        "background-color: rgba(%1,%2,%3,%4);"
        "border: 1px solid #555555;"
        "border-radius: 3px;")
        .arg(colour.red()).arg(colour.green())
        .arg(colour.blue()).arg(colour.alpha());
    button->setStyleSheet(style);
}

void CanvasProfileDialog::updatePreview()
{
    // The preview is painted directly onto canvasPreviewWidget.
    // We use a lambda-free approach: install this dialog as an event filter,
    // OR (simpler) just call update() and override paintEvent via a subclass.
    //
    // For now we use the simplest viable approach: draw into the widget by
    // grabbing its painter in a helper and calling repaint via an event filter.
    //
    // Recommended next step: create a tiny CanvasPreviewWidget : QWidget class
    // and promote canvasPreviewWidget in Qt Designer to it.  Pass it the
    // current values and let it handle its own paintEvent.  Example:
    //
    //   canvasPreviewWidget->setProfile(w, h, margins, m_visualColour);
    //   canvasPreviewWidget->update();
    //
    // Until that class exists, we repaint manually here:

    QWidget *preview = ui->canvasPreviewWidget;
    if (!preview) return;

    // Store current values as properties so the widget can read them in
    // a custom paintEvent if you later subclass / install an event filter.
    preview->setProperty("canvasW",        ui->spinBoxWidth->value());
    preview->setProperty("canvasH",        ui->spinBoxHeight->value());
    preview->setProperty("marginTop",      ui->spinBoxMarginTop->value());
    preview->setProperty("marginBottom",   ui->spinBoxMarginBottom->value());
    preview->setProperty("marginLeft",     ui->spinBoxMarginLeft->value());
    preview->setProperty("marginRight",    ui->spinBoxMarginRight->value());
    preview->setProperty("visualColour",   m_visualColour);
    preview->setProperty("bgColour",       m_bgColour);
    preview->update();
}
