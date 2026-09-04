#ifndef BUBBLEPANEL_H
#define BUBBLEPANEL_H

#include <QWidget>

#include "textartifact.h"

namespace Ui { class BubblePanel; }
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QGroupBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

/**
 * @brief Tool-options panel for the strip editor's Bubble **and** Text tools.
 *
 * One panel for both, because they author the same object: the Text tool is a TextArtifact with no
 * shape (see TextArtifact). Switching tools hides the shape group rather than swapping in a second
 * panel, so there is one set of text controls, one state, and no chance of the two drifting apart.
 *
 * Follows the CcPanel contract exactly: \c setArtifact() populates without emitting; editing emits
 * \c changed() continuously (live preview) and \c committed() once the controls settle (debounced) or
 * on a discrete action (persisted, one undo step).
 *
 * Text is edited **here**, not with a caret on the strip. That is a deliberate simplification — an
 * in-scene editor means reimplementing selection, carets and IME on a QGraphicsItem — and it costs
 * nothing in liveness: the strip redraws on every keystroke either way.
 */
class BubblePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BubblePanel(QWidget* parent = nullptr);
    ~BubblePanel() override;

    //! Populates the controls from \p a without emitting. Pass no selection to disable the panel.
    void setArtifact(const TextArtifact& a);

    //! Greys everything out and shows the "nothing selected" hint.
    void clearSelection();

    //! Hides the shape group for the Text tool; shows it for the Bubble tool.
    void setShapeControlsVisible(bool visible);

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusText();

    //! The shape the next placed bubble should use (the picker's current value).
    [[nodiscard]] TextArtifact::Shape currentShape() const;

    //! A fresh artifact carrying the panel's current styling — what a new placement starts from.
    [[nodiscard]] TextArtifact prototype() const;

signals:
    void changed(const TextArtifact& a);    //!< Continuous — for the live preview.
    void committed(const TextArtifact& a);  //!< Debounced / discrete — persist + undo.
    void fitRequested();                    //!< "Fit to text" — the viewer resizes the selected bubble.
    void deleteRequested();                 //!< Removes the selected artifact.

protected:
    //! Re-renders the shape tiles when the theme flips — they are drawn in the palette's colours.
    void changeEvent(QEvent* e) override;

private:
    void onControlChanged();  //!< Any control moved → read into m_artifact, emit changed(), arm the timer.
    void syncFromModel();     //!< Push m_artifact into the controls with their signals blocked.
    void pickColour(QColor& target, QPushButton* swatch);
    void paintSwatch(QPushButton* swatch, const QColor& c);
    void refreshShapeTiles();   //!< (Re)draws each shape tile's icon from the rasteriser.

    Ui::BubblePanel* ui;

    QGroupBox*      m_shapeGroup  = nullptr;
    /**
     * @brief The shape picker: one checkable tile per shape, laid out like the editor's tool rail.
     *
     * A grid of previews rather than a drop-down, because a bubble shape is a *look* — a name in a list
     * makes you open it to find out what it is. Each tile's icon is produced by the same rasteriser that
     * draws the bubble, so the tile is a true miniature of what placing it gives you.
     */
    QButtonGroup*   m_shapeTiles  = nullptr;
    QCheckBox*      m_tailCheck   = nullptr;
    QPushButton*    m_fillSwatch  = nullptr;
    QPushButton*    m_strokeSwatch= nullptr;
    QSpinBox*       m_strokeWidth = nullptr;

    QGroupBox*      m_textGroup   = nullptr;
    QPlainTextEdit* m_textEdit    = nullptr;
    QFontComboBox*  m_fontCombo   = nullptr;
    QSpinBox*       m_fontSize    = nullptr;
    QCheckBox*      m_boldCheck   = nullptr;
    QComboBox*      m_alignCombo  = nullptr;
    QPushButton*    m_textSwatch  = nullptr;

    QTimer* m_commitTimer = nullptr;

    TextArtifact m_artifact;          //!< Working copy of the selected artifact.
    bool m_populating   = false;      //!< Suppresses change signals while syncFromModel() runs.
    bool m_hasSelection = false;      //!< False → the controls are styling defaults for the next placement.
};

#endif // BUBBLEPANEL_H
