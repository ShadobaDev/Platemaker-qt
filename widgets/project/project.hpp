#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <QWidget>

#include "artifact.hpp"
#include "overlaystate.hpp"
#include <QList>
#include <QSize>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

#include <platemaker/models/workspace.hpp>

namespace Ui { class Project; }
class QListWidgetItem;
class OutputFormatOptionsWidget;
class QVBoxLayout;
class QUndoStack;
class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QUrl;

/**
 * @brief Where an undone or redone step is visible — the two windows one project is edited from.
 *
 * A project has **one** history covering both, so a step can land in a window the user is not looking
 * at. This is what lets undo take them there. It is not a second history and it is not a filter: every
 * step goes on the one stack in the order it was made, and this only answers "where do I look".
 */
enum class EditScope {
    ProjectDock,   //!< Inputs, links, profiles, the output directory.
    StripEditor,   //!< Text & bubbles.
};

/**
 * @brief The Project class represents a single project within the Platemaker application.
 * It provides a user interface for managing input files, canvas profiles, output profiles,
 * and rendering settings. The class allows users to add, remove, and reorder input files,
 * select canvas and output profiles, configure output format options, and initiate rendering.
 */
class Project : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a Project widget for the specified project index within the given workspace.
     * 
     * Builds the dock's contents for one project.
     * The stack is **owned by MainWindow and lives for the session**. This widget uses it; it does not
     * own it, because a history that dies with a dock is a history the artist loses by tidying up.
     * @param projectIndex The index of the project within the workspace.
     * @param workspace A reference to the workspace containing the project data.
     * @param cacheDir The directory where cached thumbnails and other temporary files are stored.
     * @param history  Everything this project records: inputs, links, profiles, the output directory,
     *                 and text & bubbles. One history, because one document is being edited — an edit
     *                 made in the strip editor and an edit made here are steps in the same session of
     *                 work, and a Ctrl+Z that skips over the last one to undo the one before it is a
     *                 worse surprise than a Ctrl+Z whose effect is in the other window.
     * @param parent The parent widget, if any.
     */
    explicit Project(int projectIndex,
                     Platemaker::Models::Workspace& workspace,
                     const QString& cacheDir,
                     QUndoStack* history,
                     QWidget *parent = nullptr);
    ~Project();         //!< Destroys the Project widget and cleans up resources.

    void populate();    //!< Populates the UI with the current state of the project, including input files, canvas profiles, output profile selection, format controls, output directory display, and output tiles.

    /**
     * @brief Updates the workspace index this widget refers to. Used when a lower-indexed
     * project is removed and the vector shifts (the widget reads the index live).
     * @param index The new project index.
     */
    void setProjectIndex(int index) { m_projectIndex = index; }

    // --- render UI (driven by MainWindow, which owns the render state) ---
    /** 
     * @brief Sets the rendering state of the project.
     *  flips Render<->Stop + disables output controls
     * @param rendering True if rendering is in progress, false otherwise.
     */
    void setRendering(bool rendering);

    /** 
     * @brief Updates the output tile at the specified index.
     *  live positional update during a render - creates or replaces the tile at row \p index
     * @param index The index of the tile to update.
     * @param name The name of the tile.
     * @param fullPath The full path of the tile.
     */
    void setOutputTile(int index, const QString& name, const QString& fullPath);
    /** 
     * @brief Updates the status of an input tile.
     *  live per-input update during a render - repaints the input tile matching \p filePath (cyan when Processed without a canvas profile)
     * @param filePath The path of the input file.
     * @param status The new status of the input tile.
     * @param renderedWithoutProfile True if the file was rendered without a canvas profile.
     */
    void setInputTileStatus(const QString& filePath, 
                            Platemaker::Models::FileStatus status,
                            bool renderedWithoutProfile = false);
    /**
     * @brief Rebuilds the output tiles from getOutputImages().
     *
     * Also marks outputs still reported as done as out of sync when the output profile has changed
     * since they were rendered — a change there leaves the old files on disk and hash-matching, so it
     * is caught here, on every repaint.
     */
    void refreshOutputTiles();

    /**
     * @brief Persist a settled colour-correction edit onto this project as one undoable step named
     *        @p undoText (also refreshes the workflow map).
     *
     * The name comes from the caller, which knows what was done — an adjustment moved or removed, a page
     * excluded — rather than being guessed here from a before-and-after that cannot tell those apart.
     *
     * A neutral grade is no grade, so this is also how every adjustment comes off at once: the workflow
     * card's "−" passes a neutral grade that **keeps the page exclusions**, which are each page's own
     * decision and apply again the moment the strip is graded again. There is no separate toggle that could
     * leave a grade parked where the render would not run it.
     *
     * Called by MainWindow on StripEdit::Editor::colourCorrectionEdited, and by the workflow card.
     * @param cc The new colour correction to apply.
     * @param undoText The text to display for the undo action.
     */
    void applyColourCorrection(const Platemaker::Models::ColourCorrection& cc, const QString& undoText);

    // --- text & bubble overlays -------------------------------------------------------------
    /**
     * @brief Where the workspace file lives — the root of `overlays/`.
     *
     * Set by MainWindow, and re-set after "Save as": the assets a project references have to follow
     * the workspace they belong to, or a moved workspace renders bubbles from the old directory.
     * 
     * @param path The path to the workspace file.
     */
    void setWorkspacePath(const QString& path) { m_workspacePath = path; }

    /**
     * @brief Adopts this project's authoring records (read back from its assets).
     * @param artifacts The map of artifacts to adopt.
     */
    void setArtifacts(ArtifactMap artifacts);

    /**
     * @brief Re-writes every overlay's SVG from its authoring record.
     *
     * Undo and redo restore what each bubble *says*, but a bubble overwrites its own asset file rather
     * than leaving one behind per edit — so the file on disk still holds whatever the step being undone
     * wrote. Re-emitting from the restored records puts the two back in agreement. Safe to call
     * repeatedly: emission is a pure function of the record, so a file that already matches is rewritten
     * with identical bytes.
     */
    void rewriteOverlayAssets();

    /**
     * @brief Returns the map of artifacts associated with this project.
     * @return The artifact map.
     */
    [[nodiscard]] const ArtifactMap& artifacts() const { return m_artifacts; }

    /**
     * @brief Registers a newly drawn bubble: writes its SVG, then lets the library inventory the file.
     *
     * The library mints the uid, hashes the file and dedups identical content, so creation goes through
     * `ProjectItem::addOverlay()` rather than being assembled here. One undo step.
     *
     * @param xFrac,yFrac    Top-left as fractions of the render's target width — page-relative to the
     *                       anchor page's top edge.
     * @param wFrac          The artwork's rendered width in that same unit.
     * @param anchorInputUid The input page the bubble rides on.
     */
    void createOverlay(const Artifact& artifact, double xFrac, double yFrac, double wFrac,
                       const QString& anchorInputUid);

    /**
     * @brief Registers artwork the author drew elsewhere as an overlay, exactly as it is.
     *
     * The file is copied into the workspace's `overlays/` under its content hash — never referenced
     * where it was found, so the workspace stays self-contained and moving it does not break a bubble.
     *
     * No authoring record is created, and that is the point: with no `pm:*` parameters the overlay is a
     * **flat asset**, drawn from the file and placed, moved and scaled like any other bubble, but not
     * re-typable. That case already exists for a bubble whose parameters were lost, so importing needs
     * no separate kind of overlay — it is the same one, arrived at deliberately.
     * 
     * @param sourceFile The file to copy into the workspace.
     * @param xFrac,yFrac Top-left as fractions of the render's target width — page-relative to the anchor page's top edge.
     * @param wFrac       The artwork's rendered width in that same unit.
     * @param naturalSize The artwork's natural size, so the library can scale it to the target width and preserve its aspect ratio.
     * @param anchorInputUid The input page the bubble rides on.
     */
    void importOverlayArtwork(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                              QSize naturalSize, const QString& anchorInputUid);

    /**
     * @brief Stores a complete new overlay state — move, restyle, delete, reorder or mute — as one
     *        undo step, re-writing the assets whose authoring record changed.
     * 
     * @param overlays The new overlays, in the order they are drawn.
     * @param artifacts The new authoring records for those overlays, keyed by `StripOverlay::uid`.
     * @param undoText The text to display for the undo action.
     */
    void applyOverlays(std::vector<Platemaker::Models::StripOverlay> overlays,
                       ArtifactMap                                  artifacts,
                       const QString&                               undoText);

    /**
     * @brief Deletes the overlays @p uids, with their authoring records, as one undo step. Through
     * @brief applyOverlays(), the same door every other overlay edit goes through.
     * @param uids The UIDs of the overlays to delete.
     * @param undoText The text to display for the undo action.
     */
    void deleteOverlays(const QStringList& uids, const QString& undoText);
    /**
     * @brief Rebuilds the palette-derived views (canvas list, output combo, format controls) after a
     * workspace-level profile edit.
     * 
     * rebuilds the palette-derived views (canvas list, output combo, format controls) after a workspace-level profile edit — see MainWindow::workspaceProfilesChanged
     */
    void refreshProfileViews();



    // --- Undo / redo ---
    // Everything this project records goes on one QUndoStack; MainWindow owns it, adds it to a
    // QUndoGroup and makes it active while either of this project's docks is in front. Workspace-scope
    // edits triggered from here (canvas-profile *content* edit, output-format edit) are bracketed with
    // WorkspaceEditor::snapshotMeta and forwarded to MainWindow via workspaceEditCommitted so they land
    // on the workspace timeline instead.
    /**
     * @brief Returns the undo stack associated with this project.
     * @return A pointer to the QUndoStack used for undo/redo operations in this project.
     */
    [[nodiscard]] QUndoStack* undoStack() const { return m_undoStack; }
    /**
     * @brief Applies a snapshot of the project state, restoring everything **except** the overlays.
     * @param snapshot The snapshot to apply.
     */
    void applyProjectSnapshot(const QString& snapshot);

    /**
     * @brief Returns the state of the strip overlays and their authoring records.
     * @return The overlay state.
     */
    [[nodiscard]] OverlayState overlayState() const;
    /**
     * @brief Restores the overlay state.
     * @param state The overlay state to restore.
     */
    void restoreOverlayState(const OverlayState& state);

protected:
    /**
     * @brief Intercepts drag/drop on the input list's viewport so images (or folders) dropped from
     * the file manager are added via the same path as Add files / Add from directory. Internal
     * reorder drags (which carry no file URLs) fall through to the list's own InternalMove handling.
     * 
     * @param watched The object being watched for events.
     * @param event The event being filtered.
     * @return True if the event was handled, false otherwise.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief Whole-widget drop target: images / folders dropped **anywhere** on the project panel
     * (not only on the input tile list) are added via the same path as Add files / Add from directory.
     * Drops that land directly on the input list are still handled by eventFilter() (so its InternalMove
     * reorder keeps working); these three catch every other area of the panel.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void projectModified();                         //!< emitted when the project is modified (inputs, outputs, profiles, etc.)
    void renderToggleRequested(int projectIndex);   //!< Render/Stop button clicked
    void viewStripRequested(int projectIndex);      //!< "View strip" button clicked — open the strip viewer for this project

    /**
     * @brief A workspace-level edit was made from this project dock (canvas-profile content edit,
     *        output-format edit). Carries the WorkspaceEditor::snapshotMeta strings from before/after
     *        the edit so MainWindow can push it onto the workspace undo stack.
     * @param text The text to display for the undo action.
     * @param before The workspace metadata snapshot before the edit.
     * @param after The workspace metadata snapshot after the edit.
     */
    void workspaceEditCommitted(const QString& text, const QString& before, const QString& after);

    /**
     * @brief This project's authoring records changed — MainWindow folds them back into its per-project cache.
     * @param artifacts The new artifact map for this project.
     */
    void artifactsChanged(const ArtifactMap& artifacts);

    /**
     * @brief A step was undone or redone: @p scope says which dock shows the difference, and @p uids
     *        names the objects it touched (empty for a project-scope step, which has none).
     *
     * Emitted only by the restores, never by the edit that created the step: an edit is already on
     * screen where it was made. MainWindow uses it to raise that dock, so a step taken in the window
     * the user is not looking at cannot pass as "Ctrl+Z did nothing", and to select what changed, so
     * a step whose effect is small cannot pass for nothing either.
     *
     * **Emitted before the state goes out to the views**, because the selection is armed on the editor
     * and consumed by the feed that follows.
     * 
     * @param scope The scope of the edit that was undone or redone (ProjectDock or StripEditor).
     * @param uids The UIDs of the objects touched by the edit (empty for a project-scope edit).
     */
    void historyStepApplied(EditScope scope, const QStringList& uids);

private slots:
    void onAddFromDirectory();                      //!< Slot for when the "Add Inputs from Directory" button is clicked. Opens a QFileDialog to select a directory and adds all image files from that directory to the input list.
    void onAddFiles();                              //!< Slot for when the "Add Input Files" button is clicked. Opens a QFileDialog to select image files and adds them to the input list.
    void onClearInputs();                           //!< Slot for when the "Clear Inputs" button is clicked. Clears all input files from the list after confirmation.
    void onApplySort();                             //!< Slot for when the "Apply Sort" button is clicked. Sorts the input files based on the selected sorting option (name, date created, or date modified).
    void onGoToOutput();                            //!< Slot for when the "Go to Output" button is clicked. Switches the UI to the Output tab.
    void onRowsMoved();                             //!< Slot for when rows in the input list are moved (drag-and-drop). Updates the order of the input files in the workspace accordingly.
    void onInputContextMenu(const QPoint& pos);     //!< Slot for when the user right-clicks on the input list. Displays a context menu with options to move selected tiles up or down, or delete them.
    void onTileMoveUp(const QString& filePath);     //!< Slot for when user clicks the move-up button on a tile. Swaps its order with the previous tile.
    void onTileMoveDown(const QString& filePath);   //!< Slot for when user clicks the move-down button on a tile. Swaps its order with the next tile.
    void onAssignCanvasProfiles();                  //!< Slot for the "Assign Canvas Profiles" button. Lets the user pick an unassigned workspace canvas profile to link to this project.
    void onCanvasProfileDoubleClicked(QListWidgetItem* item);   //!< Slot for double-clicking an assigned canvas profile. Opens CanvasProfileDialog to edit it in place.
    void onOutputProfileChanged(int index);         //!< Slot for when the output profile combo box selection changes. Updates the project's outputProfileId and refreshes the format controls.
    void onSelectOutputDir();                       //!< Slot for the "Select Output Directory" button. Opens a directory picker and stores the chosen path as the project's output directory.
    void onClearOutputDir();                        //!< Slot for the "Clear Output Directory" button. Clears the project's configured output directory.
    void onOpenOutputDir();                         //!< Slot for the "Open Output Directory" button. Opens the output directory in the system file explorer.
    void onFormatOptionsEdited();                   //!< OutputFormatOptionsWidget::edited → write back
    void onJumpToInput();                           //!< Slot for the "Jump to Input" button. Switches the UI to the Input tab.
    void onRefreshFiles();                          //!< Re-scan inputs+outputs on disk, refresh statuses/tiles

private:
    //! Project state as this GUI defines it: the library's snapshot plus the authoring records.
    [[nodiscard]] QString fullSnapshot();

    void addImageTile(const Platemaker::Models::InputFile& file);           //!< Creates an ImageTile widget for an input file and inserts it into the input list.
    void addOutputImageTile(const Platemaker::Models::OutputFile& file);    //!< Creates an ImageTile widget for an existing output file and inserts it into the output list.
    void addInputPaths(const QStringList& newPaths);                        //!< Merges new paths with the existing inputs (order-preserving, de-duplicated) and re-scans them.

    /**
     * @brief Asks for a replacement for the input at @p currentPath and swaps it in, **keeping the page**.
     *
     * The operation for "I have a newer scan of page 4". Removing the input and adding the new file
     * would mint a new uid and strand every object anchored to the old one; this keeps the uid, so
     * nothing unanchors, because anchoring was never about the file.
     * 
     * @param currentPath The path of the input file to be replaced.
     */
    void replaceInput(const QString& currentPath);

    /**
     * @brief Removes every input not in @p remainingPaths, after asking — and says what it strands.
     *
     * Both ways of removing inputs come through here, so neither can forget the question. When objects
     * are anchored to a page being removed, the confirmation says how many and offers to delete them
     * in the same step: one click, one undo, though it is two kinds of edit underneath. Keeping them is
     * the default, because they are not lost — they wait, unanchored, for a page to be given back.
     *
     * @param question  The confirmation's first sentence, specific to the caller.
     */
    void removeInputs(const std::vector<std::string>& remainingPaths, const QString& title,
                      const QString& question, const QString& undoText);
    void addDroppedUrls(const QList<QUrl>& urls);                           //!< Turns dropped file/folder URLs into image paths (folders scanned like Add from directory) and adds them as one undo step.
    /**
     * @brief Rebuilds the "Workflow" tab's pipeline map from the current project — a read-only row of
     * StageCards (Inputs → Margin crop → Colour correction → Resize → Slice → Text & bubbles → Output)
     * that doubles as a launchpad: fixed stages jump to the Input/Output tab, the optional CC/text
     * stages open the strip editor (and enable themselves). Called from populate().
     */
    void refreshWorkflowMap();

    void refreshCanvasProfilesList();      //!< Rebuilds listWidgetCanvasProfiles from the project's assigned canvas profile IDs.
    void refreshOutputProfileCombo();      //!< Repopulates comboBoxOutputProfile from the workspace's output profiles, selecting the project's current one.
    void refreshOutputDirectoryDisplay();  //!< Updates textOutputDirectory to show the project's current output directory.
    void refreshFormatControls();          //!< Reflects the selected output profile in m_formatOptions (or disables it if none selected).

    /**
     * @brief Checks whether the project's existing outputs are stale relative to
     * the current output configuration (format/size/quality changed since they
     * were rendered). Drives the "Out of sync" badge shown after Refresh.
     * @return true if the outputs no longer match the current configuration.
     */
    [[nodiscard]] bool outputsConfigStale() const;

    void setupUndo();   //!< Creates this project's undo stack (depth 10). MainWindow adds it to the group.

    /**
     * @brief Records one undoable **project-scope** edit onto this project's stack.
     *
     * Brackets \p mutate with ProjectEditor::snapshot() before/after and pushes a
     * ProjectSnapshotCommand if the project actually changed (a no-op edit records nothing).
     * @param text   Short label for the operation (shown in the undo action's text).
     * @param mutate The operation to perform (add / clear / reorder / sort / link / output selection /
     *               output dir). It still does its own populate()/projectModified().
     */
    void commitEdit(const QString& text, const std::function<void()>& mutate);

    /**
     * @brief Records one undoable **text & bubbles** edit — same history, other half of the document.
     *
     * The same bracket as commitEdit() over a much smaller snapshot, which is the reason the two are
     * separate methods: restoring the lettering does not have to reload the project, and neither can
     * tread on the other's half. Which half an operation belongs to is decided by what it changes, not
     * by which window it was triggered from: clearing every bubble is a project-dock button and still
     * belongs here.
     */
    void commitOverlayEdit(const QString& text, const std::function<void()>& mutate);

    /**
     * @brief Records one undoable **workspace-scope** edit triggered from this dock onto the workspace
     *        timeline (canvas-profile content edit, output-format edit).
     *
     * Brackets \p mutate with WorkspaceEditor::snapshotMeta before/after and, if anything changed,
     * emits workspaceEditCommitted so MainWindow pushes it onto the workspace undo stack.
     */
    void commitWorkspaceEdit(const QString& text, const std::function<void()>& mutate);

    Ui::Project* ui;                                        //!< Qt Designer-generated UI for this widget.
    int m_projectIndex;                                     //!< Index of this project within m_workspace.projectItems (kept in sync via setProjectIndex()).
    Platemaker::Models::Workspace& m_workspace;             //!< Reference to the workspace owning this project's data.
    QString     m_workspacePath;                            //!< Workspace file path — the root of overlays/.
    ArtifactMap m_artifacts;                                //!< Authoring records for this project's overlays, by uid.
    QString m_cacheDir;                                     //!< Directory where cached thumbnails and other temporary files are stored.
    OutputFormatOptionsWidget* m_formatOptions = nullptr;   //!< Shared widget for editing the selected output profile's format/options.
    QVBoxLayout* m_workflowStack = nullptr;                  //!< Vertical stack of the Workflow tab's StageCards (built in the ctor, rebuilt by refreshWorkflowMap()).
    // Owned by MainWindow and alive for the session — see the constructor.
    QUndoStack* m_undoStack = nullptr;      //!< This project's whole history.
};

#endif // PROJECT_HPP
