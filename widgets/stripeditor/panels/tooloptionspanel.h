#ifndef STRIPEDIT_TOOLOPTIONSPANEL_H
#define STRIPEDIT_TOOLOPTIONSPANEL_H

#include <QWidget>

#include "propertygroupset.h"
#include "tailseditor.h"

class QAction;
class QComboBox;
class QGroupBox;

namespace StripEdit {

class PresetStore;

/**
 * @brief Options for the Bubble and Text tools: **what the next object will be**.
 *
 * One panel for both, because they author the same object — the Text tool is a `TextArtifact` with no
 * shape. Switching between them hides the shape group rather than swapping in a second panel, so there
 * is one set of controls and no chance of two drifting apart.
 *
 * It describes an object that **does not exist yet**, so it edits nothing and emits nothing. There is no
 * text to type into it, no tail to add to something that is not there, and nothing to delete. What *is*
 * selected belongs to `ObjectStatePanel`, which is a different class answering a different question —
 * the arrangement that stops either of them meaning two things at once.
 *
 * The preset picker lives here because a preset is what the next object starts from. Applying one to an
 * object that already exists is a different act, and it has its own place: the object's context menu.
 */
class ToolOptionsPanel : public QWidget
{
    Q_OBJECT

public:
    ToolOptionsPanel(PresetStore& presets, QWidget* parent = nullptr);

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
    void onControlChanged();      //!< A group reported an edit → read it into the prototype's values.
    void syncFromModel();         //!< Push the prototype's values into the controls.
    void refreshPresetCombo(int current);
    void applyPreset(int index);
    void onSavePreset();
    void onDeletePreset();
    void onImportPack();
    void onExportPack();

    PresetStore& m_presets;

    QComboBox* m_presetCombo  = nullptr;
    QAction*   m_presetDelete = nullptr;
    QGroupBox* m_shapeGroup   = nullptr;
    QGroupBox* m_textGroup    = nullptr;

    PropertyGroupSet m_groups;
    //! The next balloon's tail. Not in the set: an existing balloon's tails are edited as objects instead.
    TailsEditor*     m_tails = nullptr;

    TextArtifact m_artifact;       //!< The next placement's working values.
    bool m_populating   = false;   //!< Suppresses change signals while binding.
    bool m_shapeVisible = true;    //!< False (Text tool) → a preset restyles without changing shape.
};

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLOPTIONSPANEL_H
