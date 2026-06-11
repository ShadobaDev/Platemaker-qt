#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDockWidget>
#include <QList>        // <-- DODAJ TO
#include <QDockWidget>  // <-- DODAJ TO

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void addNewProject(QString projectName);
private:
    void closeProjectByIndex(int index);
    void toggleProjectFloatState(int index);
private:
    Ui::MainWindow *ui;
    QList<QDockWidget*> openProjectsList;
};
#endif // MAINWINDOW_H
