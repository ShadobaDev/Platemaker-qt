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
    void onProjectsContextMenu(const QPoint &pos);

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

    // Render / processing
    void onRenderToggle(int projectIndex);   // Render/Stop from a Project widget
    void onRenderProgress(int done, int total, QString sliceName);
    void onRenderLog(int level, QString message);
    void onRenderSliceSaved(QString name, QString fullPath);
    void onRenderFinished();

private:
    // --- render orchestration ---
    void startRender(int projectIndex);
    void cancelRender();
    [[nodiscard]] Project *projectWidget(int projectIndex) const;
    [[nodiscard]] Platemaker::Models::OutputProfile resolveOutputProfileFor(
        const Platemaker::Models::ProjectItem &project) const;
    void setActionStatus(const QString &projectName, const QString &action);
    void setProjectStatus(const QString &message);
    void setProgressValue(int percent, bool error);

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

    // --- project management ---
    void renameProject(int modelIndex);
    void removeProject(int modelIndex);
    [[nodiscard]] class QDockWidget *dockForProject(int modelIndex) const;

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

    // --- render state (one render at a time, owned here) ---
    Platemaker::Infrastructure::CancellationToken m_cancelToken;
    QThread      *m_renderThread       = nullptr;
    RenderWorker *m_renderWorker        = nullptr;
    bool          m_rendering           = false;
    int           m_renderProjectIndex  = -1;
    int           m_activeProjectIndex  = -1;  // last-raised project dock (for F5/menu)
};

#endif // MAINWINDOW_H
