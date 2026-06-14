#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QDockWidget;
class QListWidgetItem;

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

private:
    // --- helpers ---
    bool maybeSave();                           // true = safe to proceed
    void loadWorkspace(const QString &path);
    void applyWorkspaceToUi();
    void closeWorkspace();
    void setDirty(bool dirty);
    void updateTitle();

    // --- project dock management ---
    void openProjectDock(int projectIndex);
    void closeProjectByIndex(int index);
    void toggleProjectFloatState(int index);

    // --- members ---
    Ui::MainWindow *ui;
    QList<QDockWidget *> m_openProjectDocks;

    Platemaker::Models::Workspace                      m_workspace;
    Platemaker::Infrastructure::WorkspaceSerializer    m_serializer;
    QString m_workspacePath;
    bool    m_dirty = false;

    // UI-only selection state (not persisted in the workspace model since v2).
    // Shouldn't be used per project, or even per input file?
    QString m_activeCanvasProfileName;
    QString m_activeOutputProfileName;
};

#endif // MAINWINDOW_H
