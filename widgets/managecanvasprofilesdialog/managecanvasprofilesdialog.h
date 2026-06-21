#ifndef MANAGECANVASPROFILESDIALOG_H
#define MANAGECANVASPROFILESDIALOG_H

#include <QDialog>
#include <QList>
#include <platemaker/models/canvas_profile.hpp>

namespace Ui {
class ManageCanvasProfilesDialog;
}

class ManageCanvasProfilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManageCanvasProfilesDialog(QWidget *parent = nullptr);
    ~ManageCanvasProfilesDialog() override;

    // Populate the list before calling exec().
    void setProfiles(const QList<Platemaker::Models::CanvasProfile> &profiles,
                     const QString &activeProfileName);

    // Retrieve results after exec() == Accepted.
    QList<Platemaker::Models::CanvasProfile> profiles() const;
    QString activeProfileName() const;

signals:
    // Emitted when the user clicks "Generate templates from active".
    // Connect in MainWindow to trigger the actual generation.
    void generateTemplatesRequested(const Platemaker::Models::CanvasProfile &activeProfile);

private slots:
    void onSelectionChanged();
    void onNewClicked();
    void onEditClicked();
    void onDuplicateClicked();
    void onDeleteClicked();
    void onSetActiveClicked();
    void onGenerateTemplatesClicked();

private:
    void rebuildList();
    void updateButtonStates();
    int  selectedRow() const;

    Ui::ManageCanvasProfilesDialog *ui;

    QList<Platemaker::Models::CanvasProfile> m_profiles;
    QString                  m_activeProfileName;
};

#endif // MANAGECANVASPROFILESDIALOG_H
