#include "ccpanel.h"
#include "ui_ccpanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int k_commitDebounceMs = 300; //!< Coalesce a drag into one undo step this long after it settles.
}

CcPanel::CcPanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::CcPanel)
{
    ui->setupUi(this); // provides the empty verticalLayout container; the controls are built here

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] { emit committed(m_cc); });

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setColumnStretch(1, 1);

    // One brightness/contrast/saturation row = label | slider | spin-box, the slider and spin mirroring
    // each other (slider is int; scale maps it to the spin's double range).
    auto addRow = [&](int row, const QString& label, QSlider*& slider, QDoubleSpinBox*& spin,
                      int sMin, int sMax, double bMin, double bMax, double scale) {
        grid->addWidget(new QLabel(label, this), row, 0);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(sMin, sMax);
        grid->addWidget(slider, row, 1);
        spin = new QDoubleSpinBox(this);
        spin->setRange(bMin, bMax);
        spin->setSingleStep(0.01);
        spin->setDecimals(2);
        spin->setMinimumWidth(84); // room for "-1,00" plus the step arrows
        grid->addWidget(spin, row, 2);

        connect(slider, &QSlider::valueChanged, this, [this, spin, scale](int v) {
            if (m_populating) return;
            QSignalBlocker block(spin);
            spin->setValue(v / scale);
            onControlChanged();
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, scale](double v) {
            if (m_populating) return;
            QSignalBlocker block(slider);
            slider->setValue(static_cast<int>(std::lround(v * scale)));
            onControlChanged();
        });
    };
    addRow(0, tr("Brightness"), m_brightnessSlider, m_brightnessSpin, -100, 100, -1.0, 1.0, 100.0);
    addRow(1, tr("Contrast"),   m_contrastSlider,   m_contrastSpin,      0, 200,  0.0, 2.0, 100.0);
    addRow(2, tr("Saturation"), m_saturationSlider, m_saturationSpin,    0, 200,  0.0, 2.0, 100.0);

    m_iccCheck = new QCheckBox(tr("Convert to sRGB (ICC)"), this);
    connect(m_iccCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_populating) return;
        onControlChanged();
        emit committed(m_cc); // a discrete toggle commits immediately
    });

    auto* resetBtn = new QPushButton(tr("Reset"), this);
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        m_cc.brightness = 0.0;
        m_cc.contrast   = 1.0;
        m_cc.saturation = 1.0; // curves / exclusions are not edited here, so leave them as they are
        syncFromModel();
        onControlChanged();
        emit committed(m_cc);
    });

    // Frame the controls in a titled group box so the tool options read as a distinct panel (matching the
    // app's other group-box panels) rather than floating on the window background.
    auto* group = new QGroupBox(tr("Colour correction"), this);
    auto* groupLay = new QVBoxLayout(group);
    groupLay->addLayout(grid);
    groupLay->addWidget(m_iccCheck);
    groupLay->addWidget(resetBtn);

    ui->verticalLayout->addWidget(group);
    ui->verticalLayout->addStretch(1);

    syncFromModel();
}

CcPanel::~CcPanel()
{
    delete ui;
}

void CcPanel::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    m_cc = cc;
    syncFromModel();
}

void CcPanel::onControlChanged()
{
    m_cc.brightness = m_brightnessSpin->value();
    m_cc.contrast   = m_contrastSpin->value();
    m_cc.saturation = m_saturationSpin->value();
    m_cc.iccToSRGB  = m_iccCheck->isChecked();
    m_cc.enabled    = true; // editing the grade implies it is on
    emit changed(m_cc);
    m_commitTimer->start();
}

void CcPanel::syncFromModel()
{
    m_populating = true;
    const auto set = [](QSlider* s, QDoubleSpinBox* box, double value, double scale) {
        QSignalBlocker b1(s), b2(box);
        box->setValue(value);
        s->setValue(static_cast<int>(std::lround(value * scale)));
    };
    set(m_brightnessSlider, m_brightnessSpin, m_cc.brightness, 100.0);
    set(m_contrastSlider,   m_contrastSpin,   m_cc.contrast,   100.0);
    set(m_saturationSlider, m_saturationSpin, m_cc.saturation, 100.0);
    { QSignalBlocker b(m_iccCheck); m_iccCheck->setChecked(m_cc.iccToSRGB); }
    m_populating = false;
}
