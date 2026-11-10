#ifndef STRIPEDIT_PRESETSDIALOG_HPP
#define STRIPEDIT_PRESETSDIALOG_HPP

#include <QDialog>

#include "objectrecord.hpp"
#include "properties/propertygroupset.hpp"

namespace Ui {
class PresetsDialog;
}

namespace StripEdit {

class PresetStore;

/**
 * \brief *Manage presets*: the presets listed, the selected one drawn and edited with OBJECT STATE's
 *        editors, and the list's own work — duplicate, rename, delete, import, export.
 *
 * A preset is saved explicitly (*Save*, *Revert*), and leaving one with unsaved edits — another preset
 * picked, or the dialog closed — asks first. An edit to a preset changes what the next objects made from
 * it look like, and **nothing that exists**: a preset is copied into an object, never attached to it.
 *
 * The built-ins are shown and drawn but not edited, renamed or deleted; *Duplicate* makes an editable copy.
 */
class PresetsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \param store  The presets.
     * \param select The name of the preset to open on; the first one when none has it.
     */
    PresetsDialog(PresetStore& store, const QString& select, QWidget* parent = nullptr);
    ~PresetsDialog() override;

    //! Closing asks about unsaved edits first, like picking another preset does.
    void done(int result) override;

private:
    void rebuildList(const QString& select);
    void showPreset(int index);   //!< Binds the editors to the store's preset @p index.
    void onEdited();
    void refreshButtons();
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool settleEdits();   //!< Save, discard or cancel unsaved edits; false when cancelled.
    void save();
    void revert();
    void duplicate();
    void rename();
    void remove();

    PresetStore&       m_store;
    Ui::PresetsDialog* ui;         //!< The form (presetsdialog.ui). Declared before m_groups, built on its editorsHost.
    PropertyGroupSet   m_groups;   //!< OBJECT STATE's editors, every row, in the form's editorsHost.

    int          m_shown = -1;          //!< The store's index of the preset the editors show.
    ObjectRecord m_edited;              //!< Its look as the editors have it.
    bool         m_populating = false;  //!< Binding, not editing.
};

//! Asks for a pack file and reads it into @p store, saying what happened. *Tools* and the dialog both use it.
void importPresetPack(QWidget* parent, PresetStore& store);
//! Asks where to write the artist's own presets and writes them, saying what went wrong if anything did.
void exportPresetPack(QWidget* parent, const PresetStore& store);

}  // namespace StripEdit

#endif // STRIPEDIT_PRESETSDIALOG_HPP
