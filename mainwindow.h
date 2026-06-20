#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QStringList>

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QDockWidget;
class QListWidgetItem;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Workspace menu
    void onOpenWorkspace();
    void onNewWorkspace();
    void onSave();
    void onSaveAs();
    void onCloseWorkspace();
    void onRevealInExplorer();

    // Project panel
    void onNewProject();
    void onProjectDoubleClicked(QListWidgetItem *item);

    // Canvas profile actions
    void onManageCanvasProfiles();
    void onNewCanvasProfile();
    void onEditActiveCanvasProfile();

    // Output profile actions
    void onManageOutputProfiles();
    void onNewOutputProfile();
    void onEditActiveOutputProfile();

    // Template actions
    void onManageTemplates();
    void onOpenTemplatesDir();

private:
    // --- helpers ---
    bool maybeSave();                           // true = safe to proceed
    void loadWorkspace(const QString &path);
    void applyWorkspaceToUi();
    void closeWorkspace();
    void setDirty(bool dirty);
    void updateTitle();

    // Captures the current workspace's serialized form as the "saved" baseline.
    // Call after every successful load/save; clears the dirty flag.
    void captureSnapshot();
    // Authoritative change check: true if the workspace differs from the last
    // captured snapshot. Robust against any action that forgot to setDirty().
    [[nodiscard]] bool isWorkspaceModified() const;

    // --- recent workspaces (advisory list in QSettings; never required) ---
    [[nodiscard]] QStringList recentWorkspaces() const;
    void addToRecentWorkspaces(const QString &path);
    void rebuildRecentMenu();
    void openRecentWorkspace(const QString &path);
    [[nodiscard]] QString defaultDialogDir() const;

    // --- project dock management ---
    void openProjectDock(int projectIndex);
    void closeProjectByIndex(int index);
    void toggleProjectFloatState(int index);

    // --- members ---
    static constexpr int k_maxRecentWorkspaces = 10;

    Ui::MainWindow *ui;
    QList<QDockWidget *> m_openProjectDocks;
    QMenu *m_recentMenu = nullptr;     // submenu attached to actionOpen_recent_workspace

    Platemaker::Models::Workspace                      m_workspace;
    Platemaker::Infrastructure::WorkspaceSerializer    m_serializer;
    QString m_workspacePath;
    bool    m_dirty = false;           // eager flag driving the title-bar asterisk
    QString m_savedSnapshot;           // serialized workspace at last load/save

    // UI-only selection state (not persisted in the workspace model since v2).
    // Shouldn't be used per project, or even per input file?
    QString m_activeCanvasProfileName;
    QString m_activeOutputProfileName;
};

#endif // MAINWINDOW_H
