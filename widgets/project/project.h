#ifndef PROJECT_H
#define PROJECT_H

#include <QWidget>

#include <platemaker/models/workspace.hpp>

namespace Ui { class Project; }
class QListWidgetItem;
class OutputFormatOptionsWidget;

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
    void addOutputTile(const QString& filePath);    //!< live append during a render
    void refreshOutputTiles();                      //!< rebuild from getOutputImages()

signals:
    void projectModified();                         //!< emitted when the project is modified (inputs, outputs, profiles, etc.)
    void renderToggleRequested(int projectIndex);   //!< Render/Stop button clicked

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
    void refreshCanvasProfilesList();      //!< Rebuilds listWidgetCanvasProfiles from the project's assigned canvas profile IDs.
    void refreshOutputProfileCombo();      //!< Repopulates comboBoxOutputProfile from the workspace's output profiles, selecting the project's current one.
    void refreshOutputDirectoryDisplay();  //!< Updates textOutputDirectory to show the project's current output directory.
    void refreshFormatControls();          //!< Reflects the selected output profile in m_formatOptions (or disables it if none selected).

    /**
     * @brief Returns the project's currently selected output profile.
     * @return A mutable pointer to the output profile, or nullptr if none is selected
     * or the selected ID no longer matches any workspace profile.
     */
    [[nodiscard]] Platemaker::Models::OutputProfile* selectedOutputProfile() const;

    /**
     * @brief Checks whether the project's existing outputs are stale relative to
     * the current output configuration (format/size/quality changed since they
     * were rendered). Drives the "Out of sync" badge shown after Refresh.
     * @return true if the outputs no longer match the current configuration.
     */
    [[nodiscard]] bool outputsConfigStale() const;

    Ui::Project* ui;                                        //!< Qt Designer-generated UI for this widget.
    int m_projectIndex;                                     //!< Index of this project within m_workspace.projectItems (kept in sync via setProjectIndex()).
    Platemaker::Models::Workspace& m_workspace;             //!< Reference to the workspace owning this project's data.
    QString m_cacheDir;                                     //!< Directory where cached thumbnails and other temporary files are stored.
    OutputFormatOptionsWidget* m_formatOptions = nullptr;   //!< Shared widget for editing the selected output profile's format/options.
};

#endif // PROJECT_H
