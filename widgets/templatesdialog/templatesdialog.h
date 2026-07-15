#ifndef TEMPLATESDIALOG_H
#define TEMPLATESDIALOG_H

#include <QDialog>

#include <platemaker/models/workspace.hpp>

namespace Ui { class TemplatesDialog; }

/**
 * @brief Manage and (re)generate canvas-profile templates for one workspace.
 *
 * Operates on a live Workspace reference (like the Project widget). Template
 * PNGs are written into a "templates" subfolder beside the workspace; each
 * profile's CanvasProfile::templateInfo records the relative path, a canvas-only
 * fingerprint, and a timestamp so the dialog can show whether a template is still
 * up to date.
 */
class TemplatesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief  Staleness of a canvas profile's generated template PNG, relative to its current dimensions.
     */
    enum class TemplateStatus {
        NotGenerated,   //!< No template has ever been generated for this profile.
        UpToDate,       //!< Generated template's fingerprint matches the profile's current canvas.
        Outdated,       //!< Template exists but the profile's canvas changed since it was generated.
        FileMissing     //!< templateInfo points at a file that is no longer on disk.
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
     * @brief Generates the template for \p cp into <workspaceDir>/templates/, updating
     * cp.templateInfo on success. Reused by MainWindow's quick-generate button.
     * @param ws The workspace supplying the default output profile used for slice-guide lines.
     * @param workspaceDir The workspace's directory (templates are written under its "templates" subfolder).
     * @param cp The canvas profile to generate a template for; its templateInfo is updated on success.
     * @param err Filled with an error message on failure.
     * \return true on success; false on failure (cp is left untouched).
     */
    static bool generateTemplate(const Platemaker::Models::Workspace& ws,
                                 const QString& workspaceDir,
                                 Platemaker::Models::CanvasProfile& cp,
                                 QString& err);

    /**
     * @brief Canvas-only staleness check for an already-generated template.
     * @param cp The canvas profile to check.
     * @param workspaceDir The workspace's directory, used to resolve the template's stored relative path.
     * \return The template's current TemplateStatus.
     */
    static TemplateStatus statusOf(const Platemaker::Models::CanvasProfile& cp,
                                   const QString& workspaceDir);

    /**
     * @brief When a template file already exists for \p cp, asks the user to confirm
     * overwriting it (the message states whether it is up to date or outdated).
     * @param parent The parent widget for the confirmation dialog.
     * @param cp The canvas profile whose template would be overwritten.
     * @param workspaceDir The workspace's directory, used to resolve the template's stored relative path.
     * \return true to proceed; always true when there is nothing to overwrite.
     */
    static bool confirmOverwrite(QWidget* parent,
                                 const Platemaker::Models::CanvasProfile& cp,
                                 const QString& workspaceDir);

signals:
    void workspaceModified();   //!< Emitted whenever a template is generated or deleted (workspace mutated).

private slots:
    void onSelectionChanged();  //!< Slot for table selection changes. Enables/disables the Generate/Delete buttons based on the selected row's template state.
    void onGenerateSelected();  //!< Slot for the "Generate" button. Confirms overwrite if needed, generates the template for the selected profile, and refreshes the table.
    void onGenerateAll();       //!< Slot for the "Generate All" button. Generates templates for every profile that is not already up to date, then reports successes/failures.
    void onOpenFolder();        //!< Slot for the "Open Folder" button. Opens the workspace's templates folder in the system file explorer.
    void onDeleteTemplate();    //!< Slot for the "Delete" button. Confirms and deletes the selected profile's template file, then clears its templateInfo.

private:
    void rebuildTable();                                //!< Rebuilds tableTemplates from m_workspace.canvasProfiles, preserving the current selection where possible.
    int  selectedRow() const;                           //!< Returns the currently selected table row, or -1 if none is selected.
    static QString statusText(TemplateStatus status);   //!< Returns the user-facing label for a TemplateStatus value.

    Ui::TemplatesDialog* ui;                    //!< Qt Designer-generated UI for this dialog.
    Platemaker::Models::Workspace& m_workspace; //!< Reference to the workspace whose canvas profiles/templates are managed.
    QString m_workspaceDir;                     //!< The workspace's directory, used to resolve the "templates" subfolder.
};

#endif // TEMPLATESDIALOG_H
