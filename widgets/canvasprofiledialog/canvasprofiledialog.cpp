#include "canvasprofiledialog.hpp"
#include "ui_canvasprofiledialog.h"

#include <platemaker/models/canvas_profile.hpp>

#include <QColorDialog>
#include <QLocale>
#include <QMessageBox>
#include <QEvent>
#include <QFrame>
#include <QPushButton>
#include <QPainter>

#include <algorithm>

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

    // canvasPreviewWidget is a plain QWidget with no paintEvent of its own;
    // intercept its paint event here so we can draw the live canvas preview.
    ui->canvasPreviewWidget->installEventFilter(this);

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

    addSavedNotes();
    connect(ui->buttonRevert, &QPushButton::clicked, this, &CanvasProfileDialog::onRevertClicked);
    connect(ui->lineEditName, &QLineEdit::textChanged, this, &CanvasProfileDialog::refreshSaved);
    connect(ui->spinBoxDpi, &QDoubleSpinBox::valueChanged, this, &CanvasProfileDialog::refreshSaved);

    updatePreview();
    updateSizeInfoLabel();
    refreshSaved();
}

CanvasProfileDialog::~CanvasProfileDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CanvasProfileDialog::setProfile(const Platemaker::Models::CanvasProfile &profile, ProfileSource source)
{
    // Kept whole, so profile() hands back what this dialog does not edit (id, template settings…).
    m_profile = profile;
    m_saved   = source == ProfileSource::Saved;

    // Restore the safe-area-mode radio state from the hint stored in the profile.
    m_safeAreaMode = profile.hintUserSafeAreaSelect;
    ui->radioButtonSafeArea->blockSignals(true);
    ui->radioButtonSafeArea->setChecked(m_safeAreaMode);
    ui->radioButtonSafeArea->blockSignals(false);

    ui->lineEditName->setText(QString::fromStdString(profile.name));

    ui->spinBoxMarginTop->setValue(profile.margins.top);
    ui->spinBoxMarginBottom->setValue(profile.margins.bottom);
    ui->spinBoxMarginLeft->setValue(profile.margins.left);
    ui->spinBoxMarginRight->setValue(profile.margins.right);

    // canvasSize is always stored as the absolute canvas. If the user entered
    // it as a safe area, display safe area = canvas − margins in the spinboxes
    // so they see their data exactly as they typed it.
    if (m_safeAreaMode) {
        ui->spinBoxWidth->setValue(profile.canvasSize.width
                                   - profile.margins.left - profile.margins.right);
        ui->spinBoxHeight->setValue(profile.canvasSize.height
                                    - profile.margins.top - profile.margins.bottom);
    } else {
        ui->spinBoxWidth->setValue(profile.canvasSize.width);
        ui->spinBoxHeight->setValue(profile.canvasSize.height);
    }

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

    ui->spinBoxDpi->setValue(profile.dpi);

    updatePreview();
    updateSizeInfoLabel();
    refreshSaved();
}

Platemaker::Models::CanvasProfile CanvasProfileDialog::profile() const
{
    Platemaker::Models::CanvasProfile cp = m_profile;   // the fields this dialog does not show, unchanged
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
    cp.dpi = ui->spinBoxDpi->value();
    return cp;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void CanvasProfileDialog::setExistingProfiles(const std::vector<Platemaker::Models::CanvasProfile>& profiles)
{
    m_existing = profiles;
}

void CanvasProfileDialog::setSaveCheck(std::function<bool(const Platemaker::Models::CanvasProfile&)> check)
{
    m_saveCheck = std::move(check);
}

void CanvasProfileDialog::onSaveClicked()
{
    if (ui->lineEditName->text().trimmed().isEmpty()) {
        ui->lineEditName->setFocus();
        ui->lineEditName->setStyleSheet("border: 1px solid #e06060;");
        return;
    }

    // An identical profile is worth a word, not a wall: names may repeat, and this may be a step on the
    // way (a copy about to be changed, two names being swapped). The profile itself is not compared —
    // a profile being edited has the same id as its saved self; a new or duplicated one has none.
    const auto edited = profile();
    for (const auto& other : m_existing) {
        if ((!edited.id.empty() && other.id == edited.id)
            || !Platemaker::Models::sameProfileContent(other, edited))
            continue;
        const auto answer = QMessageBox::warning(this, tr("Identical profile"),
            tr("This profile is identical in every setting to \"%1\". Pages would match either one the "
               "same way, so two of them only make the lists harder to read.\n\nSave it anyway?")
                .arg(QString::fromStdString(other.name)),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Save)
            return;
        break;
    }
    if (m_saveCheck && !m_saveCheck(edited))
        return;
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
    refreshSaved();
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
    refreshSaved();
}

void CanvasProfileDialog::onDimensionsChanged()
{
    updateSizeInfoLabel();
    updatePreview();
    refreshSaved();
}

void CanvasProfileDialog::onSafeAreaModeToggled(bool checked)
{
    m_safeAreaMode = checked;
    updateSizeInfoLabel();
    updatePreview();
    refreshSaved();
}

void CanvasProfileDialog::onRevertClicked()
{
    const auto answer = QMessageBox::question(this, tr("Revert"),
        tr("Put every field back to the saved profile? Your changes here are lost."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes)
        setProfile(m_profile, ProfileSource::Saved);
}

void CanvasProfileDialog::refreshSaved()
{
    const bool differs = m_saved ? m_savedNotes.refresh(m_profile, profile()) : false;
    if (!m_saved)
        m_savedNotes.clearAll();
    ui->buttonRevert->setVisible(m_saved);
    ui->buttonRevert->setEnabled(differs);
}

void CanvasProfileDialog::addSavedNotes()
{
    using Platemaker::Models::CanvasProfile;
    const auto number  = [](int v) { return QString::number(v); };   // as a QSpinBox shows it
    const auto colour  = [](const Platemaker::Models::RGBA& c) {
        return QColor(c.r, c.g, c.b, c.a).name(QColor::HexArgb);
    };
    // Width and height as the spin boxes show them now: the safe area in safe-area mode, the canvas
    // otherwise — the saved profile read the same way, so a note says what this field would hold.
    const auto shownWidth = [this](const CanvasProfile& p) {
        return m_safeAreaMode ? p.canvasSize.width - p.margins.left - p.margins.right : p.canvasSize.width;
    };
    const auto shownHeight = [this](const CanvasProfile& p) {
        return m_safeAreaMode ? p.canvasSize.height - p.margins.top - p.margins.bottom : p.canvasSize.height;
    };

    m_savedNotes.add(ui->labelSavedName, [](const CanvasProfile& p) { return QString::fromStdString(p.name); });
    m_savedNotes.add(ui->labelSavedWidth, [=](const CanvasProfile& p) { return number(shownWidth(p)); });
    m_savedNotes.add(ui->labelSavedHeight, [=](const CanvasProfile& p) { return number(shownHeight(p)); });
    m_savedNotes.add(ui->labelSavedSafeArea, [this](const CanvasProfile& p) {
        return p.hintUserSafeAreaSelect ? tr("safe area") : tr("absolute size");
    });
    m_savedNotes.add(ui->labelSavedDpi, [this](const CanvasProfile& p) {
        QLocale shown = ui->spinBoxDpi->locale();   // as the spin box shows it
        shown.setNumberOptions(QLocale::OmitGroupSeparator);
        return shown.toString(p.dpi, 'f', ui->spinBoxDpi->decimals());
    });
    m_savedNotes.add(ui->labelSavedMarginTop, [=](const CanvasProfile& p) { return number(p.margins.top); });
    m_savedNotes.add(ui->labelSavedMarginBottom, [=](const CanvasProfile& p) { return number(p.margins.bottom); });
    m_savedNotes.add(ui->labelSavedMarginLeft, [=](const CanvasProfile& p) { return number(p.margins.left); });
    m_savedNotes.add(ui->labelSavedMarginRight, [=](const CanvasProfile& p) { return number(p.margins.right); });
    m_savedNotes.add(ui->labelSavedVisualColour, [=](const CanvasProfile& p) { return colour(p.visualColour); });
    m_savedNotes.add(ui->labelSavedBgColour, [=](const CanvasProfile& p) { return colour(p.backgroundColour); });
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
    // The actual drawing happens in paintCanvasPreview() via the event filter;
    // here we just request a repaint with the current values.
    if (ui->canvasPreviewWidget)
        ui->canvasPreviewWidget->update();
}

bool CanvasProfileDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->canvasPreviewWidget && event->type() == QEvent::Paint) {
        paintCanvasPreview();
        return true; // we fully handled the paint
    }
    return QDialog::eventFilter(watched, event);
}

void CanvasProfileDialog::paintCanvasPreview()
{
    QWidget *w = ui->canvasPreviewWidget;

    const int mT = ui->spinBoxMarginTop->value();
    const int mB = ui->spinBoxMarginBottom->value();
    const int mL = ui->spinBoxMarginLeft->value();
    const int mR = ui->spinBoxMarginRight->value();

    // Spinboxes hold safe-area dimensions in safe-area mode; the preview always
    // works in absolute canvas coordinates.
    const int absW = m_safeAreaMode ? ui->spinBoxWidth->value()  + mL + mR
                                     : ui->spinBoxWidth->value();
    const int absH = m_safeAreaMode ? ui->spinBoxHeight->value() + mT + mB
                                     : ui->spinBoxHeight->value();

    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing);

    // Blend the widget background into the surrounding previewFrame (#141414).
    p.fillRect(w->rect(), QColor(0x14, 0x14, 0x14));

    if (absW <= 0 || absH <= 0)
        return;

    // 1. Available drawing area (with a small inset so the contour isn't clipped).
    const int pad = 10;
    const QRectF avail(pad, pad,
                       w->width()  - 2 * pad,
                       w->height() - 2 * pad);
    if (avail.width() <= 0 || avail.height() <= 0)
        return;

    // 2. Fit the canvas into the available area, preserving aspect ratio.
    const double scale = std::min(avail.width()  / absW,
                                  avail.height() / absH);
    const double dispW = absW * scale;
    const double dispH = absH * scale;
    const QRectF outer(avail.left() + (avail.width()  - dispW) / 2.0,
                       avail.top()  + (avail.height() - dispH) / 2.0,
                       dispW, dispH);

    // Inner (safe-area) rect = canvas inset by the scaled margins.
    const QRectF inner(outer.left()   + mL * scale,
                       outer.top()    + mT * scale,
                       outer.width()  - (mL + mR) * scale,
                       outer.height() - (mT + mB) * scale);

    // Safe-area / background fill: use the profile background colour if it has
    // any opacity, otherwise a neutral dark so the canvas shape stays visible.
    const QColor safeFill = (m_bgColour.alpha() > 0) ? m_bgColour
                                                     : QColor(45, 45, 45);

    // 3. Draw. With margins, paint the margin band in the visual colour and the
    //    safe area on top; without margins, just fill the canvas and outline it.
    const bool hasMargins = (mL || mR || mT || mB);
    if (hasMargins && inner.width() > 0 && inner.height() > 0) {
        p.fillRect(outer, m_visualColour);
        p.fillRect(inner, safeFill);
    } else {
        p.fillRect(outer, safeFill);
    }

    // Canvas contour.
    QPen pen(QColor(120, 120, 120));
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(outer);
}
