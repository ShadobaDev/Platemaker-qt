#ifndef STRIPEDIT_GRADEPANEL_H
#define STRIPEDIT_GRADEPANEL_H

#include <QWidget>

#include <platemaker/models/processing_steps.hpp>

namespace Ui { class GradePanel; }
class QSlider;
class QDoubleSpinBox;
class QTimer;

namespace StripEdit {

/**
 * @brief Tool-options panel for the strip editor's Colour-correction (Grade) tool.
 *
 * Brightness / contrast / saturation (slider + spin-box each), plus a reset.
 * The panel owns a working \c ColourCorrection: \c setColourCorrection() populates the controls without
 * emitting; editing emits \c changed() continuously (for the live preview) and \c committed() once the
 * controls settle (debounced) or on a discrete action (for the persisted, undoable write).
 *
 * Curves and per-page exclusions are not exposed here yet — a later sub-step. The panel preserves those
 * fields of the working grade untouched.
 */
class GradePanel : public QWidget
{
    Q_OBJECT

public:
    explicit GradePanel(QWidget* parent = nullptr);
    ~GradePanel() override;

    //! Populate the controls from \p cc without emitting change signals.
    void setColourCorrection(const Platemaker::Models::ColourCorrection& cc);

signals:
    void changed(const Platemaker::Models::ColourCorrection& cc);   //!< Continuous — for the live preview.
    void committed(const Platemaker::Models::ColourCorrection& cc); //!< Debounced / discrete — persist + undo.

private:
    void onControlChanged(); //!< Any control moved → read into m_cc, emit changed(), arm the commit timer.
    void syncFromModel();     //!< Push m_cc into the controls with their signals blocked.

    Ui::GradePanel*    ui;
    QSlider*        m_brightnessSlider = nullptr;
    QDoubleSpinBox* m_brightnessSpin   = nullptr;
    QSlider*        m_contrastSlider   = nullptr;
    QDoubleSpinBox* m_contrastSpin     = nullptr;
    QSlider*        m_saturationSlider = nullptr;
    QDoubleSpinBox* m_saturationSpin   = nullptr;
    QTimer*         m_commitTimer      = nullptr;

    Platemaker::Models::ColourCorrection m_cc; //!< Working grade (scalars edited here; curves/exclusions preserved).
    bool m_populating = false;                 //!< Suppresses change signals while syncFromModel() runs.
};

}  // namespace StripEdit

#endif // STRIPEDIT_GRADEPANEL_H
