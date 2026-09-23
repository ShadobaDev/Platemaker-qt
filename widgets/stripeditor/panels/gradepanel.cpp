#include "gradepanel.h"
#include "ui_gradepanel.h"

#include <QDoubleSpinBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "propertygroupeditor.h"   // k_commitDebounceMs — ③ and ④ settle at the same pace or they
                                   // feel like different applications

namespace StripEdit {

namespace {
constexpr double k_sliderScale      = 100.0;  //!< Slider steps per unit — the spin box's two decimals.
constexpr int    k_spinMinWidth     = 84;     //!< Room for "-1,00" plus the step arrows.
constexpr int    k_gridSpacing      = 6;
}

GradePanel::GradePanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::GradePanel)
{
    ui->setupUi(this); // provides the empty verticalLayout container; the controls are built here

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] {
        emit committed(m_cc, tr("Adjust %1").arg(colourAdjustmentName(m_lastEdited)));
    });

    // --- when the selection is not something a grade can be applied to ---------------------------------
    m_unavailable = new QLabel(this);
    m_unavailable->setWordWrap(true);
    m_toStrip = new QPushButton(tr("Select the strip"), this);
    connect(m_toStrip, &QPushButton::clicked, this, &GradePanel::selectStripRequested);
    ui->verticalLayout->addWidget(m_unavailable);
    ui->verticalLayout->addWidget(m_toStrip);

    // --- the adjustments, one list entry and one page of controls each, in the same order ---------------
    m_list  = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pages = new QStackedWidget(this);

    const auto addPage = [this](ColourAdjustment a) {
        auto* page = new QWidget(m_pages);
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(k_gridSpacing);
        grid->setColumnStretch(1, 1);
        m_pages->addWidget(page);
        auto* item = new QListWidgetItem(colourAdjustmentName(a), m_list);
        item->setData(Qt::UserRole, static_cast<int>(a));
        return grid;
    };

    // One row = label | slider | spin box, the slider and the spin box mirroring each other. Every control
    // belongs to one adjustment, and moving it edits that adjustment's fields and nothing else.
    const auto addRow = [this](QGridLayout* grid, int row, const QString& label, QSlider*& slider,
                               QDoubleSpinBox*& spin, double min, double max, ColourAdjustment owner) {
        grid->addWidget(new QLabel(label, this), row, 0);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(static_cast<int>(std::lround(min * k_sliderScale)),
                         static_cast<int>(std::lround(max * k_sliderScale)));
        grid->addWidget(slider, row, 1);
        spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(0.01);
        spin->setDecimals(2);
        spin->setMinimumWidth(k_spinMinWidth);
        grid->addWidget(spin, row, 2);

        connect(slider, &QSlider::valueChanged, this, [this, spin, owner](int v) {
            if (m_populating) return;
            const QSignalBlocker block(spin);
            spin->setValue(v / k_sliderScale);
            onControlChanged(owner);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, owner](double v) {
            if (m_populating) return;
            const QSignalBlocker block(slider);
            slider->setValue(static_cast<int>(std::lround(v * k_sliderScale)));
            onControlChanged(owner);
        });
    };

    // Curves are not listed: the library runs them, but there is no editor for them here yet.
    QGridLayout* bc = addPage(ColourAdjustment::BrightnessContrast);
    addRow(bc, 0, tr("Brightness"), m_brightnessSlider, m_brightnessSpin, -1.0, 1.0,
           ColourAdjustment::BrightnessContrast);
    addRow(bc, 1, tr("Contrast"), m_contrastSlider, m_contrastSpin, 0.0, 2.0,
           ColourAdjustment::BrightnessContrast);
    QGridLayout* sat = addPage(ColourAdjustment::Saturation);
    addRow(sat, 0, tr("Saturation"), m_saturationSlider, m_saturationSpin, 0.0, 2.0,
           ColourAdjustment::Saturation);

    // A short list, so no scrolling area pretending there might be more.
    m_list->setMaximumHeight(m_list->sizeHintForRow(0) * m_list->count() + 2 * m_list->frameWidth());

    m_controls = new QGroupBox(this);
    auto* groupLay = new QVBoxLayout(m_controls);
    groupLay->addWidget(m_pages);
    auto* reset = new QPushButton(tr("Reset"), m_controls);
    reset->setToolTip(tr("Takes this adjustment off the strip. The others stay, and so do page exclusions."));
    groupLay->addWidget(reset);

    // Reset is removal: a neutral adjustment is one that is not there. It is the step on its own, so a
    // slider commit still pending would only name it wrongly.
    connect(reset, &QPushButton::clicked, this, [this] {
        const ColourAdjustment a = currentAdjustment();
        m_cc = withoutColourAdjustment(m_cc, a);
        syncFromModel();
        refreshList();
        m_commitTimer->stop();
        emit changed(m_cc);
        emit committed(m_cc, tr("Reset %1").arg(colourAdjustmentName(a)));
    });

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        m_pages->setCurrentIndex(row);
        m_controls->setTitle(m_list->item(row)->text());
    });

    ui->verticalLayout->addWidget(m_list);
    ui->verticalLayout->addWidget(m_controls);
    ui->verticalLayout->addStretch(1);

    m_list->setCurrentRow(0);
    syncFromModel();
    refreshList();
    setTarget(Target::Other);
}

GradePanel::~GradePanel()
{
    delete ui;
}

void GradePanel::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    m_cc = cc;
    syncFromModel();
    refreshList();
}

void GradePanel::setTarget(Target target)
{
    const bool usable = target == Target::Strip;
    m_list->setEnabled(usable);
    m_controls->setEnabled(usable);
    m_unavailable->setVisible(!usable);
    m_toStrip->setVisible(!usable);
    m_unavailable->setText(target == Target::Page
        ? tr("A page takes the strip's colour correction. Select the strip to adjust it — or exclude "
             "this page from it in the page's properties.")
        : tr("Colour adjustments apply to the strip. Select it to adjust its colours."));
}

void GradePanel::openAdjustment(ColourAdjustment a)
{
    for (int r = 0; r < m_list->count(); ++r) {
        if (static_cast<ColourAdjustment>(m_list->item(r)->data(Qt::UserRole).toInt()) == a) {
            m_list->setCurrentRow(r);
            return;
        }
    }
}

ColourAdjustment GradePanel::currentAdjustment() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item ? static_cast<ColourAdjustment>(item->data(Qt::UserRole).toInt())
                : ColourAdjustment::BrightnessContrast;
}

void GradePanel::onControlChanged(ColourAdjustment edited)
{
    switch (edited) {
    case ColourAdjustment::BrightnessContrast:
        m_cc.brightness = m_brightnessSpin->value();
        m_cc.contrast   = m_contrastSpin->value();
        break;
    case ColourAdjustment::Saturation:
        m_cc.saturation = m_saturationSpin->value();
        break;
    case ColourAdjustment::Curves:
        break;   // no controls here
    }
    m_lastEdited = edited;
    refreshList();
    emit changed(m_cc);
    m_commitTimer->start();
}

void GradePanel::syncFromModel()
{
    m_populating = true;
    const auto set = [](QSlider* s, QDoubleSpinBox* box, double value) {
        const QSignalBlocker b1(s), b2(box);
        box->setValue(value);
        s->setValue(static_cast<int>(std::lround(value * k_sliderScale)));
    };
    set(m_brightnessSlider, m_brightnessSpin, m_cc.brightness);
    set(m_contrastSlider,   m_contrastSpin,   m_cc.contrast);
    set(m_saturationSlider, m_saturationSpin, m_cc.saturation);
    m_populating = false;
}

void GradePanel::refreshList()
{
    for (int r = 0; r < m_list->count(); ++r) {
        QListWidgetItem* item = m_list->item(r);
        const auto a       = static_cast<ColourAdjustment>(item->data(Qt::UserRole).toInt());
        const bool applied = isColourAdjustmentApplied(m_cc, a);
        QFont f = m_list->font();
        f.setBold(applied);
        item->setFont(f);
        item->setToolTip(applied ? tr("Applied — %1").arg(colourAdjustmentValues(m_cc, a))
                                 : tr("Not applied"));
    }
}

}  // namespace StripEdit
