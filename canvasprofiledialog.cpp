#include "canvasprofiledialog.h"
#include "ui_canvasprofiledialog.h"

#include <platemaker/models/canvas_profile.hpp>

#include <QColorDialog>
#include <QFrame>
#include <QPushButton>
#include <QPainter>

// ---------------------------------------------------------------------------

CanvasProfileDialog::CanvasProfileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CanvasProfileDialog)
    , m_visualColour(255, 105, 180, 128)
    , m_bgColour(0, 0, 0, 0)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Make textAbsoluteSize look like a dim label, not an editor.
    ui->textAbsoluteSize->setFrameStyle(QFrame::NoFrame);
    ui->textAbsoluteSize->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textAbsoluteSize->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->textAbsoluteSize->setMaximumHeight(20);
    ui->textAbsoluteSize->setStyleSheet(
        "QTextBrowser { background: transparent; color: #888888; "
        "font-size: 11px; border: none; padding: 0; }");

    // A standalone QRadioButton stays checked after first click unless we
    // disable auto-exclusivity, which makes it behave like a checkbox toggle.
    ui->radioButtonSafeArea->setAutoExclusive(false);

    applyColourToButton(ui->buttonVisualColour, m_visualColour);
    applyColourToButton(ui->buttonBgColour,     m_bgColour);

    connect(ui->buttonSave,         &QPushButton::clicked,  this, &CanvasProfileDialog::onSaveClicked);
    connect(ui->buttonVisualColour, &QPushButton::clicked,  this, &CanvasProfileDialog::onPickVisualColour);
    connect(ui->buttonBgColour,     &QPushButton::clicked,  this, &CanvasProfileDialog::onPickBgColour);
    connect(ui->radioButtonSafeArea, &QRadioButton::toggled, this, &CanvasProfileDialog::onSafeAreaModeToggled);

    connect(ui->spinBoxWidth,        &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxHeight,       &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginTop,    &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginBottom, &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginLeft,   &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);
    connect(ui->spinBoxMarginRight,  &QSpinBox::valueChanged, this, &CanvasProfileDialog::onDimensionsChanged);

    updatePreview();
    updateSizeInfoLabel();
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
    // Restore the safe-area-mode radio state from the hint stored in the profile.
    m_safeAreaMode = profile.hintUserSafeAreaSelect;
    ui->radioButtonSafeArea->blockSignals(true);
    ui->radioButtonSafeArea->setChecked(m_safeAreaMode);
    ui->radioButtonSafeArea->blockSignals(false);

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
    updateSizeInfoLabel();
}

Platemaker::Models::CanvasProfile CanvasProfileDialog::profile() const
{
    Platemaker::Models::CanvasProfile cp;
    cp.name = ui->lineEditName->text().toStdString();

    const int mT = ui->spinBoxMarginTop->value();
    const int mR = ui->spinBoxMarginRight->value();
    const int mB = ui->spinBoxMarginBottom->value();
    const int mL = ui->spinBoxMarginLeft->value();

    if (m_safeAreaMode) {
        // Spinboxes hold safe area → absolute canvas = safe area + all margins.
        cp.canvasSize = { ui->spinBoxWidth->value()  + mL + mR,
                          ui->spinBoxHeight->value() + mT + mB };
    } else {
        cp.canvasSize = { ui->spinBoxWidth->value(),
                          ui->spinBoxHeight->value() };
    }

    cp.margins                = { mT, mR, mB, mL };
    cp.hintUserSafeAreaSelect = m_safeAreaMode;
    cp.visualColour     = { static_cast<uint8_t>(m_visualColour.red()),
                             static_cast<uint8_t>(m_visualColour.green()),
                             static_cast<uint8_t>(m_visualColour.blue()),
                             static_cast<uint8_t>(m_visualColour.alpha()) };
    cp.backgroundColour = { static_cast<uint8_t>(m_bgColour.red()),
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
    updateSizeInfoLabel();
    updatePreview();
}

void CanvasProfileDialog::onSafeAreaModeToggled(bool checked)
{
    m_safeAreaMode = checked;
    updateSizeInfoLabel();
    updatePreview();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CanvasProfileDialog::updateSizeInfoLabel()
{
    const int w  = ui->spinBoxWidth->value();
    const int h  = ui->spinBoxHeight->value();
    const int mT = ui->spinBoxMarginTop->value();
    const int mB = ui->spinBoxMarginBottom->value();
    const int mL = ui->spinBoxMarginLeft->value();
    const int mR = ui->spinBoxMarginRight->value();

    if (m_safeAreaMode) {
        const int absW = w + mL + mR;
        const int absH = h + mT + mB;
        ui->textAbsoluteSize->setText(
            tr("Absolute size: %1 × %2").arg(absW).arg(absH));
        ui->labelPreviewDimensions->setText(
            tr("%1 × %2 px").arg(absW).arg(absH));
    } else {
        const int safeW = w - mL - mR;
        const int safeH = h - mT - mB;
        if (safeW > 0 && safeH > 0) {
            ui->textAbsoluteSize->setText(
                tr("Work area: %1 × %2").arg(safeW).arg(safeH));
        } else {
            ui->textAbsoluteSize->setText(tr("Work area: (margins exceed canvas)"));
        }
        ui->labelPreviewDimensions->setText(
            tr("%1 × %2 px").arg(w).arg(h));
    }
}

void CanvasProfileDialog::applyColourToButton(QPushButton *button, const QColor &colour)
{
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
    const int mT = ui->spinBoxMarginTop->value();
    const int mB = ui->spinBoxMarginBottom->value();
    const int mL = ui->spinBoxMarginLeft->value();
    const int mR = ui->spinBoxMarginRight->value();

    // Preview always uses absolute canvas dimensions.
    const int absW = m_safeAreaMode
                     ? ui->spinBoxWidth->value()  + mL + mR
                     : ui->spinBoxWidth->value();
    const int absH = m_safeAreaMode
                     ? ui->spinBoxHeight->value() + mT + mB
                     : ui->spinBoxHeight->value();

    QWidget *preview = ui->canvasPreviewWidget;
    if (!preview) return;

    preview->setProperty("canvasW",      absW);
    preview->setProperty("canvasH",      absH);
    preview->setProperty("marginTop",    mT);
    preview->setProperty("marginBottom", mB);
    preview->setProperty("marginLeft",   mL);
    preview->setProperty("marginRight",  mR);
    preview->setProperty("visualColour", m_visualColour);
    preview->setProperty("bgColour",     m_bgColour);
    preview->update();
}
