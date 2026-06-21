#ifndef PROJECT_H
#define PROJECT_H

#include <QWidget>

#include <platemaker/models/workspace.hpp>

namespace Ui { class Project; }
class QListWidgetItem;
class OutputFormatOptionsWidget;

class Project : public QWidget
{
    Q_OBJECT

public:
    explicit Project(int projectIndex,
                     Platemaker::Models::Workspace& workspace,
                     const QString& cacheDir,
                     QWidget *parent = nullptr);
    ~Project();

    void populate();

    // Updates the workspace index this widget refers to. Used when a lower-indexed
    // project is removed and the vector shifts (the widget reads the index live).
    void setProjectIndex(int index) { m_projectIndex = index; }

    // --- render UI (driven by MainWindow, which owns the render state) ---
    void setRendering(bool rendering);             // flips Render⇄Stop + disables output controls
    void addOutputTile(const QString& filePath);   // live append during a render
    void refreshOutputTiles();                      // rebuild from getOutputImages()

signals:
    void projectModified();
    void renderToggleRequested(int projectIndex);   // Render/Stop button clicked

private slots:
    void onAddFromDirectory();
    void onAddFiles();
    void onClearInputs();
    void onApplySort();
    void onGoToOutput();
    void onRowsMoved();
    void onInputContextMenu(const QPoint& pos);
    void onTileMoveUp(const QString& filePath);
    void onTileMoveDown(const QString& filePath);
    void onAssignCanvasProfiles();
    void onCanvasProfileDoubleClicked(QListWidgetItem* item);
    void onOutputProfileChanged(int index);
    void onSelectOutputDir();
    void onClearOutputDir();
    void onFormatOptionsEdited();   // OutputFormatOptionsWidget::edited → write back
    void onJumpToInput();
    void onRefreshFiles();          // re-scan inputs+outputs on disk, refresh statuses/tiles

private:
    void addImageTile(const Platemaker::Models::InputFile& file);
    void addOutputImageTile(const Platemaker::Models::OutputFile& file);
    void addInputPaths(const QStringList& newPaths);
    void refreshCanvasProfilesList();
    void refreshOutputProfileCombo();
    void refreshOutputDirectoryDisplay();
    void refreshFormatControls();          // reflect the selected output profile
    // Mutable pointer to the project's selected output profile, or nullptr.
    [[nodiscard]] Platemaker::Models::OutputProfile* selectedOutputProfile() const;

    Ui::Project* ui;
    int m_projectIndex;
    Platemaker::Models::Workspace& m_workspace;
    QString m_cacheDir;
    OutputFormatOptionsWidget* m_formatOptions = nullptr;
};

#endif // PROJECT_H
