#ifndef STRIPEDIT_BUBBLEPANEL_H
#define STRIPEDIT_BUBBLEPANEL_H

#include <QWidget>

#include "textartifact.h"
#include "propertygroupset.h"

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
 * @brief Tool options for the Bubble and Text tools: **what the next object will be**.
 *
 * One panel for both, because they author the same object — the Text tool is a TextArtifact with no
 * shape. Switching between them hides the shape group rather than swapping in a second panel, so there
 * is one set of controls and no chance of two drifting apart.
 *
 * It describes an object that **does not exist yet**, so it edits nothing and emits nothing. What *is*
 * selected is `ObjectStatePanel`'s business, and the two no longer share a class: they answer different
 * questions and the artist can see which is which.
 */
class BubblePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BubblePanel(QWidget* parent = nullptr);
    ~BubblePanel() override;

    /**
     * @brief Hides the shape group for the Text tool; shows it for the Bubble tool.
     *
     * A tool's options follow the tool, which is exactly what a panel describing a *selected object*
     * must never do.
     */
    void setShapeControlsVisible(bool visible);

    //! A fresh artifact carrying the panel's current styling — what a new placement starts from.
    [[nodiscard]] TextArtifact prototype() const;

private:
    void onControlChanged();  //!< Any control moved → read it into the prototype's working values.
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
     * This panel owns none of them, and neither does the one describing the selected object. Both hold
     * a set, so the two rules about applying all five — shape before tails, and the seed afterwards —
     * are written once (see PropertyGroupSet).
     */
    PropertyGroupSet m_groups;

    TextArtifact m_artifact;          //!< The next placement's working values.
    bool m_populating   = false;      //!< Suppresses change signals while syncFromModel() runs.
    bool m_shapeVisible = true;       //!< False (Text tool) → a preset restyles without changing shape.
};

}  // namespace StripEdit

#endif // STRIPEDIT_BUBBLEPANEL_H
