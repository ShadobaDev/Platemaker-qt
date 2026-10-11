#ifndef TEMPLATESDIALOG_HPP
#define TEMPLATESDIALOG_HPP

#include <QDialog>

#include <platemaker/models/workspace.hpp>

#include <QStringList>

#include <vector>

namespace Ui { class TemplatesDialog; }

/**
 * @brief Manage and (re)generate canvas-profile templates for one workspace.
 *
 * Operates on a live Workspace reference (like the Project widget). Templates (PSD or PNG, per the
 * profile's templateSettings, at its dpi) are written into a "templates" subfolder beside the
 * workspace; each profile's CanvasProfile::templates record, per format, the relative path, the
 * signature, the hash of the bytes written and a timestamp, so the dialog can show whether a template is
 * still up to date and why not. The
 * selected profile's template settings are edited here; its resolution belongs to the canvas profile.
 */
class TemplatesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief  Staleness of a canvas profile's generated template, relative to its current canvas and resolution.
     */
    enum class TemplateStatus {
        NotGenerated,   //!< No template has been generated in this format for this profile.
        UpToDate,       //!< The file is what the profile asks for, as it was written.
        Outdated,       //!< The profile changed, the file was changed elsewhere, or another profile overwrote it.
        FileMissing     //!< The tracked file is no longer on disk.
    };

    /**
     * @brief One template of a profile, checked: its status and, when it is not up to date, why — in
     *        words for the status column, its tooltip and the status bar.
     */
    struct TemplateState {
        TemplateStatus status = TemplateStatus::NotGenerated;
        QStringList    reasons;                                     //!< Most decisive first; empty when up to date.
        const Platemaker::Models::CanvasTemplateInfo* entry = nullptr; //!< The tracked file, if any (points into the profile).
    };

    /**
     * @brief Constructs the dialog and populates the template table for \p workspace.
     * @param workspace A reference to the workspace whose canvas profiles are managed.
     * @param workspaceDir The workspace's directory, used to resolve/locate the "templates" subfolder.
     * @param parent The parent widget, if any.
     */
    explicit TemplatesDialog(Platemaker::Models::Workspace& workspace,
                             const QString& workspaceDir,
                             QWidget* parent = nullptr);
    ~TemplatesDialog() override;   //!< Destroys the dialog and cleans up resources.

    /**
     * @brief Generates the template for \p cp into <workspaceDir>/templates/ as the profile asks
     * (TemplateGenerator::optionsFor: its dpi, format and empty layer), replacing that format's entry in
     * cp.templates on success (the other format's stays). The file is named after the profile, plus
     * what sets it apart from a different profile of the same name. Reused by MainWindow's
     * quick-generate button, so both give the same file.
     * @param ws The workspace supplying the default output profile used for slice-guide lines.
     * @param workspaceDir The workspace's directory (templates are written under its "templates" subfolder).
     * @param cp The canvas profile to generate a template for; its templates are updated on success.
     * @param err Filled with an error message on failure.
     * \return true on success; false on failure (cp is left untouched).
     */
    static bool generateTemplate(const Platemaker::Models::Workspace& ws,
                                 const QString& workspaceDir,
                                 Platemaker::Models::CanvasProfile& cp,
                                 QString& err);

    /**
     * @brief Checks @p cp's template in @p format against the profile and the file
     *        (Core::TemplateGenerator::check()), with the reasons put into words.
     * @param palette Every canvas profile — to notice a different profile that overwrote the same file.
     */
    static TemplateState stateOf(const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                 const Platemaker::Models::CanvasProfile& cp,
                                 const QString& workspaceDir,
                                 Platemaker::Models::TemplateFormat format);
    //! stateOf() for the format @p cp asks for — the template its row and the status bar speak about.
    static TemplateState stateOf(const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                 const Platemaker::Models::CanvasProfile& cp,
                                 const QString& workspaceDir);
    //! "Outdated — Resolution 25.4 → 300 dpi": the status and its most decisive reason.
    static QString stateText(const TemplateState& state);
    static QString statusText(TemplateStatus status);   //!< Returns the user-facing label for a TemplateStatus value.

    /**
     * @brief When the file a generation would write already exists — the profile's template in the
     * format its settings ask for — asks the user to confirm overwriting it (the message states
     * whether it is up to date or outdated). A template in the other format is written beside it.
     * @param parent The parent widget for the confirmation dialog.
     * @param cp The canvas profile whose template would be overwritten.
     * @param workspaceDir The workspace's directory, used to resolve the template's stored relative path.
     * \return true to proceed; always true when there is nothing to overwrite.
     */
    static bool confirmOverwrite(QWidget* parent,
                                 const std::vector<Platemaker::Models::CanvasProfile>& palette,
                                 const Platemaker::Models::CanvasProfile& cp,
                                 const QString& workspaceDir);

signals:
    void workspaceModified();   //!< Emitted whenever a template is generated or deleted (workspace mutated).

private slots:
    void onSelectionChanged();  //!< Slot for table selection changes. Enables/disables the Generate/Delete buttons based on the selected row's template state.
    void onGenerateSelected();  //!< Slot for the "Generate" button. Confirms overwrite if needed, generates the template for the selected profile, and refreshes the table.
    void onGenerateAll();       //!< Slot for the "Generate All" button. Generates templates for every profile that is not already up to date, then reports successes/failures.
    void onRegenerateAll();     //!< Slot for the "Regenerate all" button. Applies the selected profile's template settings to every profile, then regenerates every template.
    void onOpenFolder();        //!< Slot for the "Open Folder" button. Opens the workspace's templates folder in the system file explorer.
    void onDeleteTemplate();    //!< Slot for the "Delete" button. Confirms, then drops the selected profile's template in its current format (the file is swept at the next open).
    void onSettingsEdited();    //!< Slot for the format combo / empty-layer box: writes the selected profile's templateSettings.

private:
    void rebuildTable();                                //!< Rebuilds tableTemplates from m_workspace.canvasProfiles(), preserving the current selection where possible.
    void syncSettings();                                //!< Shows the selected profile's template settings and resolution (disabled with no selection).
    void generateMany(bool onlyOutdated);               //!< Generates every profile's template (or only those not up to date), writes the results back and reports.
    int  selectedRow() const;                           //!< Returns the currently selected table row, or -1 if none is selected.
    Ui::TemplatesDialog* ui;                    //!< Qt Designer-generated UI for this dialog.
    Platemaker::Models::Workspace& m_workspace; //!< Reference to the workspace whose canvas profiles/templates are managed.
    QString m_workspaceDir;                     //!< The workspace's directory, used to resolve the "templates" subfolder.
    bool m_syncing = false;                     //!< True while syncSettings() fills the controls, so their signals are not edits.
};

#endif // TEMPLATESDIALOG_HPP
