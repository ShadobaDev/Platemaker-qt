#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QStringList>

#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QDockWidget;
class QListWidgetItem;
class QMenu;
class QThread;
class Project;
class RenderWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr); //!< Constructs the main window and initializes the UI.
    ~MainWindow() override;                         //!< Destroys the main window and cleans up resources.

protected:
    /**
     * Handles the window close event.
     * @param event The close event.
     */
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Workspace menu
    void onOpenWorkspace(); //!< Opens a workspace file (JSON) from disk, replacing the current workspace.
    void onNewWorkspace();  //!< Creates a new workspace (clears the current workspace).
    void onSave();          //!< Saves the current workspace to disk (overwrites the existing file).
    void onSaveAs();        //!< Saves the current workspace to a new file (prompts for a file path).
    void onCloseWorkspace();    //!< Closes the current workspace.
    void onRevealInExplorer();  //!< Opens the system file explorer at the current workspace's directory.

    // Project panel
    void onNewProject();    //!< Prompts for a new project name and adds it to the workspace.
    /**
     * \brief Opens the dock for the double-clicked project, or brings it to front if already open.
     * It will detach projects from the list and open them in a separate dock window, 
     * or if already detached, it will bring the dock to the front.
     * @param item The list widget item that was double-clicked.
     */
    void onProjectDoubleClicked(QListWidgetItem *item);
    void onProjectsContextMenu(const QPoint &pos);  //!< Shows a context menu for the project list (rename, remove, etc.) on right click.

    // Canvas profile actions
    void onManageCanvasProfiles();      //!< Opens the canvas profile management dialog.
    void onNewCanvasProfile();          //!< Prompts for a new canvas profile and adds it to the workspace.
    void onEditActiveCanvasProfile();   //!< Opens the editor for the currently active canvas profile.

    // Output profile actions
    void onManageOutputProfiles();      //!< Opens the output profile management dialog.
    void onNewOutputProfile();          //!< Prompts for a new output profile and adds it to the workspace.
    void onEditActiveOutputProfile();   //!< Opens the editor for the currently active output profile.

    // Template actions
    void onManageTemplates();       //!< Opens the template management dialog.
    void onOpenTemplatesDir();      //!< Opens the system file explorer at the templates directory.

    // Render / processing
    void onRenderToggle(int projectIndex);   // Render/Stop from a Project widget
    /**
     * \brief Update the rendering progress for the specified project.
     * This is called by the RenderWorker to report progress back to the MainWindow.
     *
     * @param done The number of slices that have been processed so far.
     * @param total The total number of slices to process.
     * @param sliceName The name of the slice currently being processed (may be empty).
     */
    void onRenderProgress(int done, int total, QString sliceName); 

    /**
     * \brief Logs a message from the rendering process.
     * This is called by the RenderWorker to report log messages back to the MainWindow.
     * @param level The log level (e.g., info, warning, error).
     * @param message The log message.
     */
    void onRenderLog(int level, QString message);

    /**
     * \brief Called when a slice has been saved during rendering.
     * This is called by the RenderWorker to notify the MainWindow that a slice has been saved.
     * @param name The name of the slice that was saved.
     * @param fullPath The full file path where the slice was saved.
     */
    void onRenderSliceSaved(QString name, QString fullPath);

    /**
     * \brief Called when the rendering process has finished.
     * This is called by the RenderWorker to notify the MainWindow that rendering has completed.
     */
    void onRenderFinished();

private:
    // --- render orchestration ---
    void startRender(int projectIndex); //!< Starts the rendering process for the specified project index.
    void cancelRender();                //!< Cancels the ongoing rendering process, if any.
    /**
     * \brief Deletes confirmed orphan output files (m_renderOrphanCandidates) that the
     * freshly-rendered project no longer produces.
     * @param project The project for which to delete orphaned outputs.
     */
    void deleteOrphanedOutputs(const Platemaker::Models::ProjectItem &project);
    
    // --- UI helpers ---
    [[nodiscard]] Project *projectWidget(int projectIndex) const;   //!< Gets the Project widget for the specified project index, nullptr if not found.
    /**
     * \brief Resolves the output profile for the given project.
     * This function determines the effective output profile for a project, taking into account
     * the project's selected output profile and any workspace-level defaults.
     *
     * @param project The project for which to resolve the output profile.
     * @return The resolved output profile for the project.
     */
    [[nodiscard]] Platemaker::Models::OutputProfile resolveOutputProfileFor(
        const Platemaker::Models::ProjectItem &project) const;

    void setActionStatus(const QString &projectName, const QString &action);    //!< Sets the action status message in the UI for the specified project.
    void setProjectStatus(const QString &message);      //!< Sets the project status message in the UI (e.g., "Rendering...", "Finished", etc.).
    void setProgressValue(int percent, bool error);     //!< Sets the progress bar value and color (red if error is true).

    // --- Workspace helpers ---
    bool maybeSave();                           //!< True = safe to proceed
    void loadWorkspace(const QString &path);    //!< Loads a workspace from disk, replacing the current workspace
    void applyWorkspaceToUi();                  //!< Updates the UI to reflect the current workspace model
    void closeWorkspace();                      //!< Closes the current workspace, clearing the model and UI
    void setDirty(bool dirty);                  //!< Sets the dirty flag and updates the title bar. True = workspace has unsaved changes and the title bar will show '*'
    void updateTitleBar();                      //!< Updates the window title to reflect the current workspace and dirty state.

    // Captures the current workspace's serialized form as the "saved" baseline.
    // Call after every successful load/save; clears the dirty flag.
    void captureSnapshot();

    // Authoritative change check: true if the workspace differs from the last
    // captured snapshot. Robust against any action that forgot to setDirty().
    [[nodiscard]] bool isWorkspaceModified() const;

    // --- recent workspaces (advisory list in QSettings; never required) ---
    [[nodiscard]] QStringList recentWorkspaces() const;     //!< Returns a list of recently opened workspaces.
    void addToRecentWorkspaces(const QString &path);        //!< Adds a workspace path to the list of recent workspaces.
    void rebuildRecentMenu();                               //!< Rebuilds the "Open Recent Workspace" menu based on the current list of recent workspaces.
    void openRecentWorkspace(const QString &path);          //!< Opens a workspace from the recent workspaces list.
    [[nodiscard]] QString defaultDialogDir() const;         //!< Returns the default directory for file dialogs (last workspace's dir or home dir).

    // --- project management ---
    void renameProject(int modelIndex);     //!< Prompts the user to rename the project at the given model index and updates the workspace and UI accordingly.
    void removeProject(int modelIndex);     //!< Removes the project at the given model index from the workspace.
    [[nodiscard]] class QDockWidget *dockForProject(int modelIndex) const;  //!< Returns the QDockWidget for the project at the given model index, or nullptr if not found.

    // --- project dock management ---
    /**
     * \brief Opens a dock for the project at the given model index.
     * If the dock is already open, it is raised to the front.
     * @param projectIndex The index of the project in the workspace model.
     */
    void openProjectDock(int projectIndex);

    /**
     * \brief Closes the dock for the project at the given model index.
     * If the dock is not open, this function does nothing.
     * @param index The index of the project in the workspace model.
     */
    void closeProjectByIndex(int index);

    /**
     * \brief Toggles the floating state of the dock for the project at the given model index.
     * If the dock is not open, this function does nothing.
     * @param index The index of the project in the workspace model.
     */
    void toggleProjectFloatState(int index);

    // --- members ---
    static constexpr int k_maxRecentWorkspaces = 10;    //!< Maximum number of recent workspaces to track in the menu.

    Ui::MainWindow *ui;                         //!< The UI form generated by Qt Designer (ui_mainwindow.h).
    QList<QDockWidget *> m_openProjectDocks;    //!< List of currently open project docks (QDockWidget) for the workspace's projects.
    QMenu *m_recentMenu = nullptr;              //!< Submenu attached to actionOpen_recent_workspace

    Platemaker::Models::Workspace                      m_workspace; //!< The authoritative workspace model (projects, profiles, templates).
    Platemaker::Infrastructure::WorkspaceSerializer    m_serializer;//!< Serializes the workspace model to/from disk.
    QString m_workspacePath;    //!< Path of the currently loaded workspace file (empty if none).
    bool    m_dirty = false;    //!< Eager flag driving the title-bar asterisk (*)
    QString m_savedSnapshot;    //!< Serialized workspace at last load/save, used to detect unsaved changes (dirty state). Empty if no workspace is loaded.

    // UI-only selection state (not persisted in the workspace model since v2).
    // Shouldn't be used per project, or even per input file?
    QString m_activeCanvasProfileName;  //!< Name of the canvas profile currently selected in the UI (may not exist in the workspace model).
    QString m_activeOutputProfileName;  //!< Name of the output profile currently selected in the UI (may not exist in the workspace model).

    // --- render state (one render at a time, owned here) ---
    Platemaker::Infrastructure::CancellationToken m_cancelToken;    //<! Cancellation token for the current render operation, if any.
    QThread      *m_renderThread       = nullptr;   //!< Thread running the current RenderWorker (if any).
    RenderWorker *m_renderWorker        = nullptr;  //!< The current RenderWorker instance (if any).
    bool          m_rendering           = false;    //!< True if a render operation is currently in progress.
    int           m_renderProjectIndex  = -1;       //!< Index of the project currently being rendered (in m_workspace.projectItems), -1 if none.
    int           m_activeProjectIndex  = -1;       //!< Index of the project dock that was last raised (for F5/menu).

    // Outputs from the previous configuration to delete after a config-change
    // full re-render (set only when the user confirmed the cleanup prompt).
    QStringList   m_renderOrphanCandidates;         //!< List of orphaned output files from the previous configuration.
    QString       m_renderOrphanDir;                //!< Directory containing the orphaned output files.
};

#endif // MAINWINDOW_H
