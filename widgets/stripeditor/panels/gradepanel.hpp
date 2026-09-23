#ifndef STRIPEDIT_GRADEPANEL_HPP
#define STRIPEDIT_GRADEPANEL_HPP

#include <QWidget>

#include <platemaker/models/colour_correction.hpp>

#include "colouradjustment.hpp"

namespace Ui { class GradePanel; }
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTimer;

namespace StripEdit {

/**
 * @brief The Grade tool's options — an image editor's colour menu, applied to the selected object.
 *
 * A list of adjustments at the top, the way an image editor's colour menu lists them, and the chosen one's
 * controls below it.
 * Controls apply live and settle into one undo step, named for the adjustment that was edited, rather than
 * waiting behind an OK button: the preview already is the result, and every settled edit is already
 * undoable. An adjustment the grade applies is shown in bold.
 *
 * **What it applies to is the selection**, and today that can only be the strip: the library holds one
 * grade per chapter, and a page's only colour decision is whether that grade skips it (Q65). With anything
 * else selected the panel stays visible, says why it cannot act, and offers the way to the strip — a tool
 * that silently does nothing is worse than one that explains itself.
 *
 * The panel works on a whole `ColourCorrection` and edits only the fields of the adjustment in front of it;
 * curves and page exclusions pass through untouched.
 */
class GradePanel : public QWidget
{
    Q_OBJECT

public:
    //! What the selection is, as far as a grade is concerned.
    enum class Target {
        Strip,   //!< Something a grade can be applied to.
        Page,    //!< Takes the strip's grade, or is excluded from it — decided in its own properties.
        Other,   //!< Nothing, or an overlay.
    };

    explicit GradePanel(QWidget* parent = nullptr);
    ~GradePanel() override;

    //! Populate the controls from \p cc without emitting change signals.
    void setColourCorrection(const Platemaker::Models::ColourCorrection& cc);

    //! Enables the panel for the strip; for anything else, disables it and says why.
    void setTarget(Target target);

    //! Shows @p a's controls — how *Edit* on an applied adjustment, elsewhere, reopens it here.
    void openAdjustment(ColourAdjustment a);

signals:
    void changed(const Platemaker::Models::ColourCorrection& cc);   //!< Continuous — for the live preview.
    //! Settled or discrete — persist and record, as a step named @p undoText.
    void committed(const Platemaker::Models::ColourCorrection& cc, const QString& undoText);
    //! The panel cannot act on the selection, and the artist asked to go to what it can act on.
    void selectStripRequested();

private:
    void onControlChanged(ColourAdjustment edited);   //!< Read that adjustment's controls, preview, arm the commit.
    void syncFromModel();                              //!< Push m_cc into every control with its signals blocked.
    void refreshList();                                //!< Bold for what is applied, with its values in the tooltip.
    [[nodiscard]] ColourAdjustment currentAdjustment() const;

    Ui::GradePanel*    ui;
    QLabel*         m_unavailable      = nullptr;   //!< Why the panel cannot act on the selection.
    QPushButton*    m_toStrip          = nullptr;   //!< ...and the way to something it can act on.
    QListWidget*    m_list             = nullptr;   //!< The adjustments, as an image editor's menu lists them.
    QGroupBox*      m_controls         = nullptr;   //!< The chosen adjustment, titled with its name.
    QStackedWidget* m_pages            = nullptr;   //!< One page of controls per list entry, in the same order.
    QSlider*        m_brightnessSlider = nullptr;
    QDoubleSpinBox* m_brightnessSpin   = nullptr;
    QSlider*        m_contrastSlider   = nullptr;
    QDoubleSpinBox* m_contrastSpin     = nullptr;
    QSlider*        m_saturationSlider = nullptr;
    QDoubleSpinBox* m_saturationSpin   = nullptr;
    QTimer*         m_commitTimer      = nullptr;

    Platemaker::Models::ColourCorrection m_cc;   //!< Working grade; only the shown adjustment's fields are edited.
    ColourAdjustment m_lastEdited = ColourAdjustment::BrightnessContrast;   //!< Names the pending commit.
    bool m_populating = false;                   //!< Suppresses change signals while syncFromModel() runs.
};

}  // namespace StripEdit

#endif // STRIPEDIT_GRADEPANEL_HPP
