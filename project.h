#ifndef PROJECT_H
#define PROJECT_H

#include <QWidget>

#include <platemaker/models/workspace.hpp>

namespace Ui { class Project; }

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

signals:
    void projectModified();

private slots:
    void onAddFromDirectory();
    void onRowsMoved();

private:
    void addImageTile(const Platemaker::Models::InputFile& file);

    Ui::Project* ui;
    int m_projectIndex;
    Platemaker::Models::Workspace& m_workspace;
    QString m_cacheDir;
};

#endif // PROJECT_H
