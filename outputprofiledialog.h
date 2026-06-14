#ifndef OUTPUTPROFILEDIALOG_H
#define OUTPUTPROFILEDIALOG_H

#include <QDialog>
#include <platemaker/models/output_profile.hpp>

namespace Ui {
class OutputProfileDialog;
}

class OutputProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OutputProfileDialog(QWidget *parent = nullptr);
    ~OutputProfileDialog() override;

    void setProfile(const Platemaker::Models::OutputProfile &profile);
    Platemaker::Models::OutputProfile profile() const;

private slots:
    void onSaveClicked();
    void onFormatChanged();   // shows/hides JPEG or WEBP option panels

private:
    Ui::OutputProfileDialog *ui;
};

#endif // OUTPUTPROFILEDIALOG_H
