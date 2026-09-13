#ifndef STRIPEDIT_BUBBLEPANEL_H
#define STRIPEDIT_BUBBLEPANEL_H

#include <QWidget>

#include "textartifact.h"

namespace Ui { class BubblePanel; }
class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace StripEdit {

class ShapeEditor;
class SkinEditor;
class StyleEditor;
class TailsEditor;
class TextEditor;

/**
 * @brief A named look, with nothing said in it: shape, colours, stroke, line style, font.
 *
 * Everything a bubble *is*, minus everything it *says* — no text, no box size, no tails, no placement.
 * That split is the whole idea: applying one restyles the selection without touching the lettering, and
 * placing with one active starts a new bubble from it. Persisted by dropping the content keys from
 * artifactToJson(), so a styling field added to TextArtifact joins presets without being listed twice.
 */
struct BubblePreset
{
    QString      name;
    TextArtifact artifact;
};

/**
 * @brief Tool-options panel for the strip editor's Bubble **and** Text tools.
 *
 * One panel for both, because they author the same object: the Text tool is a TextArtifact with no
 * shape (see TextArtifact). Switching tools hides the shape group rather than swapping in a second
 * panel, so there is one set of text controls, one state, and no chance of the two drifting apart.
 *
 * Follows the GradePanel contract exactly: \c setArtifact() populates without emitting; editing emits
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
    /**
     * @brief Which of the editor's two seats this panel is sitting in.
     *
     * The controls are the same either way, which is why it is one class: a bubble's colours, stroke,
     * line style and font mean the same thing whether they describe the next object or this one. What
     * differs is the *subject*, and that used to be implicit — one panel meaning "the selection" when
     * there was one and "the next placement" when there was not, with nothing on screen saying which.
     * Two seats, two instances, and the question stops being asked.
     */
    enum class Seat {
        ToolDefaults,     //!< Bottom-left, under the tool rail: what the *next* object will look like.
        ObjectProperties  //!< Right-top: what *this* object is. Inert with nothing selected.
    };

    explicit BubblePanel(Seat seat, QWidget* parent = nullptr);
    ~BubblePanel() override;

    //! Populates the controls from \p a without emitting. Pass no selection to disable the panel.
    void setArtifact(const TextArtifact& a);

    //! Greys everything out and shows the "nothing selected" hint. Only meaningful in an
    //! ObjectProperties seat — tool defaults have no selection to lose.
    void clearSelection();

    /**
     * @brief Hides the shape group for the Text tool; shows it for the Bubble tool.
     *
     * **For a panel showing a tool's options, and only that.** A panel showing the selected object must
     * never be called here: what it shows follows the object, not whichever tool happens to be active.
     * The decision belongs to the editor, which owns the surfaces — not to this widget, which must not
     * know which surface it is.
     */
    void setShapeControlsVisible(bool visible);

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusText();

    //! A fresh artifact carrying the panel's current styling — what a new placement starts from.
    [[nodiscard]] TextArtifact prototype() const;

signals:
    void changed(const TextArtifact& a);    //!< Continuous — for the live preview.
    void committed(const TextArtifact& a);  //!< Debounced / discrete — persist + undo.
    void fitRequested();                    //!< "Fit to text" — the viewer resizes the selected bubble.
    void deleteRequested();                 //!< Removes the selected artifact.

private:
    void onControlChanged();  //!< Any control moved → read into m_artifact, emit changed(), arm the timer.
    void syncFromModel();     //!< Push m_artifact into the controls with their signals blocked.

    // --- presets ---
    void loadPresets();       //!< Built-ins, then the artist's own from the application config.
    void savePresets();       //!< Writes the artist's own back; the built-ins are code, never stored.
    void refreshPresetCombo(int current);  //!< Rebuilds the list, keeping \p current selected.
    void applyPreset(const BubblePreset& p);
    void onSavePreset();
    void onDeletePreset();
    void onImportPack();
    void onExportPack();
    //! True when the current combo entry is one of the artist's own, i.e. deletable.
    [[nodiscard]] bool currentPresetIsCustom() const;

    const Seat m_seat;
    //! Shown in an ObjectProperties seat while nothing is selected, in place of controls that would
    //! otherwise look editable and silently do nothing.
    QLabel* m_emptyHint = nullptr;

    Ui::BubblePanel* ui;

    /**
     * @brief The preset picker, above everything it restyles.
     *
     * Lives in the application config rather than the workspace: restyling is a habit of the artist, not
     * a property of one comic. A *pack* — the same JSON, in a file — is how one travels to someone else,
     * which is the shareability a separate bubble-editor repository would have been built for.
     */
    QComboBox*      m_presetCombo = nullptr;
    QAction*        m_presetDelete = nullptr;
    QList<BubblePreset> m_presets;      //!< Built-ins first, then the artist's own.
    int             m_builtinCount = 0; //!< How many of m_presets are built in — those cannot be deleted.

    /**
     * @brief The two boxes the editors are arranged in.
     *
     * The Text tool hides the shape box wholesale, which is why every group that only makes sense on a
     * balloon — its tails, its surface, its line style — lives inside it.
     */
    QGroupBox* m_shapeGroup = nullptr;
    QGroupBox* m_textGroup  = nullptr;

    /**
     * @brief The five groups, each owning its own properties.
     *
     * This panel owns **none** of them. It holds the working artifact, hands it to each editor to be
     * shown, and collects the edits back through their applyTo() — which is all that is left of a class
     * that used to read and write thirteen properties by hand. When the panel itself is replaced by the
     * object-state and tool-option surfaces, these five move across unchanged, because none of them
     * knows which panel it is sitting in.
     */
    ShapeEditor* m_shape = nullptr;
    SkinEditor*  m_skin  = nullptr;
    StyleEditor* m_style = nullptr;
    TextEditor*  m_text  = nullptr;
    TailsEditor* m_tails = nullptr;

    QTimer* m_commitTimer = nullptr;

    TextArtifact m_artifact;          //!< Working copy of the selected artifact.
    bool m_populating   = false;      //!< Suppresses change signals while syncFromModel() runs.
    bool m_hasSelection = false;      //!< False → the controls are styling defaults for the next placement.
    bool m_shapeVisible = true;       //!< False (Text tool) → a preset restyles without changing shape.
};

}  // namespace StripEdit

#endif // STRIPEDIT_BUBBLEPANEL_H
