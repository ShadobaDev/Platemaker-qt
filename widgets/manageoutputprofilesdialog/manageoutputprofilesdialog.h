#ifndef MANAGEOUTPUTPROFILESDIALOG_H
#define MANAGEOUTPUTPROFILESDIALOG_H

#include <QDialog>
#include <QList>
#include <platemaker/models/output_profile.hpp>

namespace Ui {
class ManageOutputProfilesDialog;
}

class ManageOutputProfilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManageOutputProfilesDialog(QWidget *parent = nullptr);
    ~ManageOutputProfilesDialog() override;

    void setProfiles(const QList<Platemaker::Models::OutputProfile> &profiles,
                     const QString &activeProfileName);

    QList<Platemaker::Models::OutputProfile> profiles() const;
    QString activeProfileName() const;

private slots:
    void onSelectionChanged();
    void onNewClicked();
    void onEditClicked();
    void onDuplicateClicked();
    void onDeleteClicked();
    void onSetActiveClicked();

private:
    void rebuildList();
    void updateButtonStates();
    int  selectedRow() const;

    Ui::ManageOutputProfilesDialog *ui;

    QList<Platemaker::Models::OutputProfile> m_profiles;
    QString                  m_activeProfileName;
};

#endif // MANAGEOUTPUTPROFILESDIALOG_H
