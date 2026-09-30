#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "artifact.hpp"

#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QDockWidget;
class QListWidgetItem;
class QMenu;
class QTabBar;
class QThread;
class QUndoGroup;
class QUndoStack;
class Advisories;
class AdvisoryBar;
class Project;
class RenderWorker;
class WorkspaceLock;
namespace StripEdit { class PresetStore; }

/**
 * @brief The MainWindow class represents the main application window of Platemaker.
 * It manages the workspace, projects, canvas profiles, output profiles, templates,
 * and rendering processes. It provides a user interface for interacting with these
 * components and orchestrates their behavior.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr); //!< Constructs the main window and initializes the UI.
    ~MainWindow() override;                         //!< Destroys the main window and cleans up resources.

    /**
     * @brief Restores workspace-level metadata (profiles + project names) from a
     *        WorkspaceEditor::snapshotMeta string and refreshes every view. Called by
     *        WorkspaceSnapshotCommand on undo/redo. Project *contents* are untouched (they live on
     *        each project's own undo stack).
     */
    void applyWorkspaceSnapshot(const QString& snapshot);

protected:
    /**
     * Handles the window close event.
     * @param event The close event.
     */
    void closeEvent(QCloseEvent *event) override;

    //! Clamps the Action column's applied width to the current window (never more than half of it) so a
    //! width chosen while maximized doesn't leave the panel oversized after the window shrinks. The chosen
    //! width (m_actionDockWidth) is kept, so re-maximizing restores it. setFixedWidth still does the actual
    //! "never grows on resize" job; this only shrinks it to fit.
    void resizeEvent(QResizeEvent *event) override;

    //! Handles the Action column's manual resize: mouse drags on m_actionGrip (its left-edge handle) grow
    //! or shrink the fixed-width panel, updating the chosen width m_actionDockWidth.
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    //! Emitted after a workspace-level profile edit (canvas/output Manage/New/Edit). Every open
    //! Project dock connects this to Project::refreshProfileViews() so their palette-derived views
    //! (output combo, assigned-canvas list, format controls) update at once, without a manual
    //! iteration over the open docks.
    void workspaceProfilesChanged();

private slots:
    // File menu
    void onOpenWorkspace();     //!< Opens a workspace file (JSON) from disk, replacing the current workspace.
    void onNewWorkspace();      //!< Creates a new workspace (clears the current workspace).
    void onSave();              //!< Saves the current workspace to disk (overwrites the existing file).
    void onSaveAs();            //!< Saves the current workspace to a new file (prompts for a file path).
    void onCloseWorkspace();    //!< Closes the current workspace.
    void onRevealInExplorer();  //!< Opens the system file explorer at the current workspace's directory.

    //--- Project panel
    void onNewProject();    //!< Prompts for a new project name and adds it to the workspace.
    
    /**
     * @brief Opens the dock for the double-clicked project, or brings it to front if already open.
     * It will detach projects from the list and open them in a separate dock window, 
     * or if already detached, it will bring the dock to the front.
     * @param item The list widget item that was double-clicked.
     */
    void onProjectDoubleClicked(QListWidgetItem *item); //!< Handle attach/detach action of porject dock window
    void onProjectsContextMenu(const QPoint &pos);      //!< Shows a context menu for the project list (rename, remove, etc.) on right click.
    void onActionLogContextMenu(const QPoint &pos);     //!< Shows the render-log context menu (Copy/Select All + Save log as… / Clear) on right click.

    // Canvas profile actions
    void onManageCanvasProfiles();      //!< Opens the canvas profile management dialog.
    void onNewCanvasProfile();          //!< Prompts for a new canvas profile and adds it to the workspace.
    void onEditActiveCanvasProfile();   //!< Opens the editor for the currently active canvas profile.

    // Output profile actions
    void onManageOutputProfiles();      //!< Opens the output profile management dialog.
    void onNewOutputProfile();          //!< Prompts for a new output profile and adds it to the workspace.
    void onEditActiveOutputProfile();   //!< Opens the editor for the currently active output profile.

    // Tools
    void onImportBubblePresets();   //!< Imports a bubble-preset pack into the application's presets.
    void onExportBubblePresets();   //!< Exports the artist's own bubble presets as a pack.
    void onExportPackage();         //!< *File → Export package…*: the workspace and every file it uses, one zip.
    void onOpenPackage();           //!< *File → Open package…*: unpacked into a new folder, then opened.
    void onFonts();                 //!< *Tools → Fonts…*: the workspace's own fonts, the missing ones, Add font….

    // Template actions
    void onManageTemplates();       //!< Opens the template management dialog.
    void onOpenTemplatesDir();      //!< Opens the system file explorer at the templates directory.

    // About menu — all open the AboutDialog on the matching tab (see about.cpp).
    void onShowVersion();   //!< Opens the About dialog on the About/Version tab.
    void onShowAuthors();   //!< Opens the About dialog on the Authors tab.
    void onShowHelp();      //!< Opens the About dialog on the Manual tab (mock).

    // Render / processing
    void onRenderToggle(int projectIndex);   //!< Render/Stop from a Project widget

    /**
     * @brief Sweeps every project in the workspace, rendering the ones that need it (F6).
     *
     * Projects are rendered one at a time; see the batch helpers in renderbatch.cpp for
     * why this is sequential rather than parallel.
     */
    void onRefreshAllProjects();

    /**
     * @brief Update the rendering progress for the specified project.
     * This is called by the RenderWorker to report progress back to the MainWindow.
     *
     * @param done The number of slices that have been processed so far.
     * @param total The total number of slices to process.
     * @param sliceName The name of the slice currently being processed (may be empty).
     */
    void onRenderProgress(int done, int total, QString sliceName); 

    /**
     * @brief Logs a message from the rendering process.
     * This is called by the RenderWorker to report log messages back to the MainWindow.
     * @param level The log level (e.g., info, warning, error).
     * @param message The log message.
     */
    void onRenderLog(int level, QString message);

    /**
     * @brief Called when a slice has been saved during rendering.
     * This is called by the RenderWorker to notify the MainWindow that a slice has been saved.
     * @param name The name of the slice that was saved.
     * @param fullPath The full file path where the slice was saved.
     */
    void onRenderSliceSaved(int index, QString name, QString fullPath);

    /**
     * @brief Called once per input during a render (phase 1), to update its tile live.
     * @param path The input file's absolute path.
     * @param status A Core::InputStatus value (appended / skipped-with-reason), mapped to a
     *               FileStatus for the tile.
     */
    void onRenderInput(QString path, int status);

    /**
     * @brief Called when the rendering process has finished.
     * This is called by the RenderWorker to notify the MainWindow that rendering has completed.
     */
    void onRenderFinished();

private:
    // --- render orchestration ---

    /**
     * @brief Starts the rendering process for the specified project index.
     *
     * @param projectIndex Index into m_workspace.projectItems.
     * @return true only when a worker was actually started; false when the project was
     *         skipped for any reason (no output profile / directory / inputs, already
     *         up to date, config-change prompt declined, output directory not creatable).
     *         The batch queue relies on this to tell "skipped" from "started" — without
     *         it, it would wait forever on a finished() signal that never comes.
     */
    bool startRender(int projectIndex);

    void cancelRender();                //!< Cancels the ongoing rendering process, if any.

    // --- batch render (F6) — see renderbatch.cpp ---

    /**
     * @brief Starts the next project in the batch queue, skipping ones that need no work.
     *
     * Loops rather than recurses: whole runs of projects can be skipped (e.g. all up to
     * date), and a loop handles that without deep recursion or re-entering a slot. Returns
     * as soon as a worker starts — the rest of the queue is driven by onRenderFinished().
     * When the queue empties it calls finishBatch().
     */
    void advanceBatch();

    /// Reports the batch summary (rendered / skipped / failed) and clears the batch state.
    void finishBatch();

    /**
     * @brief Warns once, after opening a workspace, if canvas profiles moved on since the
     *        last render — and offers to refresh right away.
     *
     * Editing a canvas profile leaves every file byte-identical, so the only visible sign
     * is that tiles turned amber ("out of sync"). This explains why, instead of letting
     * the user discover it as a surprise prompt mid-render. Accepting starts the batch
     * with the config-change question already answered.
     */
    void warnIfCanvasConfigStale();

    /**
     * @brief Tells the user, once after opening a workspace, that colliding profile
     *        identifiers had to be repaired — and saves the repaired workspace.
     *
     * Profiles used to be given an id derived from the clock, so several created in one
     * pass shared one. A shared id makes the second profile unreachable: it counts as
     * already assigned everywhere and disappears from the assign list. The library
     * separates them on load; this explains the change and the one consequence the user
     * can see, namely that projects may now report as out of sync.
     *
     * Saving matters — without it captureSnapshot() would treat the repaired state as the
     * baseline, the fix would never reach disk, and the dialog would return on every open.
     *
     * @param report What load() repaired; does nothing when empty.
     */
    void reportWorkspaceRepair(
        const Platemaker::Infrastructure::WorkspaceRepairReport &report);

    /**
     * @brief Asks whether the batch should continue after a project failed.
     *
     * The whole error policy lives here, so switching to "log and continue" is a one-line
     * change rather than an edit spread across onRenderFinished().
     *
     * @param projectName Name of the project that failed.
     * @return true to keep going with the remaining projects.
     */
    [[nodiscard]] bool batchShouldContinueAfterFailure(const QString &projectName);

    /**
     * @brief Deletes confirmed orphan output files (m_renderOrphanCandidates) that the
     * freshly-rendered project no longer produces.
     * @param project The project for which to delete orphaned outputs.
     */
    void deleteOrphanedOutputs(const Platemaker::Models::ProjectItem &project);

    /**
     * @brief Writes the current action-log text to a timestamped file in the workspace cache and
     * prunes to the newest k_maxRenderLogs runs. Called at each render session's end (single render or
     * batch). A no-op when no workspace is loaded; best-effort (write/prune failures are ignored).
     */
    void persistRenderLog();

    /**
     * @brief Builds the render summary block for the action log: slice count / input count / elapsed
     * time, the heaviest slice (name + size), and the total output size — one line each (the heaviest
     * line is omitted when there are no slices).
     * @param project    The just-rendered project (for output/input counts and the output directory).
     * @param elapsedMs  Wall-clock render time in milliseconds.
     */
    [[nodiscard]] QStringList renderSummaryLines(
        const Platemaker::Models::ProjectItem &project, qint64 elapsedMs) const;

    /** @brief Formats a millisecond duration for humans ("820 ms", "3.2 s", "1 m 05 s"). */
    [[nodiscard]] static QString humanReadableDuration(qint64 ms);

    // --- UI helpers ---
    [[nodiscard]] Project *projectWidget(int projectIndex) const;   //!< Gets the Project widget for the specified project index, nullptr if not found.

    /**
     * @brief Resolves the output profile for the given project.
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

    /**
     * @brief Asks where a workspace file should go, and keeps asking until the answer is a folder no other
     *        workspace lives in.
     *
     * New and Save As are where a second workspace would enter a folder, so this is where the one-per-folder
     * rule (workspacefolder.hpp) is enforced: a folder that already holds one is refused, with the offer of
     * a subfolder named after the new file.
     *
     * @param title   The file dialog's caption.
     * @param startAt Where the dialog opens.
     * @return The path to write, or empty when the user gave up.
     */
    [[nodiscard]] QString askWorkspaceFile(const QString &title, QString startAt);

    /**
     * @brief Opening a folder that holds several workspaces: which one stays.
     *
     * Such a folder is not opened as it is — the workspaces in it share every file beside them, and nothing
     * that writes there can be safe. The user picks the one to keep; the others go to the Recycle Bin, where
     * they can be restored from.
     *
     * @param path The workspace the user asked to open.
     * @return The workspace to open now, or empty when the user cancelled or a file could not be moved.
     *         \p path itself when its folder holds nothing else.
     */
    [[nodiscard]] QString resolveSharedFolder(const QString &path);

    /**
     * @brief Claims the folder \p workspacePath lives in, asking the user where someone else holds it.
     *
     * Another window on this machine is reported and refused; another computer can be taken over, for the
     * case nothing here can detect — that computer is off, or Platemaker crashed there. A folder that
     * cannot be marked at all (read-only media) is opened unguarded rather than refused.
     *
     * @return The lock, or null when the user cancelled or the folder is open in another window here.
     */
    [[nodiscard]] std::unique_ptr<WorkspaceLock> lockFolderOf(const QString &workspacePath);

    //! Whether the folder \p workspacePath lives in is the one this window holds, and still holds.
    [[nodiscard]] bool holdsFolderOf(const QString &workspacePath);

    /**
     * @brief Asked before anything is written to the workspace folder: is it still ours?
     *
     * False once another computer has taken the folder over — and then the user is told, once, and offered
     * the two ways out that do not write over the other computer's work.
     */
    [[nodiscard]] bool canWriteWorkspace();

    void onWorkspaceTakenOver();   //!< The prompt canWriteWorkspace() and window activation raise.

    /**
     * @brief *Save As* to another folder: copies in everything the workspace made, and points it there.
     *
     * Every project's overlay files (and the pictures behind lettered pictures — see
     * collectOverlayFiles()), every canvas profile's template and the whole `fonts/` folder, so that after the save the workspace
     * references only its own folder and the old one can be moved, zipped or deleted without breaking it.
     * All or nothing: nothing in the model changes unless every file made it, and a failure names the file
     * and stops the Save As before anything is written.
     *
     * @param newWorkspacePath Where the workspace is about to be saved.
     * @return False when a file could not be copied; the user has been told which.
     */
    [[nodiscard]] bool collectWorkspaceFiles(const QString &newWorkspacePath);

    //! Every file the open workspace uses: overlay files, the pictures behind lettered pictures, templates.
    [[nodiscard]] QStringList referencedWorkspaceFiles() const;

    /**
     * @brief Moves what the workspace made and no longer uses to the Recycle Bin — at open.
     *
     * At open because that is the one moment the model *is* the file and no undo history exists: at close,
     * after *Discard*, memory is not what is on disk; during the session, undo may still need a file. Runs
     * only with the folder held (W2) and holding this workspace alone (W1); anything the trash refuses is
     * offered for permanent deletion, once per open. Reported in the Action log and the status bar — an
     * event, not a standing condition, so not an advisory. The caller checks the lock.
     */
    void sweepWorkspaceFolder();

    /**
     * @brief Re-sets, in their own font, the bubbles whose file was baked in a stand-in — at open, and
     *        when *Add font…* brings the font. Open strip editors are re-fed when anything was re-set.
     *
     * A bubble edited while its font was missing is outlined in whatever Qt substituted, and the file says
     * so (`pm:fontFallback`); its record still names the font it was meant to have. Once that font is
     * installed (or brought by the workspace) this rewrites the file from the record, updates its hash and
     * marks the workspace modified — the save is the artist's. Not an undo step: it repairs files to match
     * their records. Runs before the sweep, since a lettered picture's wrapper is renamed by its content and
     * the old one is then unused; and only with the folder held (the caller checks), since it writes there.
     */
    void healFontFallbacks();
    void applyWorkspaceToUi();                  //!< Updates the UI to reflect the current workspace model
    void closeWorkspace();                      //!< Closes the current workspace, clearing the model and UI
    void setDirty(bool dirty);                  //!< Sets the dirty flag and updates the title bar. True = workspace has unsaved changes and the title bar will show '*'
    void updateTitleBar();                      //!< Updates the window title to reflect the current workspace and dirty state.

    /**
     * @brief Captures the current workspace's serialized form as the "saved" baseline.
     * Call after every successful load/save; clears the dirty flag.
     */
    void captureSnapshot();

    // --- advisories and the status bar (see advisories.cpp) ---

    /**
     * @brief Re-evaluates every condition this window can observe about one project, raising or
     *        withdrawing its advisories to match.
     *
     * Cheap and idempotent on purpose, so it can be called from anything that changes a project
     * rather than from the one place that changes each condition — which is the arrangement where a
     * condition is eventually forgotten and a badge outlives its cause.
     */
    void refreshAdvisoriesFor(int projectIndex);
    void refreshAllAdvisories();      //!< The same, for every project in the workspace.

    //! This project's lettered objects drawn in a stand-in because their font is missing, in composite
    //! order; \p families (when given) gets those fonts, once each. What the missing-fonts advisory and
    //! the Fonts dialog both report, so the two can never disagree.
    //! Whether \p family is installed on this computer — as opposed to only brought by a workspace's fonts/.
    //! One font answers to several names (Win32, DirectWrite, typographic), and Qt names a font added from a
    //! file differently from the same font installed, so the start-up list alone misses some; DirectWrite's
    //! installed fonts are asked by every name as well (installedFontFiles()).
    [[nodiscard]] bool isInstalledFamily(const QString& family) const;

    [[nodiscard]] QStringList objectsInStandIns(const Platemaker::Models::ProjectItem& project,
                                                QStringList* families = nullptr) const;

    //! Points the status bar's advisory strip at whichever project is being looked at. The strip keeps
    //! itself current with the registry; what it cannot know on its own is which chapter that is.
    void retargetStatusAdvisories();

    //! Opens \p projectUid's strip editor with \p uids selected — an advisory's "Show them", its way out of
    //! itself, not a menu item.
    void showObjects(const QString& projectUid, const QStringList& uids);
    //! Deletes them, as one undoable step on that chapter's history. Offered only by the render gate.
    void deleteUnanchoredObjects(const QString& projectUid);

    //! What the render gate decided.
    enum class RenderGate {
        Pass,    //!< Nothing stands in the way, or the artist chose to render anyway.
        Stop,    //!< Cancelled, or the artist went to look at the problem.
        Again,   //!< A resolution changed the project; everything derived before it is out of date.
    };

    /**
     * @brief The render gate: a render stops and asks while any **Error** advisory stands for the project.
     *
     * One rule rather than a list of checks, and every button comes from an advisory — its way to look
     * and its way to resolve — so a second kind of error brings its own ways out and nothing here
     * changes. *Render anyway* is always offered: it is what the library does unaided, and the gate is
     * there so nobody finds out an hour later, not to overrule them. A batch does not ask; it skips the
     * chapter and says why in the summary.
     */
    [[nodiscard]] RenderGate askRenderGate(int projectIndex);

    [[nodiscard]] int     projectIndexForUid(const QString& projectUid) const;   //!< -1 when it is gone.
    [[nodiscard]] QString activeProjectUid() const;   //!< The project the status bar is speaking about.

    // --- undo / redo ---
    void setupUndo();   //!< Creates the QUndoGroup + workspace stack and wires the Edit-menu actionUndo/actionRedo to it.

    /**
     * @brief Records one undoable **workspace-scope** edit (profile CRUD, project rename, templates)
     *        onto the workspace undo stack.
     *
     * Brackets \p mutate with WorkspaceEditor::snapshotMeta before/after and pushes a
     * WorkspaceSnapshotCommand if anything changed. \p mutate does its own UI refresh / setDirty.
     */
    void commitWorkspaceEdit(const QString& text, const std::function<void()>& mutate);

    /**
     * @brief Authoritative change check: true if the workspace differs from the last
     * captured snapshot. Robust against any action that forgot to setDirty().
     */
    [[nodiscard]] bool isWorkspaceModified() const;

    // --- profile portability helpers (see profiles.cpp) ---

    //! Fills an Import submenu on demand (aboutToShow): Browse, the user library (if any), and
    //! "recent workspace" / "recent bundle" sub-submenus. Each leaf runs importProfilesFlow for that
    //! source. A native menubar submenu, not a QMenu popped from an action handler. @p canvasKind
    //! selects the palette.
    void populateImportMenu(QMenu* menu, bool canvasKind);

    //! Fills an Export submenu on demand: to a bundle file, or to the user library.
    void populateExportMenu(QMenu* menu, bool canvasKind);

    //! Loads @p src (a bundle or a full workspace), cherry-picks, and imports the selection through
    //! WorkspaceEditor::importProfiles into this workspace (fresh ids, so it stays self-contained).
    void importProfilesFlow(bool canvasKind, const QString& src);

    //! Cherry-picks from this workspace's palette of the given kind, then writes a bundle file
    //! (@p toFile) or merges the selection into the user library.
    void exportProfilesFlow(bool canvasKind, bool toFile);

    //! Absolute path of the user's global profile library — a bundle in the OS app-data dir. The
    //! library is purely a GUI convenience: an import source and an export target. The lib stays
    //! app-data-agnostic, so the GUI owns this location. The parent directory is created on demand.
    [[nodiscard]] QString userProfileLibraryPath() const;

    //! Recently used profile-bundle files (QSettings; advisory, never required), most-recent first.
    [[nodiscard]] QStringList recentBundles() const;
    void addToRecentBundles(const QString& path); //!< Adds/promotes a bundle path in the recent-bundles list.

    //! Loads the canvas + output palettes from \p path, which may be a profile bundle
    //! (.platemaker.profiles.json) or a full workspace (.platemaker.json) — distinguished by content.
    //! Shows an error and returns false on failure.
    bool loadProfilesFromFile(const QString&                                   path,
                              std::vector<Platemaker::Models::CanvasProfile>&  canvasOut,
                              std::vector<Platemaker::Models::OutputProfile>&  outputOut);

    //! Merges the given profiles into the user library bundle (upsert by name for each kind, leaving
    //! the other kind untouched), then saves it. Shows an error and returns false on failure.
    bool addToUserLibrary(const std::vector<Platemaker::Models::CanvasProfile>& canvas,
                          const std::vector<Platemaker::Models::OutputProfile>& output);

    // --- recent workspaces (advisory list in QSettings; never required) ---
    [[nodiscard]] QStringList recentWorkspaces() const;     //!< Returns a list of recently opened workspaces.
    void addToRecentWorkspaces(const QString &path);        //!< Adds a workspace path to the list of recent workspaces.
    void rebuildRecentMenu();                               //!< Rebuilds the "Open Recent Workspace" menu based on the current list of recent workspaces.
    void openRecentWorkspace(const QString &path);          //!< Opens a workspace from the recent workspaces list.
    [[nodiscard]] QString defaultDialogDir() const;         //!< Returns the default directory for file dialogs (last workspace's dir or home dir).
    [[nodiscard]] QString workspaceCacheDir() const;        //!< Returns "<workspace dir>/.platemaker-cache" (thumbnails, render logs); empty when no workspace is loaded.

    // --- project management ---
    void renameProject(int modelIndex);     //!< Prompts the user to rename the project at the given model index and updates the workspace and UI accordingly.
    void duplicateProject(int modelIndex);  //!< Creates a new project seeded from the one at the given model index (its inputs + profile links only — no outputs / output dir).
    void removeProject(int modelIndex);     //!< Removes the project at the given model index from the workspace.
    [[nodiscard]] class QDockWidget *dockForProject(int modelIndex) const;  //!< Returns the QDockWidget for the project at the given model index, or nullptr if not found.

    // --- project dock management ---
    /**
     * @brief Opens a dock for the project at the given model index.
     * If the dock is already open, it is raised to the front.
     * @param projectIndex The index of the project in the workspace model.
     */
    void openProjectDock(int projectIndex);

    /**
     * @brief Closes the dock for the project at the given model index.
     * If the dock is not open, this function does nothing.
     * @param index The index of the project in the workspace model.
     */
    void closeProjectByIndex(int index);

    /**
     * @brief Toggles the floating state of the dock for the project at the given model index.
     * If the dock is not open, this function does nothing.
     * @param index The index of the project in the workspace model.
     */
    void toggleProjectFloatState(int index);

    /**
     * @brief (Re)wires every dock tab bar so its tabs are closable and route close / double-click to the
     * resolvers below. Idempotent — call it after any change that adds a dock to a tab group (opening a
     * project, tabifying the strip). The workspace, project and strip docks can share one tab bar.
     */
    void wireDockTabBars();

    /**
     * @brief The dock whose tab is at @p index in tab bar @p bar, resolved by window title across the
     * workspace, project and strip docks (a shared tab bar means a raw index into any one list is wrong).
     * @return The matching dock, or nullptr.
     */
    [[nodiscard]] QDockWidget *dockForTabBarTab(const QTabBar *bar, int index) const;

    /**
     * @brief Installs the shared `DockTitleBar` on @p dock and wires its dock-specific behaviour: minimise
     * toggles dock ⇄ detach (docking tabs it beside the Workspace, except the Workspace anchor and the
     * Action column which just re-dock); close routes to closeDock(). Maximise is handled inside
     * DockTitleBar. Used for the Workspace, project, strip and Action docks.
     */
    void installDockTitleBar(QDockWidget *dock);

    //! Closes @p dock the way its kind expects: the Workspace and strip docks hide (reopened via their
    //! menu / View strip); a project dock is removed and destroyed. Shared by the tab close and the
    //! custom title bar's close button.
    void closeDock(QDockWidget *dock);

    // --- strip viewer dock (per-project, floating) ---

    /**
     * @brief Opens (or raises + refreshes) the continuous strip viewer for the project at @p projectIndex.
     *
     * A dedicated per-project dock, defaulting to floating; allowed Left/Top/Bottom but never the Action
     * column, and never tab-combined. It carries a **custom title bar** whose buttons dock it (minimise),
     * fill the screen (maximise ⇄ restore) or close it — a floating QDockWidget otherwise shows only a
     * close button, and native min/max on a dock misbehave. Fed the project's committed output slices; a
     * render's finish hook refreshes an open one. Raise-if-open, tracked in m_openStripDocks,
     * reindexed/closed with the project.
     */
    void openStripEditorDock(int projectIndex);

    //! The open strip dock for the project at @p modelIndex, or nullptr. Keyed by the "projectIndex" property.
    [[nodiscard]] QDockWidget *dockForStripEditor(int modelIndex) const;

    //! Reloads @p dock's StripEdit::Editor from its project's current committed output slices (in strip order).
    void refreshStripEditor(QDockWidget *dock);

    // --- members ---
    static constexpr int k_maxRecentWorkspaces = 10;     //!< Maximum number of recent workspaces to track in the menu.
    static constexpr int k_maxRecentBundles    = 10;     //!< Maximum number of recent profile bundles to track.
    static constexpr int k_maxRenderLogs       = 10;     //!< Maximum number of timestamped render logs kept in the workspace cache.
    static constexpr int k_actionDockDefaultWidth = 350; //!< Startup width of the Action right column (also its minimum); changed only by dragging its grip.
    static constexpr int k_actionGripWidth        = 6;   //!< Width of the Action column's left-edge drag handle.
    //! How long a notice stays in the status bar. Long enough to read a sentence, short enough that it is
    //! gone before it becomes furniture — a message that never expires is a badge, and an event is not a
    //! condition (see the strip editor's `noted` signal).
    static constexpr int k_noticeMs = 6000;

    Ui::MainWindow *ui;                         //!< The UI form generated by Qt Designer (ui_mainwindow.h).
    QList<QDockWidget *> m_openProjectDocks;    //!< List of currently open project docks (QDockWidget) for the workspace's projects.
    QList<QDockWidget *> m_openStripDocks;      //!< Open strip-viewer docks (one per project), keyed by the "projectIndex" property.
    QWidget *m_actionGrip         = nullptr;    //!< Left-edge drag handle of the fixed-width Action column.
    bool     m_actionGripDragging = false;      //!< True while the Action grip is being dragged.
    int      m_actionGripStartX   = 0;          //!< Global mouse X at grip-drag start.
    int      m_actionGripStartWidth = 0;        //!< Action column width at grip-drag start.
    int      m_actionDockWidth    = k_actionDockDefaultWidth; //!< Current fixed width of the Action column.
    QMenu *m_recentMenu = nullptr;              //!< Submenu attached to actionOpen_recent_workspace
    // Import/Export submenus, attached to the profile actions and (re)populated on aboutToShow.
    QMenu *m_importCanvasMenu = nullptr;    //!< Submenu for importing canvas profiles (Browse, user library, recent workspaces/bundles).
    QMenu *m_importOutputMenu = nullptr;    //!< Submenu for importing output profiles (Browse, user library, recent workspaces/bundles).
    QMenu *m_exportCanvasMenu = nullptr;    //!< Submenu for exporting canvas profiles (to a bundle file or the user library).
    QMenu *m_exportOutputMenu = nullptr;    //!< Submenu for exporting output profiles (to a bundle file or the user library).

    Platemaker::Models::Workspace m_workspace;  //!< The authoritative workspace model (projects, profiles, templates).

    /**
     * @brief Parsed authoring records for every project's text/bubble overlays.
     *
     * A cache, not a store: the records live inside the overlays' own SVG assets, and are read back
     * from them when a workspace opens. Nothing here is written to disk — an authoring sidecar would be
     * a second copy of what the asset already carries.
     *
     * Kept here rather than on the Project widget because a project has records whether or not its dock
     * happens to be open.
     */
    ArtifactStore m_overlayArtifacts;
    Platemaker::Infrastructure::WorkspaceSerializer m_serializer;   //!< Serializes the workspace model to/from disk.
    std::unique_ptr<WorkspaceLock> m_lock;      //!< The open workspace's folder, held; null when none is open.
    QHash<QString, int> m_workspaceFonts;       //!< The open workspace's `fonts/`, as registered: file name → QFontDatabase id. By name, so it survives a Save As.
    QStringList m_installedFamilies;            //!< The font families installed on this computer, taken before any workspace's are added.
    //! The bubble presets, one store for the application: every strip editor is handed it, so a preset
    //! saved, deleted or imported anywhere is offered everywhere at once. Presets follow the artist, not
    //! the chapter — they live in the application's settings, and a workspace closing does not touch them.
    StripEdit::PresetStore *m_presets = nullptr;
    bool m_takeoverPromptOpen = false;          //!< Keeps the takeover prompt from stacking on itself.

    // Undo/redo: a QUndoGroup holds one stack per open project plus the workspace stack; the active
    // stack follows the visible tab in the workspace dock area (Ctrl+Z/Ctrl+Y route to it). Depth 10.
    QUndoGroup* m_undoGroup         = nullptr;  //!< Owns the per-context stacks; provides the Undo/Redo actions.

    /**
     * @brief Every project's history, for the life of the session, keyed by `ProjectItem::uid`.
     *
     * **One stack per project**, covering both the project dock and its strip editor, because both edit
     * one document and the artist edits it in one train of thought. A history per window would make
     * Ctrl+Z reach past the last thing done to undo the one before it, which is a stranger thing to
     * explain than a Ctrl+Z whose effect is in the other window — and that one is answered instead by
     * taking the artist there (see `showDockAttention`). What a step covers is still decided per step:
     * `ProjectSnapshotCommand` and `OverlaySnapshotCommand` each restore one half of the document and
     * leave the other alone.
     *
     * Owned here and never by a dock. Closing a project or its strip editor and opening it again finds
     * the history where it was left: a history that dies with a window is a history the artist loses by
     * tidying up. Keyed by uid rather than by index because removing a project shifts every index after
     * it, and a key that moves is not a key.
     */
    QHash<QString, QUndoStack*> m_projectHistories;

    //! This project's history, created on first use.
    [[nodiscard]] QUndoStack* historyFor(int projectIndex);
    //! Forgets a removed project's history — there is nothing left for it to restore.
    void dropHistoryFor(const QString& projectUid);
    QUndoStack* m_workspaceUndoStack = nullptr; //!< Workspace-scope history (profiles, project rename, templates).

    /**
     * @brief Every standing advisory in the application — conditions, not events.
     *
     * Owned here rather than by the strip editor's framework because the conditions are not a tool's:
     * a tool notices that the grade will not run once you are inside the editor, but the switch that
     * decides it is in the Workflow tab. Whoever can observe a condition raises it.
     */
    Advisories* m_advisories = nullptr;

    /**
     * @brief The status bar's advisory strip — **one** widget holding every chip.
     *
     * One rather than several because `QStatusBar` frames every item it is given, and a frame per chip
     * is a vertical rule between each pair. The same widget class the strip editor hosts along its own
     * bottom edge: the registry always allowed more than one subscriber, and this is the surface.
     */
    AdvisoryBar* m_statusAdvisories = nullptr;

    QString m_workspacePath;                    //!< Path of the currently loaded workspace file (empty if none).
    bool    m_dirty = false;                    //!< Eager flag driving the title-bar asterisk (*)
    QString m_savedSnapshot;                    //!< Serialized workspace at last load/save, used to detect unsaved changes (dirty state). Empty if no workspace is loaded.

    // UI-only selection state (not persisted in the workspace model since v2).
    // Shouldn't be used per project, or even per input file?
    QString m_activeCanvasProfileName;  //!< Name of the canvas profile currently selected in the UI (may not exist in the workspace model).
    QString m_activeOutputProfileId;    //!< Id of the output profile currently selected in the UI. Tracked by id, not name: a preset and a user copy can share a name, and the id may point at a preset (resolved from the catalogue, not the workspace).

    // --- render state (one render at a time, owned here) ---
    Platemaker::Infrastructure::CancellationToken m_cancelToken;    //<! Cancellation token for the current render operation, if any.
    QThread      *m_renderThread        = nullptr;  //!< Thread running the current RenderWorker (if any).
    RenderWorker *m_renderWorker        = nullptr;  //!< The current RenderWorker instance (if any).
    bool          m_rendering           = false;    //!< True if a render operation is currently in progress.
    int           m_renderProjectIndex  = -1;       //!< Index of the project currently being rendered (in m_workspace.projectItems), -1 if none.
    int           m_activeProjectIndex  = -1;       //!< Index of the project dock that was last raised (for F5/menu).
    QElapsedTimer m_renderTimer;                    //!< Wall-clock timer for the current single render (started in startRender), read for the action-log summary.
    QString       m_lastRenderOutputDir;            //!< Output directory of the most recent render (set in startRender), for the log's "Open output folder" action.

    // Outputs from the previous configuration to delete after a config-change
    // full re-render (set only when the user confirmed the cleanup prompt).
    QStringList   m_renderOrphanCandidates;         //!< List of orphaned output files from the previous configuration.
    QString       m_renderOrphanDir;                //!< Directory containing the orphaned output files.

    // --- batch render state (F6). m_batchTotal == 0 means no batch is in flight. ---
    std::vector<int> m_batchQueue;      //!< Project indices still to render.
    QElapsedTimer    m_batchTimer;      //!< Wall-clock timer for the whole batch (started in onRefreshAllProjects), read in finishBatch.
    int              m_batchTotal = 0;  //!< Number of projects the batch started with.
    QStringList      m_batchOk;         //!< Projects that rendered successfully.
    QStringList      m_batchSkipped;    //!< Projects skipped, each with the reason.
    QStringList      m_batchFailed;     //!< Projects whose render failed.

    /** 
     * @brief Why the last startRender() bailed out, so the batch can report a useful reason
     * instead of a bare "skipped". Set by startRender(), consumed by advanceBatch().
     */
    QString          m_batchSkipReason;

    /** 
     * @brief How a config change (format / slice size / canvas profile) is confirmed before a destructive re-render.
     * 
     * A batch answers once for the whole sweep: asking per project would defeat the point
     * of "refresh everything in one go". The first project that actually hits a config
     * change asks — so a sweep with nothing to confirm stays silent — and its answer
     * promotes the policy to AlreadyConfirmed or DeclineAll for the rest of the run.
     */
    enum class ConfigChangePolicy {
        AskPerProject,     //!< Default (single render) — confirm separately every time.
        AskOnceForBatch,   //!< Batch start: the next config change asks, then decides for all.
        AlreadyConfirmed,  //!< Confirmed for this sweep; proceed without asking again.
        DeclineAll,        //!< Declined for this sweep; skip config-changed projects.
    };

    /** 
     * @brief Reset to AskPerProject by finishBatch(); leaving a batch-wide answer in place would
     *  let a later single render skip the destructive prompt silently.
     */
    ConfigChangePolicy m_configChangePolicy = ConfigChangePolicy::AskPerProject;
};

#endif // MAINWINDOW_HPP
