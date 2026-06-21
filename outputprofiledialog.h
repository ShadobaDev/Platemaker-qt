#ifndef OUTPUTPROFILEDIALOG_H
#define OUTPUTPROFILEDIALOG_H

#include <QDialog>
#include <platemaker/models/output_profile.hpp>

namespace Ui {
class OutputProfileDialog;
}
class OutputFormatOptionsWidget;

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

private:
    Ui::OutputProfileDialog *ui;
    OutputFormatOptionsWidget* m_formatOptions = nullptr;
};

#endif // OUTPUTPROFILEDIALOG_H
