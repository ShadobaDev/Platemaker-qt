#ifndef PROJECT_H
#define PROJECT_H

#include <QWidget>
#include <QList>

#include <functional>
#include <string>
#include <vector>

#include <platemaker/models/workspace.hpp>

namespace Ui { class Project; }
class QListWidgetItem;
class OutputFormatOptionsWidget;
class QUndoStack;
class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QUrl;

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
     * @param projectIndex The index of the project within the workspace.
     * @param workspace A reference to the workspace containing the project data.
     * @param cacheDir The directory where cached thumbnails and other temporary files are stored.
     * @param parent The parent widget, if any.
     */
    explicit Project(int projectIndex,
                     Platemaker::Models::Workspace& workspace,
                     const QString& cacheDir,
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
    void setRendering(bool rendering);              //!<  flips Render⇄Stop + disables output controls
    void setOutputTile(int index, const QString& name, const QString& fullPath); //!< live positional update during a render — creates or replaces the tile at row \p index
    void setInputTileStatus(const QString& filePath, Platemaker::Models::FileStatus status,
                            bool renderedWithoutProfile = false); //!< live per-input update during a render — repaints the input tile matching \p filePath (cyan when Processed without a canvas profile)
    void refreshOutputTiles();                      //!< rebuild from getOutputImages()
    void refreshProfileViews();                     //!< rebuilds the palette-derived views (canvas list, output combo, format controls) after a workspace-level profile edit — see MainWindow::workspaceProfilesChanged

    // --- Undo / redo ---
    // Project-scope edits (inputs, canvas links, output-profile selection, output dir) go on this
    // project's own QUndoStack; MainWindow adds it to a QUndoGroup and makes it active when this
    // dock is visible. Workspace-scope edits triggered from here (canvas-profile *content* edit,
    // output-format edit) are bracketed with WorkspaceEditor::snapshotMeta and forwarded to MainWindow
    // via workspaceEditCommitted so they land on the workspace timeline instead.
    [[nodiscard]] QUndoStack* undoStack() const { return m_undoStack; } //!< This project's undo stack (owned here; added to MainWindow's group).
    void applyProjectSnapshot(const QString& snapshot);  //!< Restore the project from a ProjectEditor::snapshot string, repopulate, mark modified. Called by ProjectSnapshotCommand.

protected:
    /**
     * @brief Intercepts drag/drop on the input list's viewport so images (or folders) dropped from
     * the file manager are added via the same path as Add files / Add from directory. Internal
     * reorder drags (which carry no file URLs) fall through to the list's own InternalMove handling.
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

    /**
     * @brief A workspace-level edit was made from this project dock (canvas-profile content edit,
     *        output-format edit). Carries the WorkspaceEditor::snapshotMeta strings from before/after
     *        the edit so MainWindow can push it onto the workspace undo stack.
     */
    void workspaceEditCommitted(const QString& text, const QString& before, const QString& after);

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
    void addImageTile(const Platemaker::Models::InputFile& file);           //!< Creates an ImageTile widget for an input file and inserts it into the input list.
    void addOutputImageTile(const Platemaker::Models::OutputFile& file);    //!< Creates an ImageTile widget for an existing output file and inserts it into the output list.
    void addInputPaths(const QStringList& newPaths);                        //!< Merges new paths with the existing inputs (order-preserving, de-duplicated) and re-scans them.
    void addDroppedUrls(const QList<QUrl>& urls);                           //!< Turns dropped file/folder URLs into image paths (folders scanned like Add from directory) and adds them as one undo step.
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
    QString m_cacheDir;                                     //!< Directory where cached thumbnails and other temporary files are stored.
    OutputFormatOptionsWidget* m_formatOptions = nullptr;   //!< Shared widget for editing the selected output profile's format/options.
    QUndoStack* m_undoStack = nullptr;                      //!< Per-project undo history for input-list edits (owned via QObject parent).
};

#endif // PROJECT_H
