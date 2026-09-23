#ifndef STRIPEDIT_TOOLOPTIONSPANEL_HPP
#define STRIPEDIT_TOOLOPTIONSPANEL_HPP

#include <optional>

#include <QWidget>

#include "propertygroupset.hpp"
#include "tailseditor.hpp"

class QAction;
class QComboBox;
class QGroupBox;

namespace StripEdit {

class PresetStore;

/**
 * @brief Options for the Bubble and Text tools: **what the next object will be**.
 *
 * One panel for both, because they author the same object — the Text tool is an `Artifact` with no
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
     * @brief The shape the active tool places — or no value, when the artist picks it here.
     *
     * The Bubble tool passes no value and the shape tiles are shown; the Text tool passes `Shape::None`
     * and the Caption tool `Shape::Caption`, and the tiles go away, because a tool that places one shape
     * has already answered that question. prototype() then applies it **over** the artist's pick without
     * disturbing it, so switching back to the Bubble tool brings back the shape that was chosen before.
     *
     * A tool's options follow the tool, which is exactly what a panel describing a *selected object*
     * must never do.
     */
    void setToolShape(std::optional<Artifact::Shape> shape);

    //! A fresh artifact carrying the panel's current styling — what a new placement starts from.
    [[nodiscard]] Artifact prototype() const;

    /**
     * @brief The silhouette the tiles are set to — **the artist's own pick, never the active tool's**.
     *
     * What *Convert to ▸ Balloon* arrives at, and what the Bubble tool places. Never `None`: the tiles
     * no longer offer "no balloon", because that is a kind rather than a silhouette, so a `None` here
     * could only come from a settings file written before that was true.
     */
    [[nodiscard]] Artifact::Shape balloonShape() const;

private:
    void onControlChanged();      //!< A group reported an edit → read it into the prototype's values.
    void syncFromModel();         //!< Push the prototype's values into the controls.
    void refreshPresetCombo();
    void applyPreset(int index);
    void onSavePreset();
    void onDeletePreset();
    void onImportPack();
    void onExportPack();

    PresetStore& m_presets;

    /**
     * @brief Re-decides what *Delete* acts on: the preset these values **are**, if they are one.
     *
     * There is no chip here. ③ wears one because a selected object's look is a question the panel
     * cannot otherwise answer; here every property a preset covers is on screen a few points below, so
     * a chip would report what the controls already say — and sitting beside the picker it read as a
     * second control answering the same question, which is the fault this panel was rearranged to fix.
     */
    void refreshLook();

    QComboBox* m_presetCombo  = nullptr;
    QAction*   m_presetDelete = nullptr;
    QGroupBox* m_shapeGroup   = nullptr;
    QGroupBox* m_textGroup    = nullptr;

    PropertyGroupSet m_groups;
    //! The next balloon's tail. Not in the set: an existing balloon's tails are edited as objects instead.
    TailsEditor*     m_tails = nullptr;

    Artifact m_artifact;       //!< The next placement's working values.
    bool m_populating   = false;   //!< Suppresses change signals while binding.
    //! The active tool's fixed shape, if it has one → the tiles are hidden and a preset restyles
    //! without changing shape, because the shape is the tool's to say.
    std::optional<Artifact::Shape> m_toolShape;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLOPTIONSPANEL_HPP
